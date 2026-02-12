"""
LLDB debugger formatters for psi::vm containers.

Integration:
  Option A: Add to ~/.lldbinit for automatic loading:
    command script import /path/to/psi_vm_lldb.py

  Option B: Load manually in an LLDB session:
    (lldb) command script import /path/to/psi_vm_lldb.py

  Option C: For VSCode with CodeLLDB extension, add to settings.json:
    "lldb.launch.initCommands": [
        "command script import /path/to/psi_vm_lldb.py"
    ]
"""

import lldb


# ==============================================================================
# Helpers
# ==============================================================================

def get_child_by_name(valobj, name):
    """Get a child member, searching through base classes if needed."""
    child = valobj.GetChildMemberWithName(name)
    if child.IsValid():
        return child
    # Search base classes
    for i in range(valobj.GetNumChildren()):
        c = valobj.GetChildAtIndex(i)
        if c.GetName() == name:
            return c
        # Recurse into unnamed base classes
        if c.GetName() is None or c.GetName().startswith("psi::vm::"):
            found = get_child_by_name(c, name)
            if found.IsValid():
                return found
    return valobj.CreateValueFromExpression("invalid", "0")


def get_vector_data_and_size(valobj):
    """Extract (data_ptr, size) from a std::vector, tr_vector, or fc_vector."""
    type_name = valobj.GetType().GetUnqualifiedType().GetName()

    if "tr_vector" in type_name:
        p = valobj.GetChildMemberWithName("p_array_")
        s = valobj.GetChildMemberWithName("size_")
        return p, s.GetValueAsUnsigned(0)

    if "fc_vector" in type_name:
        s = valobj.GetChildMemberWithName("size_")
        arr = valobj.GetChildMemberWithName("array_")
        data = arr.GetChildMemberWithName("data") if arr.IsValid() else valobj
        return data.AddressOf(), s.GetValueAsUnsigned(0)

    # Assume libc++ std::vector layout: __begin_, __end_, __end_cap_
    begin = valobj.GetChildMemberWithName("__begin_")
    end = valobj.GetChildMemberWithName("__end_")
    if begin.IsValid() and end.IsValid():
        begin_val = begin.GetValueAsUnsigned(0)
        end_val = end.GetValueAsUnsigned(0)
        elem_size = begin.GetType().GetPointeeType().GetByteSize()
        size = (end_val - begin_val) // elem_size if elem_size > 0 else 0
        return begin, size

    # MSVC STL layout (fallback, for cppvsdbg users shouldn't reach here)
    # _Mypair._Myval2._Myfirst / _Mylast
    pair = valobj.GetChildMemberWithName("_Mypair")
    if pair.IsValid():
        val2 = pair.GetChildMemberWithName("_Myval2")
        if val2.IsValid():
            first = val2.GetChildMemberWithName("_Myfirst")
            last = val2.GetChildMemberWithName("_Mylast")
            if first.IsValid() and last.IsValid():
                f = first.GetValueAsUnsigned(0)
                l = last.GetValueAsUnsigned(0)
                elem_size = first.GetType().GetPointeeType().GetByteSize()
                size = (l - f) // elem_size if elem_size > 0 else 0
                return first, size

    # libstdc++ layout: _M_impl._M_start / _M_finish
    impl = valobj.GetChildMemberWithName("_M_impl")
    if impl.IsValid():
        start = impl.GetChildMemberWithName("_M_start")
        finish = impl.GetChildMemberWithName("_M_finish")
        if start.IsValid() and finish.IsValid():
            s = start.GetValueAsUnsigned(0)
            f = finish.GetValueAsUnsigned(0)
            elem_size = start.GetType().GetPointeeType().GetByteSize()
            size = (f - s) // elem_size if elem_size > 0 else 0
            return start, size

    return None, 0


# ==============================================================================
# tr_vector
# ==============================================================================

