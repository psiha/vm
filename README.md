# psi::vm

[![Build & Test](https://github.com/psiha/vm/actions/workflows/gh-actions.yml/badge.svg?branch=master)](https://github.com/psiha/vm/actions/workflows/gh-actions.yml)
[![License](https://img.shields.io/badge/license-BSL--1.0-blue.svg)](https://www.boost.org/LICENSE_1_0.txt)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/compiler_support)

**Portable, lightweight, near-zero-overhead memory mapping, virtual memory management, containers and utilities.**


---

## Virtual memory & mapping

Cross-platform abstractions over OS virtual memory and memory-mapped I/O:

- **Allocation** — reserve/commit semantics (Windows `VirtualAlloc`, POSIX `mmap`+`madvise`)
- **File mapping** — RAII mapped views with configurable access flags and sharing modes
- **Shared memory** — IPC-ready shared memory objects
- **Protection** — page-level read/write/execute protection control
- **Alignment utilities** — aligned allocation, page-size queries

---

## Vectors

The vector system is built on a **`vector<Storage>`** class template parameterized by a storage backend. This design cleanly separates the standard vector interface (push_back, insert, erase, ...) from the memory management strategy.

### Storage backends

| Storage | Description |
|---------|-------------|
| `heap_storage<T>` | Heap-allocated array with pluggable allocator (default: `mimalloc` on Windows/macOS, `crt` on Linux). Supports `tr_vector_options` for capacity caching, geometric growth, and alignment |
| `fixed_storage<T, N>` | Inline (stack) array with compile-time capacity bound. Configurable overflow handler |
| `vm_storage<T>` | Persistent storage via memory-mapped files or shared memory, with configurable headers and allocation granularity |

### Vector types

| Container | Definition | Description |
|-----------|-----------|-------------|
| `vector<Storage>` | Primary template | Standard vector interface over any storage backend |
| `tr_vector<T>` | `= vector<heap_storage<T>>` | Trivially relocatable heap vector — exploits trivial relocatability for move/realloc without element-wise copy |
| `fc_vector<T, N>` | `= vector<fixed_storage<T, N>>` | Fixed-capacity inline vector (`inplace_vector`-like) |
| `vm_vector<T>` | `= vector<vm_storage<T>>` | VM-backed persistent vector |
| `small_vector<T, N>` | Standalone | Inline (stack) buffer with heap spill. Three layout modes: *compact* (MSB tag, trivially relocatable, `[[clang::trivial_abi]]`), *compact_lsb* (LSB tag, optimal LE addressing), *pointer_based* (LLVM/Boost style, type-erasable via `small_vector_base<T>`). Configurable `geometric_growth` factor, `sz_t`, and alignment |

### Allocators

All heap-based storage uses allocators derived from the `allocator_base<Derived, T, sz_t>` CRTP base, which provides the [N2045](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2006/n2045.html) "Version 2" allocator interface (`allocation_command`, `allocate_at_least`, single-node allocation, and generic resize dispatch).

| Allocator | Backend | In-place expand | Notes |
|-----------|---------|:-:|-------|
| `crt_allocator` | `malloc`/`realloc` | MSVC `_expand` | Default on Linux (glibc). MSVC `_expand` enables in-place growth & shrink |
| `mimalloc_allocator` | [mimalloc](https://github.com/microsoft/mimalloc) | `mi_expand` | Default on Windows & macOS. Best overall throughput |
| `dlmalloc_allocator` | Boost.Container alloc_lib | `boost_cont_grow` | Good in-place growth, guaranteed in-place shrink |
| `jemalloc_allocator` | [jemalloc](https://github.com/jemalloc/jemalloc) | `je_xallocx` | Optional |
| `tcmalloc_allocator` | [tcmalloc](https://github.com/google/tcmalloc) | `tc_realloc` | Optional |

---

## Tree containers

| Container | Description |
|-----------|-------------|
| `b_plus_tree<K, Cmp>` | Cache-friendly B+ tree with linear-search-optimized leaves |

---

## Flat associative containers

A complete, compact [C++23 `std::flat_map`/`std::flat_set`](https://wg21.link/P0429R9) polyfill with extensions. See the detailed [comparison with libc++ and MS STL](doc/flat_container_comparison.md).

| Container | Description |
|-----------|-------------|
| `flat_set<K, Cmp, KC>` | Sorted unique set backed by a contiguous container |
| `flat_multiset<K, Cmp, KC>` | Sorted multiset variant |
| `flat_map<K, V, Cmp, KC, MC>` | Sorted unique map with separate key/value containers |
| `flat_multimap<K, V, Cmp, KC, MC>` | Sorted multimap variant |

Highlights:
- Inheritance-based deduplication — deducing-this based CRTP with maximum binary-level not just source-level code sharing and reuse
- When available, will use adaptive merge and pdq sort (including branchless variant) from the Boost libraries
- Avoids the 'double not-less-than _and_ not-greater-than comparison' idiom to perform simple equivalence comparisons (using direct equality comparisons when safe to do so)
- Optimized bulk insert: append + sort tail + adaptive merge + dedup
- Optimizations at the micro/ABI level

### Extensions beyond C++23 `std::flat_*`

| Extension | Containers | Description |
|-----------|------------|-------------|
| `nth(n)` | all | Returns an iterator to the *n*-th element (like `begin() + n` but named, matching Boost.Container) |
| `index_of(it)` | all | Returns the positional index of an iterator (inverse of `nth`) |
| `emplace_back(key, args...)` | all | Unchecked sorted append — O(1) insertion when the caller guarantees the key is ≥ all existing keys (debug-asserted). Returns a reference to the inserted element. Ideal for building containers from pre-sorted data |
| `merge(source)` | all | Transfer elements from another container of the same type (matching Boost.Container) |
| `reserve(n)` | all | Pre-allocate storage for *n* elements |
| `shrink_to_fit()` | all | Release excess capacity |
| `keys()` | all | Direct `const` access to the underlying key container. Note: `std::flat_set` does **not** provide `keys()` — only `std::flat_map` does — so this is an 'API uniformization' extension for sets |
| `values()` | maps | Direct-and-safe mutable access to values as a span (TODO a subrange to also cover deques) |

#### Iterator categories

Unlike `std::flat_set` (whose iterators are `random_access_iterator` in libc++ due to a protective wrapper), `psi::vm` flat_*set containers expose the strongest iterator category the underlying container supports — no wrapping overhead.

---

## Compiler & platform support

| Compiler | Platform | Standard library | CI | Notes |
|----------|----------|------------------:|:--:|-------|
| Clang 21+ | Linux | libc++ | :white_check_mark: | `-std=gnu++2c` |
| Clang-CL 20+ | Windows | MS STL | :white_check_mark: | MSVC ABI, Clang frontend |
| Apple Clang | macOS (ARM64) | libc++ | :white_check_mark: | Latest Xcode |
| GCC 15+ | Linux | libstdc++ | :white_check_mark: | `-std=gnu++2c` |
| MSVC 19.x+ | Windows | MS STL | :white_check_mark: | `/std:c++latest` |

All configurations are tested in both **Debug** and **Release** builds.

---

## Building

### Requirements

- **CMake** 3.27+
- **C++23**-capable compiler (see table above)
- **Ninja** (recommended) or Visual Studio generator
- Dependencies are fetched automatically via [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake)

### Quick start

```bash
# Linux / macOS (Clang)
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build --config Release

# Linux (GCC)
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=g++
cmake --build build --config Release

# Windows (MSVC)
cmake -B build
cmake --build build --config Release

# Windows (Clang-CL)
cmake -B build -T ClangCL
cmake --build build --config Release

# Run tests
cmake --build build --target vm_unit_tests
ctest --test-dir build/test --build-config Release --output-on-failure
```

### CMake integration (CPM)

```cmake
CPMAddPackage("gh:psiha/vm@master")
target_link_libraries(your_target PRIVATE psi::vm)
```

---

## Project structure

```
include/psi/vm/
├── allocators/             Allocator backends
│   ├── allocator_base.hpp  N2045 V2 CRTP base (allocation_command, allocate_at_least, resize)
│   ├── crt.hpp             CRT malloc/realloc allocator
│   ├── mimalloc.hpp        Microsoft mimalloc allocator
│   ├── dlmalloc.hpp        Boost.Container dlmalloc allocator
│   ├── jemalloc.hpp        jemalloc allocator
│   └── tcmalloc.hpp        Google tcmalloc allocator
├── containers/
│   ├── storage/            Storage backends for vector<Storage>
│   │   ├── heap.hpp        heap_storage (heap-allocated, pluggable allocator)
│   │   └── fixed.hpp       fixed_storage (inline, compile-time capacity)
│   ├── vector.hpp          vector<Storage> template + vector_impl CRTP mixin
│   ├── tr_vector.hpp       tr_vector<T> type alias (= vector<heap_storage<T>>)
│   ├── fc_vector.hpp       fc_vector<T,N> type alias (= vector<fixed_storage<T,N>>)
│   ├── vm_vector.hpp       vm_vector<T> (memory-mapped storage)
│   ├── small_vector.hpp    small_vector<T,N> (inline buffer + heap spill)
│   ├── b_plus_tree.hpp     B+ tree
│   └── flat_*.hpp          Flat associative containers (set, map, multi*)
├── mapped_view/            RAII memory-mapped view wrappers
├── mapping/                File & memory mapping primitives
├── mappable_objects/       File handles, shared memory objects
├── handles/                Cross-platform OS handle abstractions
├── flags/                  Access, protection, construction policy flags
├── allocation.hpp          Reserve/commit allocation
├── align.hpp               Alignment utilities
├── protection.hpp          Memory protection flags
└── span.hpp                Strongly-typed memory spans

src/                        Platform-specific implementations (win32/posix)
test/                       Google Test suite
doc/                        Technical documentation & analyses
```

---

## Dependencies

Fetched automatically at configure time via CPM.cmake:

| Dependency | Version | Purpose |
|-----------|---------|---------|
| [Boost](https://www.boost.org/) | 1.90.0 | `container`, `core`, `assert`, `integer`, `move`, `stl_interfaces` |
| [psiha/config_ex](https://github.com/psiha/config_ex) | master | Configuration utilities |
| [psiha/std_fix](https://github.com/psiha/std_fix) | master | C++ standard library fixes/extensions |
| [psiha/err](https://github.com/psiha/err) | master | Error handling |
| [psiha/build](https://github.com/psiha/build) | master | Build infrastructure |
| [Google Test](https://github.com/google/googletest) | 1.15.2 | Testing (test target only) |
| [mimalloc](https://github.com/microsoft/mimalloc) | latest | Allocator backend (ON by default, CMake: `PSI_VM_MIMALLOC`) |
| [jemalloc](https://github.com/jemalloc/jemalloc) | — | Allocator backend (OFF by default, CMake: `PSI_VM_JEMALLOC`) |
| [tcmalloc](https://github.com/google/tcmalloc) | — | Allocator backend (OFF by default, CMake: `PSI_VM_TCMALLOC`) |

---

## Debugger support

- **Visual Studio**: [Natvis](https://learn.microsoft.com/en-us/visualstudio/debugger/create-custom-views-of-native-objects) visualizers for all container types (`psi_vm.natvis`, auto-loaded by CMake)
- **LLDB**: Python pretty-printer script (`psi_vm_lldb.py`)

---

## Documentation

- [Flat container comparison: psi::vm vs libc++ vs MS STL](doc/flat_container_comparison.md) — detailed architectural, codegen, and compliance analysis
- [B+ tree lower_bound optimization](include/psi/vm/containers/BTREE_LOWER_BOUND_OPTIMIZATION.md)

---

## Sponsors (past and present)

[Farseer](https://farseer.com) · [Microblink](https://microblink.com)

---

## License

[Boost Software License 1.0](https://www.boost.org/LICENSE_1_0.txt)

Copyright &copy; 2011 &ndash; 2026 Domagoj Šarić. All rights reserved.
