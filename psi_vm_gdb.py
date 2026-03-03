"""
GDB debugger pretty-printers for psi::vm containers.

Integration:
  Option A: Add to ~/.gdbinit for automatic loading:
    python
    import sys
    sys.path.insert(0, '/path/to/psi/vm')
    import psi_vm_gdb
    end

  Option B: Load manually in a GDB session:
    (gdb) source /path/to/psi_vm_gdb.py

  Option C: For VSCode with cppdbg (GDB), add to launch.json:
    "setupCommands": [
        { "text": "source /path/to/psi_vm_gdb.py" }
    ]

Type name notes:
  GDB shows expanded type names for template aliases:
    heap_vector<T,sz,opts>    -> vector<heap_storage<T,sz,void,opts>, geometric_growth{}>
    fc_vector<T,N,oh>       -> vector<fixed_storage<T,N,oh>>
    small_vector<T,N,s,o>   -> vector<sbo_hybrid<T,N,s,o>, geometric_growth{}>
  Patterns match both old (typedef) and new (expanded) names.
"""

import gdb
import re


# ==============================================================================
# Helpers
# ==============================================================================

def get_type_tag(val):
    """Return the unqualified type tag, stripping typedefs and references."""
    t = val.type
    if t.code == gdb.TYPE_CODE_REF or t.code == gdb.TYPE_CODE_RREF:
        t = t.target()
    t = t.unqualified().strip_typedefs()
    return t.tag or str(t)


def find_field(val, name):
    """Find a field by name, searching through base classes."""
    try:
        return val[name]
    except gdb.error:
        pass
    # Search base classes
    t = val.type.strip_typedefs()
    for f in t.fields():
        if f.is_base_class:
            try:
                base = val.cast(f.type)
                return find_field(base, name)
            except gdb.error:
                continue
    raise gdb.error(f"No field '{name}' in {t}")


def safe_find_field(val, name):
    """Like find_field but returns None on failure."""
    try:
        return find_field(val, name)
    except gdb.error:
        return None


# ==============================================================================
# vector<heap_storage<...>> / heap_vector
# ==============================================================================

class HeapVectorPrinter:
    """Pretty-printer for vector<heap_storage<...>> (and legacy heap_vector).
    Data members (inherited from heap_storage base): p_array_, size_."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        size = int(find_field(self.val, 'size_'))
        return f'size={size}'

    def children(self):
        p = find_field(self.val, 'p_array_')
        size = int(find_field(self.val, 'size_'))
        for i in range(size):
            yield f'[{i}]', (p + i).dereference()

    def display_hint(self):
        return 'array'


# ==============================================================================
# vector<fixed_storage<...>> / fc_vector
# ==============================================================================

class FixedVectorPrinter:
    """Pretty-printer for vector<fixed_storage<...>> (and legacy fc_vector).
    Data members (inherited from fixed_storage base): size_, array_."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        size = int(find_field(self.val, 'size_'))
        return f'size={size}'

    def children(self):
        size = int(find_field(self.val, 'size_'))
        arr = find_field(self.val, 'array_')
        data = arr['data']
        for i in range(size):
            yield f'[{i}]', data[i]

    def display_hint(self):
        return 'array'


# ==============================================================================
# vector<sbo_hybrid<...>> / small_vector
# ==============================================================================

