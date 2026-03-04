# C++23 Flat Container Implementations: A Comparative Analysis

**psi::vm vs libc++ (LLVM 22) vs Microsoft STL**

*February 2026*

---

## Table of Contents

1. [Overview](#1-overview)
2. [Class Hierarchy & Architecture](#2-class-hierarchy--architecture)
3. [Template Bloat & Instantiation Analysis](#3-template-bloat--instantiation-analysis)
4. [Compile-time Impact](#4-compile-time-impact)
5. [Algorithmic Complexities](#5-algorithmic-complexities)
6. [Codegen Quality & Code Size Minimization](#6-codegen-quality--code-size-minimization)
7. [General Efficiency & Runtime Performance](#7-general-efficiency--runtime-performance)
8. [General Complexity (LOC & Metrics)](#8-general-complexity-loc--metrics)
9. [Standard Compliance](#9-standard-compliance)
10. [Extensions Beyond the Standard](#10-extensions-beyond-the-standard)
11. [Summary & Recommendations](#11-summary--recommendations)

---

## 1. Overview

All three implementations target the C++23 flat associative container adaptors specified in P0429R9 (`<flat_map>`) and P1222R4 (`<flat_set>`). These are sorted containers backed by contiguous-memory sequence containers (defaulting to `std::vector`) rather than node-based trees.

| Implementation | Vendor | Version | Standards | Language Baseline |
|---|---|---|---|---|
| **psi::vm** | Domagoj Saric | Current (2026-02) | C++23 | C++23 (deducing this) |
| **libc++** | LLVM Project | LLVM 22 | C++23, constexpr C++26 | C++23 |
| **MS STL** | Microsoft | VS 2022 17.x | C++23 | C++23 |

**Development effort context:**
- **MS STL**: ~2.5 years (Jun 2023 -- Feb 2026), 269 commits, 85 nontrivial PRs, 11+ contributors. Merged via [PR #6071](https://github.com/microsoft/STL/pull/6071).
- **libc++**: Developed independently, shipping since LLVM 19+.
- **psi::vm**: Single-author library, designed for a specific high-performance application (RAMA data warehouse), with custom container backends (`vm_vector`, `heap_vector`, `fc_vector`).

---

## 2. Class Hierarchy & Architecture

### 2.1 psi::vm — Inheritance-based (flat_set) / Monolithic bool (flat_map)

**flat_set family** (refactored design):
```
flat_set_impl<Key, Compare, KC>           (shared base, NO IsUnique parameter)
  ├── flat_set<Key, Compare, KC>          (unique: public inheritance)
  └── flat_multiset<Key, Compare, KC>     (multi: public inheritance)
```

**flat_map family** (original design, not yet refactored):
```
flat_map_impl<bool IsUnique, Key, T, Compare, KC, MC>    (monolithic template)
  ├── flat_map      = flat_map_impl<true, ...>            (type alias)
  └── flat_multimap = flat_map_impl<false, ...>           (type alias)
```

**Shared utilities** (flat_common.hpp):
- `detail::paired_storage<KC, MC>` — synchronized dual-container operations (flat_map base)
- `detail::lower_bound_idx`, `upper_bound_idx`, `key_eq_at` — lookup helpers
- `detail::sort_and_dedup<Unique>`, `sort_merge_dedup<Unique, WasSorted>` — sort/merge utilities
- `sorted_unique_t`, `sorted_equivalent_t` — tag types

**Key design decisions:**
- `flat_set_impl` uses **public inheritance + protected constructors**: derived classes call protected `sort_and_dedup<Unique>()` and `sort_merge<Unique, WasSorted>()` with the appropriate bool.
- `Compare` stored via **EBO (private inheritance)** — zero overhead for stateless comparators.
- Iterator is **index-based** (`key_type const *` base pointer + `size_type` index).
- `flat_set_impl::count()` and `erase(key)` are generic (`upper_bound - lower_bound` and `equal_range + erase`), working correctly for both unique and multi.
- Template friend declarations allow derived classes to access base internals of other instances (needed for `merge()`).

### 2.2 libc++ — Fully Separated Classes

```
flat_set<Key, Compare, KC>         (standalone class)
flat_multiset<Key, Compare, KC>    (standalone class, NO shared base)
flat_map<Key, T, Compare, KC, MC>  (standalone class)
flat_multimap<Key, T, Compare, KC, MC>  (standalone class, NO shared base)
```

**Shared utilities** (as befriendable structs, not base classes):
- `__flat_set_utils` — `__emplace_exact_pos()`, `__append()` (set family)
- `__flat_map_utils` — `__emplace_exact_pos()`, `__append()` (map family)

**Key design decisions:**
- **No shared base class at all**. Each container is a complete, self-contained class template.
- Code sharing happens through `__flat_set_utils` / `__flat_map_utils` — static method structs that are `friend`ed into the container classes. This avoids the ABI complications of base classes while allowing some shared logic.
- `_Compare` stored via `_LIBCPP_NO_UNIQUE_ADDRESS` attribute (functionally equivalent to EBO but using [[no_unique_address]]).
- Each class independently defines all constructors, insert/emplace, lookup, erase, etc.
- Significant code duplication between `flat_set` and `flat_multiset` (~1750 lines combined, mostly parallel).

### 2.3 MS STL — Shared Base with `bool _IsUnique`

```
_Flat_set_base<bool _IsUnique, Key, Compare, Container>
  ├── flat_set      = _Flat_set_base<true, ...>   (type alias)
  └── flat_multiset = _Flat_set_base<false, ...>  (type alias)

_Flat_map_base<bool _IsUnique, Key, Mapped, Compare, KC, MC>
  ├── flat_map      = _Flat_map_base<true, ...>   (type alias)
  └── flat_multimap = _Flat_map_base<false, ...>  (type alias)
```

**Key design decisions:**
- **Single monolithic class per family** with `_IsUnique` bool parameter. `flat_set`/`flat_multiset` are type aliases, not derived classes.
- `if constexpr (_IsUnique)` branching throughout the class body differentiates behavior.
- Separate `_Flat_set_base` and `_Flat_map_base` (not unified between set and map).
- Comparators stored as members (not EBO — MSVC relies on `[[msvc::no_unique_address]]` internally for similar effect).
- Filed issue [#6069](https://github.com/microsoft/STL/issues/6069) acknowledging "too much special-casing of `_IsUnique`" and that emplace_hint implementations diverged between flat_map and flat_set.

### 2.4 Hierarchy Comparison Table

| Aspect | psi::vm flat_set | psi::vm flat_map | libc++ | MS STL |
|---|---|---|---|---|
| **Sharing mechanism** | Inheritance (base class) | Monolithic template | Befriendable utility structs | Monolithic template |
| **IsUnique parameter** | Not in base; derived classes select | `bool` template param | Implicit (separate classes) | `bool` template param |
| **Concrete types** | Distinct classes | Type aliases | Distinct classes | Type aliases |
| **Code duplication** | Minimal (base has ~60% of code) | None | Significant (~90% parallel) | None |
| **ABI implications** | Base class in mangled name | Single template | Clean (no base) | Single template |

### 2.5 Analysis

**psi::vm flat_set** achieves the best balance: the base class contains all `IsUnique`-independent logic (iterators, lookup, capacity, erase, swap, comparison, extract/replace), while derived classes add only uniqueness-dependent operations (emplace, insert, merge, constructors). The base has **no `IsUnique` template parameter**, meaning iterator types, lookup code, and capacity operations are shared at the binary level — `flat_set<int>::iterator` and `flat_multiset<int>::iterator` are the same type.

**libc++** avoids the hierarchy entirely, choosing simplicity and ABI independence at the cost of code duplication. Each class is self-contained, which makes each one easy to understand in isolation but requires maintaining parallel implementations. The `__flat_set_utils` pattern is a pragmatic middle ground — it factors out the most complex shared logic (emplace with exception guards, bulk append) without creating a type relationship.

**MS STL** takes the maximally-sharing approach with `_IsUnique` as a template parameter. This eliminates duplication but means every instantiation carries the full class body, with dead code eliminated only by `if constexpr`. The acknowledged issue of "too much special-casing" suggests this approach has reached its ergonomic limits — the team themselves recognize the need to factor things out differently.

---

## 3. Template Bloat & Instantiation Analysis

### 3.1 Template Parameters per Instantiation

| Container | psi::vm | libc++ | MS STL |
|---|---|---|---|
| **flat_set** | `<Key, Compare, KC>` (3 params) | `<Key, Compare, KC>` (3 params) | `<true, Key, Compare, KC>` (4 params) |
| **flat_multiset** | `<Key, Compare, KC>` (3 params) | `<Key, Compare, KC>` (3 params) | `<false, Key, Compare, KC>` (4 params) |
| **flat_set_impl** (base) | `<Key, Compare, KC>` (3 params) | N/A | N/A |
| **flat_map** | `<true, K, T, Comp, KC, MC>` (6) | `<K, T, Comp, KC, MC>` (5) | `<true, K, T, Comp, KC, MC>` (6) |
| **flat_multimap** | `<false, K, T, Comp, KC, MC>` (6) | `<K, T, Comp, KC, MC>` (5) | `<false, K, T, Comp, KC, MC>` (6) |

### 3.2 Shared Code at Binary Level

**psi::vm flat_set**: `flat_set<int>` and `flat_multiset<int>` share the same `flat_set_impl<int, std::less<int>, std::vector<int>>` base instantiation. Only the thin derived class methods (emplace, insert, merge, constructors) are duplicated. All lookup, iteration, erase, swap, comparison, capacity operations exist once in the binary.

**psi::vm flat_map**: `flat_map<int, double>` and `flat_multimap<int, double>` are different instantiations of `flat_map_impl` with `IsUnique=true` vs `false`. The compiler can fold identical functions via ICF (Identical Code Folding), but this is not guaranteed.

**libc++**: `flat_set<int>` and `flat_multiset<int>` are completely independent types. All methods are duplicated at the template level. The compiler *may* fold identical code (e.g., `lower_bound` implementations) via ICF, but it depends on the linker.

**MS STL**: Same situation as psi::vm flat_map — `_Flat_set_base<true, int, ...>` and `_Flat_set_base<false, int, ...>` are separate instantiations. `if constexpr` eliminates the dead `_IsUnique` branches, but the shared code (lookup, iteration, etc.) is still instantiated twice. The linker's ICF pass must merge them.

### 3.3 Iterator Type Proliferation

| Implementation | flat_set iterator | flat_map iterator | Iterator sharing |
|---|---|---|---|
| **psi::vm** | `flat_set_impl<K,C,KC>::iterator_impl` | `flat_map_impl<U,K,T,C,KC,MC>::iterator_impl<IsConst>` | flat_set/multiset share iterator type |
| **libc++** | `__ra_iterator<KC>` | `__key_value_iterator<KI, MI>` | flat_set/multiset share `__ra_iterator` |
| **MS STL** | `Container::const_iterator` (no wrapper) | `_Pairing_iterator_provider::_Iterator` | N/A (uses underlying container's iterator) |

MS STL wins here for flat_set — it adds zero iterator overhead by directly using the underlying container's `const_iterator`. libc++ wraps in a thin `__ra_iterator`. psi::vm uses a custom index-based iterator.

---

## 4. Compile-time Impact

### 4.1 Include Dependency Weight

| Implementation | flat_set includes | flat_map includes | Notes |
|---|---|---|---|
| **psi::vm** | 5 headers (flat_common, abi, algorithm, ranges, boost/assert) | 6 headers (+ flat_common) | Minimal; assumes `<algorithm>` and `<ranges>` are already available |
| **libc++** | ~35 granular internal headers | ~40 granular internal headers | Each header is small but many includes; designed for modular builds |
| **MS STL** | ~10 headers (yvals_core, algorithm, compare, concepts, initializer_list, type_traits, utility, vector, xmemory, xutility) | Same set | Coarser-grained; each include pulls in more |

**psi::vm** has the lightest include footprint by far. It relies on just `<algorithm>`, `<ranges>`, `<type_traits>`, `<utility>`, and its own `flat_common.hpp` + `abi.hpp`. It doesn't pull in `<vector>` (the user chooses the container type).

**libc++** uses extremely fine-grained internal headers (e.g., `<__algorithm/lower_bound.h>` rather than `<algorithm>`), which in theory allows for precise dependency tracking and parallel compilation, but in practice means ~35-40 `#include` directives per container header.

**MS STL** falls between the two, with medium-grained includes. Issue [#3599](https://github.com/microsoft/STL/issues/3599) notes that `/std:c++latest` headers can be up to 10x slower to include.

### 4.2 Template Instantiation Depth

| Aspect | psi::vm | libc++ | MS STL |
|---|---|---|---|
| **Deduction guides** | 7 per concrete type (flat_set), 7 per (flat_multiset) = 14 total for set family | ~18 per class (flat_set), ~18 per class (flat_multiset) = ~36 for set family | ~20+ per concrete type (deduced via alias CTAD) |
| **SFINAE / concepts** | Moderate (requires clauses on transparent lookup) | Heavy (concepts, requires clauses, container_traits) | Heavy (concepts, requires, container_traits) |
| **Allocator overloads** | **None** | Full allocator-extended constructors (~10+ extra overloads) | Full allocator-extended constructors |

psi::vm omits allocator-extended constructors entirely, which dramatically reduces the number of constructor overloads and associated SFINAE complexity. The standard requires these for use with `std::pmr` containers, but psi::vm targets its own custom containers where allocator propagation is irrelevant.

---

## 5. Algorithmic Complexities

All three implementations share the same fundamental algorithmic strategy for bulk operations:

| Operation | Complexity | Algorithm |
|---|---|---|
| **Lookup** (find, lower_bound, etc.) | O(log n) | Binary search on sorted keys |
| **Single insert** (unique) | O(n) | Binary search + vector insert |
| **Single insert** (multi) | O(n) | Binary/upper search + vector insert |
| **Bulk insert** (k elements into n) | O((n+k) log(n+k)) | Append + sort tail + inplace_merge + unique |
| **Erase by key** | O(n) | Find + vector erase |
| **Erase by iterator** | O(n) | Vector erase (element shift) |
| **Construct from sorted** | O(n) | Copy + validate |
| **Construct from unsorted** | O(n log n) | Copy + sort + unique |

### 5.1 Optimization Differences

**MS STL: `_Establish_invariants` with `is_sorted_until`**
```
On construction: detect how much is already sorted via is_sorted_until(),
then only sort the unsorted tail before merging.
```
This means constructing from mostly-sorted data is faster: if the first 90% is sorted, only the last 10% gets sorted before merge. Complexity: O(n + k log k) where k = unsorted tail length.

**psi::vm: `sort_merge_dedup<Unique, WasSorted>`**
```
Template parameter WasSorted skips the sort step entirely for pre-sorted input.
Template parameter Unique skips the unique step for multi containers.
```
Binary-level optimization: when called with `WasSorted=true`, the sort code is completely eliminated by the compiler (not just short-circuited at runtime).

**libc++: `__append_sort_merge_unique`**
```
Same append-sort-merge-unique strategy.
Template parameter _WasSorted controls whether the appended range is sorted.
```
Functionally equivalent to psi::vm's approach.

### 5.2 Comparator Wrapping

**psi::vm**: Wraps comparators with `make_trivially_copyable_predicate()` before passing to `std::lower_bound`/`std::upper_bound`. This ensures the comparator is passed by value (not by reference) to the algorithm, enabling better inlining and avoiding indirect calls through references.

**MS STL**: Uses `_Pass_fn()` wrapping (PR #5953) for `ranges::sort`. This prevents issues with non-trivially-copyable comparators being passed through range algorithms.

**libc++**: Uses comparators directly; relies on the implementation's own algorithm library to handle comparator forwarding efficiently.

---

## 6. Codegen Quality & Code Size Minimization

### 6.1 Iterator Implementation Strategy

**psi::vm** (index-based):
```cpp
struct iterator_impl {
    key_type const * base_;
    size_type        idx_;
};
```
- Size: 2 pointers (pointer + index, where index is typically `uint32_t` or `uint16_t` depending on the container)
- Dereference: `base_[idx_]` — single indexed load
- Increment: `++idx_` — single integer increment
- Difference: `idx_ - other.idx_` — single subtraction
- **Advantage**: When the underlying `size_type` is smaller than a pointer (e.g., `uint16_t` for `fc_vector` or `uint32_t` for `heap_vector`), the iterator is smaller than two pointers. Fits in a register pair.

**libc++ flat_set** (`__ra_iterator`):
```cpp
template <class _Container>
class __ra_iterator {
    using _Iterator = typename _Container::const_iterator;
    _Iterator __iter_;
};
```
- Size: whatever the underlying iterator is (typically a pointer for `std::vector`)
- Dereference: `*__iter_` — single pointer dereference
- Zero overhead wrapper; compiles down to the underlying iterator's operations.

**MS STL flat_set**:
```cpp
using iterator = container_type::const_iterator;
```
- **Zero wrapper** — directly uses the container's `const_iterator`.
- Smallest possible binary footprint for the iterator type.
- No additional template instantiation for an iterator wrapper.

**libc++ flat_map** (`__key_value_iterator`):
```cpp
class __key_value_iterator {
    __key_iterator   __key_iter_;
    __mapped_iterator __mapped_iter_;
};
```
- Size: 2 pointers (key iterator + mapped iterator)
- Dereference: constructs a `pair<const key_ref, mapped_ref>` proxy
- Increment: advances **both** iterators
- **`iterator_category = input_iterator_tag`** (degraded) because the proxy reference doesn't satisfy legacy RandomAccessIterator. `iterator_concept = random_access_iterator_tag` (correct for C++20).

**MS STL flat_map** (`_Pairing_iterator_provider::_Iterator`):
- Similar dual-iterator design to libc++
- Full MSVC iterator debugging infrastructure (`_Unwrapped()`, `_Seek_to()`, `_Verify_range()`, `_Compat()`)
- `_Arrow_proxy` class for `operator->`
- Debug builds: consistency checks that both iterators maintain equal distances

**psi::vm flat_map** (`flat_map_impl::iterator_impl<IsConst>`):
- Stores reference to `paired_storage` + index
- Dereference: `pair_ref{ keys[idx], values[idx] }` (proxy pair)
- Arrow: `arrow_proxy` that stores constructed pair
- Single index increment (both containers addressed by same index)
- **Advantage**: Only one index needs to be stored/compared/incremented (not two iterators)

### 6.2 EBO and Stateless Comparator Overhead

All three implementations ensure zero overhead for stateless comparators (`std::less<>`, etc.):
- **psi::vm**: Private inheritance from Compare
- **libc++**: `_LIBCPP_NO_UNIQUE_ADDRESS` attribute on comparator member
- **MS STL**: `[[msvc::no_unique_address]]` (or equivalent internal mechanism)

### 6.3 Binary Size Considerations

**psi::vm** produces the least code duplication for the flat_set family because `flat_set<int>` and `flat_multiset<int>` share the same `flat_set_impl<int, ...>` base instantiation. Only the emplace/insert/merge methods are duplicated.

**libc++** and **MS STL** both generate separate instantiations for unique vs multi variants. The linker's ICF (Identical Code Folding) pass can merge functions with identical machine code, but this is:
- Not guaranteed (depends on linker settings)
- Not available on all platforms (e.g., older linkers)
- Only works for *identical* code (not just *equivalent* code)

---

## 7. General Efficiency & Runtime Performance

### 7.1 Exception Safety Strategy

| Implementation | Strategy | Guard mechanism | Cost |
|---|---|---|---|
| **psi::vm** | `try { ... } catch { rollback; throw; }` | Manual try/catch in `paired_storage` | Zero overhead on the happy path (no guard object construction) |
| **libc++** | `__make_exception_guard` + `__complete()` | RAII scope guard with lambda | Guard object constructed on every insert; `__complete()` dismisses |
| **MS STL** | `_Clear_guard` / swap guards | RAII scope guard with pointer | Guard object constructed on every mutation; `_Target = nullptr` dismisses |

**psi::vm** avoids guard objects entirely for single-element operations (flat_set emplace is a single `keys_.insert()` call — no dual-container synchronization needed). For flat_map, `paired_storage::insert_element_at()` uses a manual try/catch which has zero cost when no exception is thrown.

**MS STL** specializes its swap guard on `noexcept`:
```cpp
template <class _Container>
struct _Flat_set_swap_clear_guard</*_IsNoexcept=*/true, _Container> {
    constexpr explicit _Flat_set_swap_clear_guard(...) noexcept {}
    void _Dismiss() noexcept {}
    // Empty — compiles away entirely
};
```
This is smart: when the underlying container's swap is noexcept (the common case for `std::vector`), the guard is zero-cost.

### 7.2 Deduplication Optimization

**MS STL** (PR #6024): For "usual types" with standard comparators, uses **equality** (`==`) instead of **equivalence** (`!(a < b) && !(b < a)`) for deduplication. This enables SIMD vectorization for integer types and is cheaper for strings.

Benchmark results from the PR:
- `flat_map_strings`: 773ns → 639ns (-17%)
- `flat_set_strings`: 450ns → 358ns (-20%)

**psi::vm** and **libc++** use equivalence-based deduplication (`key_equiv(comp)` = `!comp(a,b) && !comp(b,a)`), which requires two comparisons per element pair. This is correct for all comparators but suboptimal for common cases.

### 7.3 Lookup Performance

All three use `std::lower_bound` / `std::upper_bound` on the keys container, giving O(log n) binary search. The key difference is in comparator forwarding:

**psi::vm**: `make_trivially_copyable_predicate(comp)` wraps the comparator to ensure it's passed by value, enabling better inlining when the comparator is a function object.

**MS STL**: `_Pass_fn(comp)` serves a similar purpose.

**libc++**: Direct comparator use; relies on internal algorithm implementation quality.

---

## 8. General Complexity (LOC & Metrics)

### 8.1 Raw Line Counts

| File | psi::vm | libc++ | MS STL |
|---|---|---|---|
| **flat_set + flat_multiset header** | 875 | 1,752 (892 + 860) | ~1,142 |
| **flat_map + flat_multimap header** | 774 | 2,379 (1,289 + 1,090) | ~1,700 (est.) |
| **Shared utilities** | 274 (flat_common.hpp) | 422 (utils.h × 2 + ra_iterator + key_value_iterator + sorted_*.h × 2) | Inline in headers |
| **Top-level forwarding headers** | N/A | 1,364 (flat_map: 744, flat_set: 620) — mostly synopsis | N/A |
| **TOTAL (all flat containers)** | **1,923** | **5,917** | **~2,842** |

### 8.2 Code Duplication Analysis

| Metric | psi::vm | libc++ | MS STL |
|---|---|---|---|
| **flat_set vs flat_multiset duplication** | ~15% (only emplace/insert/merge/ctors) | ~90% (nearly complete copy) | ~5% (if constexpr branches only) |
| **flat_set family vs flat_map family sharing** | `flat_common.hpp` (274 lines shared) | `sorted_unique.h`/`sorted_equivalent.h` (31 lines each) | None (independent headers) |
| **Deduction guides** | 14 (set family) + 10 (map family) | ~36 (set family) + ~36 (map family) | ~20+ per family |
| **Constructor overloads** | ~8 per concrete type | ~15+ per class (with allocator-extended) | ~15+ per alias (with allocator-extended) |

### 8.3 Complexity per Feature

| Feature | psi::vm LOC | libc++ LOC (per class) | MS STL LOC (per family) |
|---|---|---|---|
| **Iterator (flat_set)** | ~50 (iterator_impl) | ~157 (ra_iterator.h) | 0 (uses container's iterator directly) |
| **Iterator (flat_map)** | ~80 (iterator_impl<IsConst>) | ~217 (key_value_iterator.h) | ~100+ (_Pairing_iterator_provider) |
| **Exception guards** | ~10 (try/catch) | ~80 (utils.h per family) | ~40 (_Clear_guard + swap guards) |
| **Lookup operations** | ~60 (in base) | ~80 (per class) | ~60 (in _Base) |
| **Emplace/insert** | ~100 (per derived) | ~150 (per class) | ~120 (in _Base with if constexpr) |
| **Deduction guides** | ~70 (per family) | ~200+ (per class pair) | ~150+ (per family) |

### 8.4 Complexity Ratio

Normalizing to psi::vm = 1.0x:

| Metric | psi::vm | libc++ | MS STL |
|---|---|---|---|
| Total LOC | **1.0x** | **3.1x** | **1.5x** |
| Per-feature complexity | **1.0x** | **~1.5x** | **~1.2x** |

The 3.1x factor for libc++ is largely due to: (1) complete class duplication for unique/multi, (2) allocator-extended constructors, and (3) verbose top-level forwarding headers with full synopses.

---

## 9. Standard Compliance

### 9.1 Feature Compliance Matrix

| Feature | Standard | psi::vm | libc++ | MS STL |
|---|---|---|---|---|
| **All required constructors** | [flat.set]/[flat.map] | Partial (no allocator ctors) | Full | Full |
| **Allocator-extended constructors** | [flat.set.cons] | **No** | Yes | Yes |
| **sorted_unique / sorted_equivalent tags** | [flat.set.cons] | Yes | Yes | Yes |
| **from_range_t constructors** | [flat.set.cons] | Yes | Yes | Yes |
| **Deduction guides** | [flat.set.cons] | Yes (on concrete types) | Yes | Yes |
| **Heterogeneous lookup** | [flat.set.overview]/p4 | Yes (transparent comparators) | Yes | Yes |
| **extract() / replace()** | [flat.set.modifiers] | Yes | Yes | Yes |
| **merge()** | [flat.set.modifiers] | Yes | Yes | Yes |
| **erase_if (non-member)** | [flat.set.erasure] | Yes (hidden friend) | Yes | Yes |
| **operator<=>** | [flat.set.overview] | Yes | Yes | Yes |
| **swap (non-member)** | [flat.set.overview] | Yes (hidden friend) | Yes | Yes |
| **uses_allocator specialization** | [flat.set.overview] | **No** | Yes | Yes |
| **constexpr (C++26)** | P3372R3 | **No** | Partial (`_LIBCPP_CONSTEXPR_SINCE_CXX26`) | **No** |

### 9.2 Compliance Gaps in psi::vm

**Missing allocator support**: psi::vm omits all `uses_allocator` integration and allocator-extended constructors. This is intentional — the library targets custom container types (`vm_vector`, `heap_vector`, `fc_vector`) that don't use standard allocators. Users wanting `std::pmr::vector` as a container would not get the allocator-propagation constructors.

**Missing `at()` for flat_map**: The standard requires `flat_map::at(key)` which throws `std::out_of_range`. psi::vm's flat_map_impl uses `operator[]` and `find()` but may not provide `at()`.

**No `std::erase_if` in `<flat_map>` namespace**: The standard specifies free function `erase_if` for flat_map. psi::vm implements it as a hidden friend for flat_set but may need verification for flat_map.

### 9.3 Compliance Gaps in MS STL

**Open issue #6069**: `_Emplace_hint()` implementations diverge between flat_map and flat_set, which could lead to behavioral inconsistencies.

**Open issue #5546**: Unusual `size_type`/`difference_type` handling may not be fully correct for non-standard sequence containers.

### 9.4 libc++ Known Issues

**`vector<bool>` handling**: Unlike MS STL which explicitly rejects `vector<bool>` with a `static_assert`, libc++ may silently accept it (behavior unverified). MS STL's PR #5045 added an explicit rejection.

**Container exception safety traits**: libc++ uses `__container_traits<KC>::__emplacement_has_strong_exception_safety_guarantee` to select exception safety strategy. This trait must be specialized for non-standard containers; the default may be overly conservative.

---

## 10. Extensions Beyond the Standard

### 10.1 psi::vm Extensions

| Extension | Description |
|---|---|
| **Custom container support** | Works with `vm_vector`, `heap_vector`, `fc_vector` — VM-backed, stack-allocated, and traditional heap containers |
| **`reserve()` / `capacity()`** | Exposes underlying container's reserve/capacity (not in the standard) |
| **`shrink_to_fit()`** | Forwarded from underlying container |
| **`span()` conversion** | Direct `std::span` access to underlying storage |
| **`is_trivially_moveable` specialization** | Enables optimized move operations for containers that support it |
| **`make_trivially_copyable_predicate`** | Comparator wrapper for optimal codegen in binary search |
| **Index-based iterators** | Enable smaller iterators with non-pointer `size_type` |
| **No allocator baggage** | Simpler API surface, no allocator-extended overloads |

### 10.2 libc++ Extensions

| Extension | Description |
|---|---|
| **`_LIBCPP_CONSTEXPR_SINCE_CXX26`** | Forward-looking constexpr support for C++26 (P3372R3) |
| **`_LIBCPP_HIDE_FROM_ABI`** | Symbol visibility control for ABI stability |
| **`__ra_iterator`** | Custom random-access iterator wrapper for flat_set (enables const-enforcement without relying on container::const_iterator directly) |
| **Container traits inspection** | `__container_traits<KC>::__emplacement_has_strong_exception_safety_guarantee` for tailored exception safety |
| **Product iterator optimization** | `__is_product_iterator_of_size` optimization for zip-like iterators in `__flat_map_utils::__append` |

### 10.3 MS STL Extensions

| Extension | Description |
|---|---|
| **`_NODISCARD` annotations** | Compiler warnings for discarded return values |
| **`_STL_ASSERT` / `_STL_INTERNAL_CHECK`** | Debug-mode invariant validation (sorted, unique, sizes match) |
| **Heterogeneous insertion consistency check** | `_Msg_heterogeneous_insertion_inconsistent_with_lookup` debug assertion |
| **Natvis debugger visualizers** | Visual Studio debugging support for flat containers and iterators |
| **Iterator unwrapping** | `_Unwrapped()` / `_Seek_to()` infrastructure for algorithm optimization in debug builds |
| **`vector<bool>` rejection** | `static_assert` preventing `vector<bool>` as underlying container |
| **Noexcept-specialized guards** | Swap guards that compile away entirely when operations are noexcept |
| **Equality-over-equivalence dedup** | Performance optimization for standard comparators |

---

## 11. Summary & Recommendations

### 11.1 Strengths by Implementation

**psi::vm**
- Most compact implementation (1,923 lines total)
- Best binary code sharing for flat_set family (shared base without `IsUnique`)
- Lightest compile-time footprint (minimal includes)
- Custom container backend support
- Index-based iterators enable smaller iterator size with small `size_type`

**libc++**
- Cleanest ABI story (no base classes, no bool template parameters)
- Forward-looking constexpr (C++26) support
- Complete standard compliance including allocator support
- Each class is independently understandable
- Fine-grained exception safety via container trait inspection

**MS STL**
- Full standard compliance with extensive debug infrastructure
- Most advanced deduplication optimization (equality over equivalence)
- Partial-sort optimization on construction (`is_sorted_until`)
- Rich debugging support (natvis, iterator verification, assertions)
- Noexcept-specialized guards eliminate overhead for common cases
- Zero-overhead flat_set iterator (uses underlying container's const_iterator directly)

### 11.2 Weaknesses by Implementation

**psi::vm**
- No allocator support (intentional, but limits standard compatibility)
- flat_map still uses old monolithic `IsUnique` design (not yet refactored)
- Smaller user/testing community

**libc++**
- 3.1x more code than psi::vm due to class duplication
- No shared base → bug fixes must be applied to 4 parallel implementations
- Verbose (36+ deduction guides per container family)

**MS STL**
- Took 2.5 years and 11+ contributors to ship
- Acknowledged design smell ("too much special-casing of `_IsUnique`" — issue #6069)
- Required compiler ICE workarounds
- `iterator_category = input_iterator_tag` for flat_map iterators may surprise legacy algorithm code

### 11.3 Architectural Verdict

For the specific goal of minimizing template bloat while maintaining clean separation of unique/multi concerns, **psi::vm's flat_set refactored design** (inheritance with `IsUnique`-free base) is the strongest approach. It achieves:

1. **Genuine binary-level code sharing** — not just `if constexpr` dead code elimination, but actual shared template instantiations
2. **Clean API** — `flat_set` and `flat_multiset` are distinct types with appropriate return types (pair vs iterator for emplace)
3. **Minimal code duplication** — only ~15% of the code is in the derived classes
4. **No `IsUnique` in the type system** — iterators, comparators, and lookup functions don't carry a bool they don't need

The natural next step would be to apply the same refactoring to `flat_map_impl`, splitting it into a shared `flat_map_impl<Key, T, Compare, KC, MC>` base with `flat_map` and `flat_multimap` as derived classes — mirroring the flat_set design.
