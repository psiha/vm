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

| Container | Description |
|-----------|-------------|
| `tr_vector<T>` | **Trivially Relocatable Vector** — exploits trivial relocatability for move/realloc without element-wise copy |
| `vm_vector<T>` | **VM-backed Vector** — persistent storage via memory-mapped files or shared memory, with configurable headers and allocation granularity |
| `fc_vector<T, N>` | **Fixed Capacity Vector** — inline (stack) storage with compile-time capacity bound (`inplace_vector`-like) |

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
- Extensions like mutable-yet-safe access to values for map containers, merge, reserve, shrink_to_fit
- Optimizations at the micro/ABI level
- Optimized bulk insert: append + sort tail + adaptive merge + dedup
- Index-based SCARY iterators

---

## Compiler & platform support

| Compiler | Platform | CI | Notes |
|----------|----------|:--:|-------|
| Clang 21+ | Linux | :white_check_mark: | `-std=gnu++2c`, libc++ |
| Clang-CL 20+ | Windows | :white_check_mark: | MSVC ABI, Clang frontend |
| Apple Clang | macOS (ARM64) | :white_check_mark: | Latest Xcode |
| GCC 15+ | Linux | :white_check_mark: | `-std=gnu++2c`, libstdc++ |
| MSVC 19.x+ | Windows | :white_check_mark: | `/std:c++latest` |

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
├── containers/         Flat containers, B+ tree, vector variants
├── mapped_view/        RAII memory-mapped view wrappers
├── mapping/            File & memory mapping primitives
├── mappable_objects/   File handles, shared memory objects
├── handles/            Cross-platform OS handle abstractions
├── flags/              Access, protection, construction policy flags
├── allocation.hpp      Reserve/commit allocation
├── align.hpp           Alignment utilities
├── protection.hpp      Memory protection flags
└── span.hpp            Strongly-typed memory spans

src/                    Platform-specific implementations (win32/posix)
test/                   Google Test suite
doc/                    Technical documentation & analyses
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