class SmallVectorPrinter:
    """Pretty-printer for psi::vm::small_vector across all 3 layout modes.

    Layout detection via member probing:
      embedded:     storage_.heap_.sz_ exists (CIS field inside union)
      compact_lsb:  external size_ before storage_, LSB flag
      compact:      external size_ after storage_, MSB flag
    """

    def __init__(self, val):
        self.val = val
        self._detect_layout()

    def _detect_layout(self):
        storage = find_field(self.val, 'storage_')
        heap = storage['heap_']

        # Try embedded layout: has sz_ inside heap_ union member
        sz_field = safe_find_field(heap, 'sz_')
        if sz_field is not None:
            raw = int(sz_field)
            self.is_heap = (raw & 1) != 0
            self.size = raw >> 1
            self.layout = 'embedded'
            if self.is_heap:
                self.data_ptr = heap['data_']
            else:
                inline = storage['inline_']
                self.data_ptr = inline['elements_']['data']
            return

        # compact_lsb or compact: has external size_ and storage_.heap_.capacity_
        size_field = find_field(self.val, 'size_')
        raw = int(size_field)

        # Heuristic: compact_lsb is the default (auto_select when sizeof(sz_t) > alignof(T)).
        # compact uses MSB flag. Without address probing, default to compact_lsb.
        # If MSB is suspiciously set and LSB is 0, try compact interpretation.
        sz_bytes = size_field.type.sizeof
        msb = 1 << (sz_bytes * 8 - 1)

        if (raw & 1) != 0:
            # LSB set -> compact_lsb (heap mode)
            self.layout = 'compact_lsb'
            self.is_heap = True
            self.size = raw >> 1
            self.data_ptr = heap['data_']
        elif (raw & msb) != 0:
            # MSB set -> compact (heap mode)
            self.layout = 'compact'
            self.is_heap = True
            self.size = raw & ~msb
            self.data_ptr = heap['data_']
        else:
            # Neither bit set -> inline mode (same for both compact variants)
            self.layout = 'compact_lsb'  # assume compact_lsb (more common default)
            self.is_heap = False
            self.size = raw >> 1
            buf = storage['buffer_']
            self.data_ptr = buf['data']

    def to_string(self):
        mode = 'heap' if self.is_heap else 'inline'
        return f'size={self.size}, {mode}'

    def children(self):
        if self.data_ptr is None:
            return
        ptr_type = self.data_ptr.type
        if ptr_type.code == gdb.TYPE_CODE_PTR:
            for i in range(self.size):
                yield f'[{i}]', (self.data_ptr + i).dereference()
        elif ptr_type.code == gdb.TYPE_CODE_ARRAY:
            for i in range(self.size):
                yield f'[{i}]', self.data_ptr[i]

    def display_hint(self):
        return 'array'


# ==============================================================================
# flat_set_impl / flat_set / flat_multiset
# ==============================================================================

def _get_vector_data_and_size(container):
    """Extract (data_ptr, size) from a psi::vm vector or std::vector."""
    # psi::vm vector<heap_storage>
    p = safe_find_field(container, 'p_array_')
    if p is not None:
        size = int(find_field(container, 'size_'))
        return p, size

    # psi::vm vector<fixed_storage>
    arr = safe_find_field(container, 'array_')
    if arr is not None:
        size = int(find_field(container, 'size_'))
        return arr['data'], size

    # libstdc++ std::vector
    impl = safe_find_field(container, '_M_impl')
    if impl is not None:
        start = impl['_M_start']
        finish = impl['_M_finish']
        elem_size = start.type.target().sizeof
        size = (int(finish) - int(start)) // elem_size if elem_size > 0 else 0
        return start, size

    # MS STL (unlikely in GDB, but for completeness)
    pair = safe_find_field(container, '_Mypair')
    if pair is not None:
        val2 = pair['_Myval2']
        first = val2['_Myfirst']
        last = val2['_Mylast']
        elem_size = first.type.target().sizeof
        size = (int(last) - int(first)) // elem_size if elem_size > 0 else 0
        return first, size

    return None, 0


class FlatSetPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        storage = find_field(self.val, 'storage_')
        _, size = _get_vector_data_and_size(storage)
        return f'size={size}'

    def children(self):
        storage = find_field(self.val, 'storage_')
        data_ptr, size = _get_vector_data_and_size(storage)
        if data_ptr is None:
            return
        if data_ptr.type.code == gdb.TYPE_CODE_PTR:
            for i in range(size):
                yield f'[{i}]', (data_ptr + i).dereference()
        elif data_ptr.type.code == gdb.TYPE_CODE_ARRAY:
            for i in range(size):
                yield f'[{i}]', data_ptr[i]

    def display_hint(self):
        return 'array'


# ==============================================================================
# flat_map_impl / flat_map / flat_multimap
# ==============================================================================

