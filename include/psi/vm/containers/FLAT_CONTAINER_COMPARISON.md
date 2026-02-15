# psi::vm Flat Containers — Implementation Comparison

Comprehensive comparison of `psi::vm::flat_{set,multiset,map,multimap}` against
the three major C++ standard library implementations: libc++ (LLVM 22),
libstdc++ (GCC 15.2.1), and MS STL (GitHub development branch).

All four implementations provide the same four container types as specified by
C++23 P0429/P1222.

**MS STL reference PRs:**
[#6071](https://github.com/microsoft/STL/pull/6071) — flat_map/flat_set implementation,
[#6024](https://github.com/microsoft/STL/pull/6024) — equivalence→equality optimisation.

---

## 1. Line Count Comparison

### 1a. Total and Code Lines

"Code lines" = total minus blank lines and pure-comment lines.

| Implementation | Files | Total Lines | Code Lines | Code/Total |
|----------------|-------|-------------|------------|------------|
| **psi::vm**    | 3 core (+3 shared¹) | 2,392 (2,822) | 1,454 (1,642) | 61% |
| **libstdc++**  | 2 | 2,692 | 2,176 | 81% |
| **MS STL**     | 2 | ~2,606 | ~2,100² | ~81% |
| **libc++**     | 10 impl (+2 umbrella) | 4,771 (6,135³) | 3,855 (4,927³) | 81% |

¹ abi.hpp (196 lines), komparator.hpp (154 lines), and lookup.hpp (80 lines) are shared
  with the b+tree; parenthesised numbers include them.
² MS STL flat containers are not shipped in any released MSVC toolset as of
  v14.50; numbers are from the GitHub STL repository development branch.
³ Grand total including umbrella headers, module .inc files, and all internal
  detail headers (__flat_map/*, __flat_set/*).

### 1b. Code Lines Excluding CTAD Deduction Guides

CTAD guides are pure boilerplate — every implementation must have them and they
are mechanically identical.

| Implementation | Code Lines | CTAD Lines | Code w/o CTAD | Savings |
|----------------|------------|------------|---------------|---------|
| **psi::vm**    | 1,461 | ~114 | **1,347** | 7.8% |
| **libstdc++**  | 2,176 | ~152 | **2,024** | 7.0% |
| **libc++**     | 3,855 | ~240 | **3,615** | 6.2% |

### 1c. Size Ratios

Relative to psi::vm core code (1,461 lines = 1.0×):

| Implementation | Code Lines | Ratio |
|----------------|------------|-------|
| **psi::vm**    | 1,461 | **1.0×** |
| **MS STL**     | ~2,100 | ~1.44× |
| **libstdc++**  | 2,176 | 1.50× |
| **libc++**     | 3,855 | 2.65× |

---

## 2. Architectural Comparison

| Feature | psi::vm | libstdc++ | libc++ | MS STL |
|---------|---------|-----------|--------|--------|
| **Shared base class** | `flat_impl` (all 4 types) | None | None | `_Flat_base` (unique+multi via `_IsUnique` bool) |
| **Unique/multi sharing** | Derived thin wrappers | Copy-paste per class | Separate files per class | Template bool parameter |
| **Map iterator** | Proxy: `paired_storage*` + index (16 B) | `_Nth_iter` (2 containers + index, 24 B) | `__key_value_iterator` (2 iterators, 16 B) | `_Pairing_iterator` (2 iterators, 16 B) |
| **SCARY iterators (set)** | Yes (= KeyContainer::iterator) | No (own type) | No (own type) | No (own type) |
| **Exception safety** | try/catch (paired_storage sync + bulk ops) | `_GLIBCXX_TRY`/`_GLIBCXX_CATCH` | `__exception_guard` RAII | `_Clear_guard` RAII |
| **Key ABI optimisation** | `pass_in_reg` + `prefetch` | None | None | None |
| **Comparator storage** | `Komparator<C>` (EBO via inheritance + transparent detection) | `[[no_unique_address]]` member | `_LIBCPP_NO_UNIQUE_ADDRESS` member | `_MSVC_NO_UNIQUE_ADDRESS` member |
| **Predicate wrapping** | `make_trivially_copyable_predicate` | None | None | `_Pass_fn` / `_Ref_fn` |
| **Equivalence test** | `comp_eq`: `==` for simple comps, `!<∧!>` fallback | `!<∧!>` always | `!<∧!>` always | `_Equivalence_is_equality`: `==` for standard comps + integral/string, `!<∧!>` fallback ([PR #6024](https://github.com/microsoft/STL/pull/6024)) |
| **Deducing this (C++23)** | Yes | No | No | No |
| **Sort algorithm** | pdqsort (external) | `std::sort` | `ranges::sort` | `std::sort` |
| **Merge dedup** | `inplace_set_unique_difference` (custom) | `inplace_merge` + `unique` | `unique` + move | `inplace_merge` + `_Unique_erase` |
| **emplace_hint (multi)** | Shared `hinted_insert_pos` in `flat_impl`: validate + narrowed half-search | Per-class: validate + narrowed half-search (reverse-iter variant) | Per-class: validate + narrowed half-search (`[[likely]]` on valid path) | Shared `_Flat_base::_Emplace_hint`: validate + narrowed half-search (`weak_ordering` dispatch) |
| **C++ module support** | No | No | Yes (.inc files) | No |
| **Allocator-aware ctors** | No | No | Yes | No |

---

## 3. Exception Safety

All operations that can fail mid-mutation (bulk insert, merge, erase_if)
provide the basic guarantee: on exception the container is left empty (cleared).

### 3a. Guarded Operations

| Operation | Guard Strategy |
|-----------|----------------|
| `paired_storage::insert_element_at` | try/catch: if value insert fails, roll back key insert |
| `paired_storage::append_ranges` | try/catch: if value append fails, truncate both to oldSize |
| `bulk_insert` (append + sort_merge) | try/catch: clear on sort/merge failure |
| `merge` (lvalue, unique) | try/catch: clear dest on emplace_back/sort_merge failure |
| `merge` (rvalue) | try/catch: clear dest on sort_merge failure |
| `erase_if` | try/catch: clear on predicate exception |

### 3b. Inherently Safe Operations

| Operation | Why |
|-----------|-----|
| Single `emplace`/`insert` | Delegates to `insert_element_at` (already guarded) or vector::insert (provides strong guarantee for single elements) |
| `erase` (positional) | Vector erase is noexcept for nothrow-movable types |
| `swap` | noexcept |
| `clear`, `reserve`, `shrink_to_fit` | noexcept or single-container operations |
| Constructors | If sort throws, object is not constructed (members destructed automatically) |

### 3c. Comparison with Standard Libraries

| Impl | Guard pattern | Scope |
|------|---------------|-------|
| **psi::vm** | Explicit try/catch, clear on failure | bulk_insert, merge, erase_if |
| **libc++** | `__exception_guard` RAII, clear on failure | operator=, replace, erase, swap, bulk insert, erase_if |
| **libstdc++** | `_GLIBCXX_TRY`/`_GLIBCXX_CATCH` macros | Similar scope to libc++ |
| **MS STL** | `_Clear_guard` RAII | Similar scope |

---

## 4. Key Differentiators

### 4a. psi::vm Advantages

- **Smallest codebase** — 1.5× smaller than the most compact stdlib (libstdc++),
  2.65× smaller than libc++.  Savings come from the shared `flat_impl` base,
  deducing-this, and SCARY set iterators.

- **Register-optimised lookups** — `pass_in_reg` wraps keys and comparators for
  efficient register passing; `prefetch` resolves indirect comparisons once
  before binary search.  Unique among all four implementations.

- **SCARY set iterators** — flat_set iterator IS the underlying vector iterator.
  Zero overhead, maximum interop.

- **Deducing-this** — eliminates CRTP boilerplate; a single `find()` template in
  flat_impl serves flat_set, flat_multiset, flat_map, and flat_multimap.

- **pdqsort** — pattern-defeating quicksort; generally faster than std::sort on
  real-world data.

- **Custom merge dedup** — `inplace_set_unique_difference` filters duplicates
  before the merge step, avoiding a separate `unique` pass.

- **Short-circuited equivalence** — `comp_eq()` uses `==` instead of
  `!comp(a,b) && !comp(b,a)` for simple comparators (`std::less<>`,
  `std::greater<>`, and transparent comparators on types with `==`).
  One comparison instead of two.  MS STL independently added the same
  optimisation for deduplication in [PR #6024](https://github.com/microsoft/STL/pull/6024)
  (17–20% faster for strings); libc++ and libstdc++ still use double-negation.

### 4b. psi::vm Trade-offs

- **No allocator-aware constructors** — only libc++ provides these.  All four
  implementations template on container types; psi::vm (like libstdc++ and MS STL)
  does not add allocator-forwarding ctors.

- **No C++ module support** — only libc++ has `.inc` module shims.

- **Depends on shared infrastructure** — abi.hpp and komparator.hpp are shared
  with the b+tree.  Standard libraries inline their equivalent machinery.

---

## 5. File Organisation

### psi::vm (3 core files)
```
flat_common.hpp   827 lines   flat_impl, detail:: utilities, sorted tags
flat_set.hpp      583 lines   flat_set_impl, flat_set, flat_multiset, CTAD
flat_map.hpp      996 lines   paired_storage, flat_map_impl, flat_map, flat_multimap, CTAD
```
Shared: `abi.hpp` (196), `komparator.hpp` (154), `lookup.hpp` (80).

### libstdc++ (2 monolithic headers)
```
flat_map          1601 lines  flat_map + flat_multimap
flat_set          1091 lines  flat_set + flat_multiset
```

### libc++ (10 implementation files + 2 umbrellas)
```
__flat_map/flat_map.h           1289    __flat_set/flat_set.h            892
__flat_map/flat_multimap.h      1090    __flat_set/flat_multiset.h       860
__flat_map/key_value_iterator.h  217    __flat_set/ra_iterator.h         157
__flat_map/utils.h               122    __flat_set/utils.h                82
__flat_map/sorted_equivalent.h    31    flat_map (umbrella)              744
__flat_map/sorted_unique.h        31    flat_set (umbrella)              620
```

### MS STL (2 files, GitHub development branch)
```
flat_map          ~1613 lines  flat_map + flat_multimap
flat_set          ~993 lines   flat_set + flat_multiset
```

---

## 6. Code Density: Effective LOC per Feature

Approximate lines of "real logic" (excluding CTAD, forwarding ctors, typedefs,
include guards) per feature area:

| Feature | psi::vm | libstdc++ | libc++ |
|---------|---------|-----------|--------|
| Lookup (find/lb/ub/er/count/contains) | ~45 (shared) | ~80 × 4 classes | ~70 × 4 classes |
| Insert/emplace (unique) | ~40 | ~60 | ~80 |
| Insert/emplace (multi) | ~25 | ~40 | ~60 |
| Erase | ~25 (shared) | ~30 × 4 | ~25 × 4 |
| Merge | ~70 (shared) | ~50 × 4 | ~60 × 4 |
| Bulk insert | ~10 (shared) | ~15 × 4 | ~20 × 4 |
| erase_if | ~20 (shared) | ~15 × 4 | ~10 × 4 |
| Map iterator | ~45 | ~60 | ~100 |

The "(shared)" annotations highlight code written once in flat_impl that serves
all four container types — the primary source of psi::vm's size advantage.

---

## 7. Predicate / Comparator Passing to Algorithms

C++ standard algorithms (`std::sort`, `std::lower_bound`, `std::inplace_merge`,
`std::unique`, etc.) accept comparators **by value**.  For non-trivially-copyable
or stateful comparators this can cause repeated copy-construction through the
algorithm's internal call chain.

Two of the four implementations address this with wrapper functions that
substitute a lightweight reference-based proxy for non-trivial comparators:

### 7a. Wrapper Comparison

| | psi::vm | MS STL | libc++ | libstdc++ |
|---|---------|--------|--------|-----------|
| **Wrapper function** | `make_trivially_copyable_predicate` | [`_Pass_fn`](https://github.com/microsoft/STL/blob/main/stl/inc/xutility) | — | — |
| **Reference wrapper** | Lambda capturing `Pred &` | `_Ref_fn<Fx>` (stores `Fx &`) | — | — |
| **Pass-by-value threshold** | `can_be_passed_in_reg`: trivially copyable **and** `sizeof ≤ 2 × sizeof(void*)` | `sizeof ≤ sizeof(void*)` **and** trivially copy-constructible **and** trivially destructible | — | — |
| **Applied to** | `lower_bound`, `upper_bound`, `equal_range`, pdqsort, `inplace_merge` | `sort`, `lower_bound`, `upper_bound`, `equal_range`, `inplace_merge`, `unique` | — | — |

### 7b. How It Works

Both implementations follow the same pattern:

```
if ( trivially-copyable and small )
    pass comparator directly (by value — fits in registers)
else
    wrap in a lightweight reference proxy, pass that instead
```

The proxy is itself trivially copyable (it's just a reference/pointer), so
algorithms can copy it freely without touching the original comparator.

**psi::vm** additionally wraps the comparator at the storage level via
`Komparator<C>` (EBO through public inheritance) and provides `enreg()` /
`pass_in_reg` to wrap *keys* for register-efficient passing — a related but
separate optimisation not present in any of the standard libraries.

### 7c. Impact

The wrapping matters for:
- **Stateful comparators** with non-trivial copy constructors (e.g., locale-aware
  collation, context objects, captured allocators).
- **Algorithms that internally copy** the predicate during partitioning (sort)
  or recursive subdivision (inplace_merge).

For the common case of stateless `std::less<>`, all four implementations
are equivalent — the comparator is empty and trivially copyable.

---

## 8. Extensions Beyond C++23

psi::vm flat containers offer several features not present in the C++23
`std::flat_map` / `std::flat_set` specification (P0429/P1222).

### 8a. API Extensions

| Extension | Scope | Description |
|-----------|-------|-------------|
| `reserve(n)` | All 4 | Pre-allocate storage capacity. Not in the standard; relies on underlying container support. |
| `shrink_to_fit()` | All 4 | Release excess capacity. |
| `merge(source)` | All 4 | `std::map`-style element transfer (lvalue: selective for unique, move-all for multi; rvalue: always move-all). Not in C++23 flat containers. |
| `insert_range(sorted_tag, R&&)` | All 4 | Sorted bulk insert — skips the sort step when the caller guarantees pre-sorted input. |
| `assign(initializer_list)` | All 4 | Convenience shorthand for `clear()` + `insert(il)`. |
| `key_comp_mutable()` | All 4 | Mutable reference to the comparator. Enables in-place comparator reconfiguration without rebuild. |
| Mutable `values()` → `span<T>` | Maps | Returns `std::span<mapped_type>` for direct mutable access to the values container. Standard only provides `const` access. |
| `sequence()` | Sets | Alias for `keys()` — returns a const reference to the underlying container. |

### 8b. Implementation Extensions

| Extension | Description |
|-----------|-------------|
| **`pass_in_reg` / `enreg`** | ABI-level optimisation: wraps keys and comparators for efficient register passing. Transparent comparators enable `pass_in_reg` even for non-trivial keys (e.g., `std::string` → `string_view`). Unique among all four implementations. |
| **`prefetch`** | Resolves indirect comparisons (e.g., pointer/ID-based keys) once before binary search, avoiding repeated fetches per comparison. |
| **`[[gnu::sysv_abi, gnu::pure]]`** | Applied to detail:: lookup functions for cross-ABI register-calling-convention optimisation. |
| **SCARY set iterators** | `flat_set::iterator` IS the underlying `KeyContainer::const_iterator`. Zero overhead, maximum interop with algorithms expecting raw container iterators. |
| **pdqsort** | Pattern-defeating quicksort replaces `std::sort`. Generally faster on real-world data with adaptive fallbacks. |
| **`inplace_set_unique_difference`** | Custom pre-merge dedup algorithm: filters the appended tail against the sorted prefix before merging, eliminating the need for a separate `unique` pass. |
| **`boost::movelib::adaptive_merge`** | Uses spare vector capacity as scratch buffer for merge operations, reducing allocations. |
| **Deducing-this (C++23)** | Eliminates CRTP; a single `find()` in `flat_impl` serves all 4 container types with correct return types. |
| **Layered `erase_if` dispatch** | `erase_if(container, pred)` delegates to `erase_if(storage, pred)` — storage-level overloads handle set (single container) and map (zip_view + projection) paths independently. |