class TrVectorSynthProvider:
    def __init__(self, valobj, internal_dict):
        self.valobj = valobj

    def update(self):
        self.p_array = self.valobj.GetChildMemberWithName("p_array_")
        self.size = self.valobj.GetChildMemberWithName("size_").GetValueAsUnsigned(0)

    def num_children(self):
        return self.size

    def get_child_index(self, name):
        try:
            return int(name.lstrip("[").rstrip("]"))
        except ValueError:
            return -1

    def get_child_at_index(self, index):
        if index < 0 or index >= self.size:
            return None
        elem_type = self.p_array.GetType().GetPointeeType()
        offset = index * elem_type.GetByteSize()
        return self.p_array.CreateChildAtOffset(f"[{index}]", offset, elem_type)


def tr_vector_summary(valobj, internal_dict):
    size = valobj.GetChildMemberWithName("size_").GetValueAsUnsigned(0)
    return f"size={size}"


# ==============================================================================
# fc_vector
# ==============================================================================

class FcVectorSynthProvider:
    def __init__(self, valobj, internal_dict):
        self.valobj = valobj

    def update(self):
        self.size = self.valobj.GetChildMemberWithName("size_").GetValueAsUnsigned(0)
        arr = self.valobj.GetChildMemberWithName("array_")
        self.data = arr.GetChildMemberWithName("data") if arr.IsValid() else None

    def num_children(self):
        return self.size

    def get_child_index(self, name):
        try:
            return int(name.lstrip("[").rstrip("]"))
        except ValueError:
            return -1

    def get_child_at_index(self, index):
        if index < 0 or index >= self.size or self.data is None:
            return None
        return self.data.GetChildAtIndex(index, lldb.eNoDynamicValues, True)


def fc_vector_summary(valobj, internal_dict):
    size = valobj.GetChildMemberWithName("size_").GetValueAsUnsigned(0)
    return f"size={size}"


# ==============================================================================
# flat_set_impl / flat_set / flat_multiset
# ==============================================================================

class FlatSetSynthProvider:
    def __init__(self, valobj, internal_dict):
        self.valobj = valobj

    def update(self):
        self.keys = get_child_by_name(self.valobj, "storage_")
        self.data_ptr, self.size = get_vector_data_and_size(self.keys)

    def num_children(self):
        return self.size

    def get_child_index(self, name):
        try:
            return int(name.lstrip("[").rstrip("]"))
        except ValueError:
            return -1

    def get_child_at_index(self, index):
        if index < 0 or index >= self.size or self.data_ptr is None:
            return None
        elem_type = self.data_ptr.GetType().GetPointeeType()
        offset = index * elem_type.GetByteSize()
        return self.data_ptr.CreateChildAtOffset(f"[{index}]", offset, elem_type)


def flat_set_summary(valobj, internal_dict):
    keys = get_child_by_name(valobj, "storage_")
    _, size = get_vector_data_and_size(keys)
    return f"size={size}"


# ==============================================================================
# detail::paired_storage (standalone struct with keys + values)
# ==============================================================================

class PairedStorageSynthProvider:
    """Shows paired_storage as a list of key-value pairs."""

    def __init__(self, valobj, internal_dict):
        self.valobj = valobj

    def update(self):
        self.keys_container = self.valobj.GetChildMemberWithName("keys")
        self.vals_container = self.valobj.GetChildMemberWithName("values")
        self.keys_ptr, self.size = get_vector_data_and_size(self.keys_container)
        self.vals_ptr, _ = get_vector_data_and_size(self.vals_container)

    def num_children(self):
        return self.size

    def get_child_index(self, name):
        try:
            return int(name.lstrip("[").rstrip("]"))
        except ValueError:
            return -1

    def get_child_at_index(self, index):
        if index < 0 or index >= self.size:
            return None
        if self.keys_ptr is None or self.vals_ptr is None:
            return None
        key_type = self.keys_ptr.GetType().GetPointeeType()
        val_type = self.vals_ptr.GetType().GetPointeeType()
        key_offset = index * key_type.GetByteSize()
        val_offset = index * val_type.GetByteSize()
        key = self.keys_ptr.CreateChildAtOffset("key", key_offset, key_type)
        val = self.vals_ptr.CreateChildAtOffset("value", val_offset, val_type)
        key_summary = key.GetSummary() or key.GetValue() or str(index)
        return val.CreateValueFromExpression(f"[{key_summary}]", val.GetValue() or "")