class FlatMapPrinter:
    """Shows flat_map entries as [key] = value pairs."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        storage = find_field(self.val, 'storage_')
        keys = storage['keys']
        _, size = _get_vector_data_and_size(keys)
        return f'size={size}'

    def children(self):
        storage = find_field(self.val, 'storage_')
        keys_container = storage['keys']
        vals_container = storage['values']
        keys_ptr, size = _get_vector_data_and_size(keys_container)
        vals_ptr, _ = _get_vector_data_and_size(vals_container)
        if keys_ptr is None or vals_ptr is None:
            return
        for i in range(size):
            if keys_ptr.type.code == gdb.TYPE_CODE_PTR:
                key = (keys_ptr + i).dereference()
            else:
                key = keys_ptr[i]
            if vals_ptr.type.code == gdb.TYPE_CODE_PTR:
                val = (vals_ptr + i).dereference()
            else:
                val = vals_ptr[i]
            yield f'[{key}]', val

    def display_hint(self):
        return 'map'


# ==============================================================================
# detail::paired_storage
# ==============================================================================

class PairedStoragePrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        keys = self.val['keys']
        _, size = _get_vector_data_and_size(keys)
        return f'size={size}'

    def children(self):
        yield '[keys]', self.val['keys']
        yield '[values]', self.val['values']


# ==============================================================================
# bptree_base / bp_tree_impl
# ==============================================================================

class BPTreePrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        p_hdr = find_field(self.val, 'p_hdr_')
        ptr = p_hdr['ptr']
        if int(ptr) != 0:
            hdr = ptr.dereference()
            size = int(hdr['size_'])
            depth = int(hdr['depth_'])
            return f'size={size}, depth={depth}'
        return 'empty / uninitialized'


# ==============================================================================
# pass_in_reg / pass_rv_in_reg
# ==============================================================================

class PassInRegPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        v = self.val['value']
        return str(v)

    def children(self):
        yield 'value', self.val['value']


# ==============================================================================
# Registration
# ==============================================================================

def _build_lookup():
    """Build a list of (compiled_regex, printer_class) pairs."""
    prefix = r'psi::vm::'
    patterns = [
        # vector<heap_storage<...>>
        (rf'^{prefix}vector<{prefix}heap_storage<',       HeapVectorPrinter),
        (rf'^{prefix}heap_vector<',                          HeapVectorPrinter),

        # vector<fixed_storage<...>>
        (rf'^{prefix}vector<{prefix}fixed_storage<',       FixedVectorPrinter),
        (rf'^{prefix}fc_vector<',                          FixedVectorPrinter),

        # vector<sbo_hybrid<...>>
        (rf'^{prefix}vector<{prefix}sbo_hybrid<',          SmallVectorPrinter),
        (rf'^{prefix}small_vector<',                       SmallVectorPrinter),

        # flat_set_impl / flat_set / flat_multiset
        (rf'^{prefix}flat_set_impl<',                      FlatSetPrinter),
        (rf'^{prefix}flat_set<',                           FlatSetPrinter),
        (rf'^{prefix}flat_multiset<',                      FlatSetPrinter),

        # flat_map_impl / flat_map / flat_multimap
        (rf'^{prefix}flat_map_impl<',                      FlatMapPrinter),
        (rf'^{prefix}flat_map<',                           FlatMapPrinter),
        (rf'^{prefix}flat_multimap<',                      FlatMapPrinter),

        # paired_storage
        (rf'^{prefix}detail::paired_storage<',             PairedStoragePrinter),

        # bptree
        (rf'^{prefix}bptree_base$',                        BPTreePrinter),
        (rf'^{prefix}bp_tree_impl<',                       BPTreePrinter),

        # pass_in_reg / pass_rv_in_reg
        (rf'^{prefix}pass_in_reg<',                        PassInRegPrinter),
        (rf'^{prefix}pass_rv_in_reg<',                     PassInRegPrinter),
    ]
    return [(re.compile(p), cls) for p, cls in patterns]


_lookup_table = _build_lookup()


def psi_vm_lookup(val):
    """GDB pretty-printer lookup function for psi::vm types."""
    tag = get_type_tag(val)
    for regex, printer_cls in _lookup_table:
        if regex.search(tag):
            return printer_cls(val)
    return None


# Register as a global pretty-printer
gdb.pretty_printers.append(psi_vm_lookup)
print("psi::vm GDB pretty-printers loaded.")
