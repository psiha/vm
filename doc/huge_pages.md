# Transparent huge pages for memory backed storage (Linux)

Memory backed storage (`map_memory()`, `map_cow_memory()` of `mem_mapping`,
`vm_storage`, `vm_vector` and the b+tree) can ask the kernel for transparent
huge pages. It is off by default. This page says what each piece does, what it
costs, and when it pays. Everything here is Linux only. Elsewhere the hint is
ignored.

## What is always on

| behaviour | what it does | cost |
|---|---|---|
| **file view placement** | A file view (memfd included) that spans at least one PMD (2 MiB with 4 KiB pages) is placed at an address congruent to its file offset modulo 2 MiB, if the kernel did not already put it there. A smaller view is left where the kernel put it. It is moved once, by the growth that first makes it span a PMD. Later growth keeps that phase. | A move (a reservation, `mremap`, up to two `munmap`s) once per view that reaches 2 MiB. |
| **growth to a PMD boundary** | A memory backed view (anonymous, memfd or a copy-on-write clone's own memory) of 2 MiB or more grows to end on a 2 MiB boundary. | Untouched address space and, for a memfd, a sparse file length. No memory unless the container writes there. |

The kernel maps a huge folio of a file (page cache or shmem) only where the
virtual address and the file offset agree modulo the folio size, and only where
the whole aligned span lies inside the mapping. Without the placement, a view
that starts small and grows in place would never get one, whatever the
settings.

Placing every file view, whatever its size, is what makes small views
expensive. A view under 2 MiB holds no whole span, so it cannot take a huge
folio. Moving it cost a small map + unmap about 1.4x the CPU. Deferring the move
has a price too. A small view now sits where the kernel packs it, with no free
address space after it, so its first growth usually relocates instead of
growing in place. With 48 memfd pools growing side by side past 2 MiB, that
means 114 relocations instead of 74. Total map/remap/unmap calls go up by about
5 %, and single-threaded growth time stays within noise.

## What `huge_pages::yes` adds

`map_memory( ..., huge_pages::yes )` / `map_cow_memory( ..., huge_pages::yes )`
(`bptree::map_memory( capacity, huge_pages::yes )` for a b+tree) advise the new
view `MADV_HUGEPAGE`, right after mapping it, before anything is written:

- **Before the first touch.** The kernel decides a page's size when the page is
  first touched. A small page keeps the whole 2 MiB span around it small.
  Constructing the initial elements writes every page of the initial capacity,
  so advice given afterwards leaves all of that capacity in small pages. It then
  stays that way unless khugepaged collapses it, which it may never do.
- **Only storage that spans a PMD.** Under `shmem_enabled=advise`, some kernels
  (6.6) allocate a whole huge folio for a memfd span that the mapping covers
  only partly. Advising a 100 KiB pool could cost it 2 MiB. Storage that starts
  smaller is not advised, even when it grows past 2 MiB later.
- The advice is a property of the VMA, so growth keeps it, whether in place or
  moved. Copy-on-write clones are never advised. A clone's writes copy into
  small pages whatever the advice.
- `map_file()` storage is never advised. On a writable file mapping, a huge
  folio becomes the unit of dirtying and writeback: one sparse update dirties
  2 MiB, and the next flush writes 2 MiB.

With `huge_pages::no` (the default), no advice is given and nothing else
changes.

Which kernel setting governs the hint depends on the backing:

| storage | setting | note |
|---|---|---|
| `map_memory` (private anonymous) | `transparent_hugepage/enabled` = `madvise` or `always` | |
| `map_cow_memory` (memfd) | `transparent_hugepage/shmem_enabled` = `advise`, `within_size`, `always` | the common default is `never` |
| memfd, below 2 MiB | `hugepages-<size>kB/shmem_enabled` = `advise` (+ the advice), `inherit` or `always` | multi-size (mTHP) folios, e.g. 64 KiB: measured on 6.12 and 7.2 |

## `-DPSI_VM_HUGE_PAGE_MAX_COVERAGE=1` (off by default)

This gate buys more coverage for memory, and it is compiled out unless defined.
It adds a member to `mem_mapping`, so it must be defined for the whole program,
not per translation unit. With it, storage that asks for huge pages is also:

- **sized in whole PMDs.** At creation, the storage is rounded up to a whole
  number of 2 MiB spans, so its last span can be huge too. The cost is up to one
  span of capacity per pool. Under the advice, that span is backed by a huge
  page as soon as it is touched.
- **advised when it grows into its first PMD span**, with that span collapsed
  synchronously (`MADV_COLLAPSE`, Linux 6.1+). This happens at most once per
  pool, for at most one span. It costs a synchronous copy of about 1 ms per
  span, plus a check on every growth, which is why it is compiled out by
  default.

Measured on 900 memfd pools of 8 KiB to 6 MiB, built by a bulk insert and then
grown 10 % (kernel 6.6):

| | PMD mapped | shmem held |
|---|---|---|
| no advice | 0 MiB | 750 MiB |
| advice after construction | 46 MiB | 792 MiB |
| advice before the first touch | 356 MiB | 908 MiB |
| + whole-PMD sizing and collapse | 682 MiB | 911 MiB |

On kernels that do not allocate a huge folio for a partly covered span (6.12,
7.2), advice without the sizing costs no extra memory, and the sizing is what
costs it.

## When it pays

Huge pages pay when a workload is bound by page walks. A b+tree descent is a
chain of dependent loads, one per level, into nodes scattered across the pool.
Once the pool is well past the TLB's reach, each of those loads also pays a
page walk, and 2 MiB pages cut the number of translations by 512x.

- **Keys compared inside the node** (`int` keys, 7.65 M of them, 512 B and
  4 KiB nodes, anonymous or memfd pool, Intel and AMD, kernel 7.0): lookups
  −8..−21 %, bulk insert −15..−21 %.
- **An indirect comparator** (each key loaded from elsewhere, e.g. an index of
  row ids ordered by a column), in the same benchmark: −1..−7 %, mostly within
  noise.
- **A workload whose b+tree indexes use indirect comparators and 4 KiB
  nodes** (kernels 6.12 and 7.2): no measurable speed change, whatever the
  pool-size threshold for the advice and with 64 KiB mTHP folios for smaller
  pools. The descent's time goes to the indirect value loads, not to
  translations. Full coverage still cost memory there, through the whole-PMD
  sizing; the advice alone did not.

So ask for huge pages when descents are dominated by the tree's own loads and
the pool is large. Measure before asking when they are dominated by loads the
comparator makes elsewhere.