def paired_storage_summary(valobj, internal_dict):
    keys = valobj.GetChildMemberWithName("keys")
    _, size = get_vector_data_and_size(keys)
    return f"size={size}"


# ==============================================================================
# flat_map_impl / flat_map / flat_multimap
# ==============================================================================

class FlatMapSynthProvider:
    """Shows flat_map entries as [key] = value pairs."""

    def __init__(self, valobj, internal_dict):
        self.valobj = valobj

    def update(self):
        # paired_storage is flat_impl::storage_ (in base class) with public keys/values
        storage = get_child_by_name(self.valobj, "storage_")
        self.storage_container = storage.GetChildMemberWithName("keys") if storage.IsValid() else get_child_by_name(self.valobj, "keys")
        self.vals_container = storage.GetChildMemberWithName("values") if storage.IsValid() else get_child_by_name(self.valobj, "values")
        self.storage_ptr, self.size = get_vector_data_and_size(self.storage_container)
        self.vals_ptr, _ = get_vector_data_and_size(self.vals_container)

    def num_children(self):
        return self.size

    def get_child_index(self, name):
        try:
            return int(name.lstrip("[").rstrip("]"))
        except ValueError:
            return -1

    def get_child_at_index(self, index):
        if index < 0 or index >= self.size:
            return None
        if self.storage_ptr is None or self.vals_ptr is None:
            return None

        key_type = self.storage_ptr.GetType().GetPointeeType()
        val_type = self.vals_ptr.GetType().GetPointeeType()

        key_offset = index * key_type.GetByteSize()
        val_offset = index * val_type.GetByteSize()

        key = self.storage_ptr.CreateChildAtOffset("key", key_offset, key_type)
        val = self.vals_ptr.CreateChildAtOffset("value", val_offset, val_type)

        # Try to format as "[key] = value"
        key_summary = key.GetSummary() or key.GetValue() or str(index)
        return val.CreateValueFromExpression(f"[{key_summary}]", val.GetValue() or "")


def flat_map_summary(valobj, internal_dict):
    storage = get_child_by_name(valobj, "storage_")
    keys = storage.GetChildMemberWithName("keys") if storage.IsValid() else get_child_by_name(valobj, "keys")
    _, size = get_vector_data_and_size(keys)
    return f"size={size}"


# ==============================================================================
# bptree_base / bp_tree_impl
# ==============================================================================

def bptree_summary(valobj, internal_dict):
    p_hdr = get_child_by_name(valobj, "p_hdr_")
    if p_hdr.IsValid():
        ptr = p_hdr.GetChildMemberWithName("ptr")
        if ptr.IsValid() and ptr.GetValueAsUnsigned(0) != 0:
            hdr = ptr.Dereference()
            size = hdr.GetChildMemberWithName("size_").GetValueAsUnsigned(0)
            depth = hdr.GetChildMemberWithName("depth_").GetValueAsUnsigned(0)
            return f"size={size}, depth={depth}"
    return "empty / uninitialized"


# ==============================================================================
# pass_in_reg / pass_rv_in_reg
# ==============================================================================

class PassInRegSynthProvider:
    def __init__(self, valobj, internal_dict):
        self.valobj = valobj

    def update(self):
        self.value = self.valobj.GetChildMemberWithName("value")

    def num_children(self):
        return self.value.GetNumChildren() if self.value.IsValid() else 0

    def get_child_index(self, name):
        return self.value.GetIndexOfChildWithName(name) if self.value.IsValid() else -1

    def get_child_at_index(self, index):
        return self.value.GetChildAtIndex(index) if self.value.IsValid() else None


def pass_in_reg_summary(valobj, internal_dict):
    value = valobj.GetChildMemberWithName("value")
    if value.IsValid():
        s = value.GetSummary() or value.GetValue()
        return s if s else ""
    return ""


# ==============================================================================
# Registration
# ==============================================================================

def __lldb_init_module(debugger, internal_dict):
    prefix = "psi::vm::"

    # tr_vector
    debugger.HandleCommand(f'type summary add -F psi_vm_lldb.tr_vector_summary -x "^{prefix}tr_vector<" -w psi_vm')
    debugger.HandleCommand(f'type synthetic add -l psi_vm_lldb.TrVectorSynthProvider -x "^{prefix}tr_vector<" -w psi_vm')

    # fc_vector
    debugger.HandleCommand(f'type summary add -F psi_vm_lldb.fc_vector_summary -x "^{prefix}fc_vector<" -w psi_vm')
    debugger.HandleCommand(f'type synthetic add -l psi_vm_lldb.FcVectorSynthProvider -x "^{prefix}fc_vector<" -w psi_vm')

    # flat_set_impl (covers flat_set and flat_multiset via inheritance)
    debugger.HandleCommand(f'type summary add -F psi_vm_lldb.flat_set_summary -x "^{prefix}flat_set_impl<" -w psi_vm')
    debugger.HandleCommand(f'type synthetic add -l psi_vm_lldb.FlatSetSynthProvider -x "^{prefix}flat_set_impl<" -w psi_vm')
    debugger.HandleCommand(f'type summary add -F psi_vm_lldb.flat_set_summary -x "^{prefix}flat_set<" -w psi_vm')
    debugger.HandleCommand(f'type synthetic add -l psi_vm_lldb.FlatSetSynthProvider -x "^{prefix}flat_set<" -w psi_vm')
    debugger.HandleCommand(f'type summary add -F psi_vm_lldb.flat_set_summary -x "^{prefix}flat_multiset<" -w psi_vm')
    debugger.HandleCommand(f'type synthetic add -l psi_vm_lldb.FlatSetSynthProvider -x "^{prefix}flat_multiset<" -w psi_vm')

    # paired_storage
    debugger.HandleCommand(f'type summary add -F psi_vm_lldb.paired_storage_summary -x "^{prefix}detail::paired_storage<" -w psi_vm')
    debugger.HandleCommand(f'type synthetic add -l psi_vm_lldb.PairedStorageSynthProvider -x "^{prefix}detail::paired_storage<" -w psi_vm')

    # flat_map_impl (covers flat_map and flat_multimap)
    debugger.HandleCommand(f'type summary add -F psi_vm_lldb.flat_map_summary -x "^{prefix}flat_map_impl<" -w psi_vm')
    debugger.HandleCommand(f'type synthetic add -l psi_vm_lldb.FlatMapSynthProvider -x "^{prefix}flat_map_impl<" -w psi_vm')
    debugger.HandleCommand(f'type summary add -F psi_vm_lldb.flat_map_summary -x "^{prefix}flat_map<" -w psi_vm')
    debugger.HandleCommand(f'type synthetic add -l psi_vm_lldb.FlatMapSynthProvider -x "^{prefix}flat_map<" -w psi_vm')
    debugger.HandleCommand(f'type summary add -F psi_vm_lldb.flat_map_summary -x "^{prefix}flat_multimap<" -w psi_vm')
    debugger.HandleCommand(f'type synthetic add -l psi_vm_lldb.FlatMapSynthProvider -x "^{prefix}flat_multimap<" -w psi_vm')

    # bptree
    debugger.HandleCommand(f'type summary add -F psi_vm_lldb.bptree_summary -x "^{prefix}bptree_base$" -w psi_vm')
    debugger.HandleCommand(f'type summary add -F psi_vm_lldb.bptree_summary -x "^{prefix}bp_tree_impl<" -w psi_vm')

    # pass_in_reg / pass_rv_in_reg
    debugger.HandleCommand(f'type summary add -F psi_vm_lldb.pass_in_reg_summary -x "^{prefix}pass_in_reg<" -w psi_vm')
    debugger.HandleCommand(f'type synthetic add -l psi_vm_lldb.PassInRegSynthProvider -x "^{prefix}pass_in_reg<" -w psi_vm')
    debugger.HandleCommand(f'type summary add -F psi_vm_lldb.pass_in_reg_summary -x "^{prefix}pass_rv_in_reg<" -w psi_vm')
    debugger.HandleCommand(f'type synthetic add -l psi_vm_lldb.PassInRegSynthProvider -x "^{prefix}pass_rv_in_reg<" -w psi_vm')

    # Enable the category
    debugger.HandleCommand('type category enable psi_vm')

    print("psi::vm debugger formatters loaded.")
