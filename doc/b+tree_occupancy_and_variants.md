# psi::vm b+tree — node occupancy, and which B-tree variant to be

Notes behind the occupancy work on `psi::vm::bp_tree`: what fill a b+tree
actually reaches, what the classical variants do about it, what shipping
systems actually do, and which of those are worth having here.

---

## 1. What occupancy a plain split-on-full b+tree reaches

Measured by `test/b+tree_space.cpp`, 8M `std::uint32_t` keys, deterministic
fill, identical results on x86-64 Linux and Windows:

| insertion pattern | 512-byte nodes | 4096-byte nodes |
|---|---|---|
| random, one by one | **69.5 %** | **72.8 %** |
| ascending, one by one | **50.0 %** | 50.0 % |
| descending, one by one | **50.0 %** | 50.0 % |
| bulk (`insert_presorted`) | 100 % | 100 % |
| batched merge landing past the max | 99.9 % | 99.6 % |
| batched merge, interleaved | 69.3 % | 67.6 % |

Three things follow, and they are the whole basis for what was and was not
built:

1. **A split-on-full tree does not sit at its 50 % minimum under random
   insertion — it sits at ln 2 ≈ 69 %.** This is Yao's result and it is what
   the measurement reproduces. So the textbook framing "B\* raises the
   guarantee from 50 % to 66 %, therefore a third less memory" is wrong about
   the *average* case by a factor of about two.
2. **Sequential insertion is where the waste actually is** — exactly 50 %,
   both directions, because every split is a 50/50 split of a node that will
   never receive another key on one side.
3. **The bulk paths are already at capacity** and must not be disturbed. A
   fill *target* of 2/3 would make them worse, not better.

A tree that is only ever built is therefore not the interesting case. A tree
that is built and then modified decays back toward (1), and that is the state
any long-lived index is actually in.

---

## 2. The classical variants, and the naming mess

The term is more misused than used. Comer's 1979 survey opens on exactly this
point:

> Perhaps the most misused term in B-tree literature is B\*-tree. Actually,
> Knuth defines a B\*-tree to be a B-tree in which each node is at least 2/3
> full … employs a **local redistribution** scheme to delay splitting until 2
> sibling nodes are full. Then the 2 nodes are divided into 3, each 2/3 full.
>
> — D. Comer, *The Ubiquitous B-Tree*, ACM Computing Surveys 11(2), 1979

So a **true B\*** is three things at once, and they are not separable:

- a **2/3 minimum occupancy** invariant,
- **local redistribution** into a sibling before splitting,
- a **2-into-3 split** (and, necessarily, a 3-into-2 merge) to maintain it.

Comer notes in the same paragraph that "B\*-tree" is habitually applied to
what he then names the **B+tree**. That confusion is still live: it is why a
system whose documentation says "B\*-tree" usually is not one.

### What shipping systems actually do

Read at source rather than taken from secondary literature:

| system | on a full-node insert | minimum occupancy |
|---|---|---|
| **SQLite** (`balance_nonroot`) | gathers the page plus **up to 2 siblings** into one cell array, repacks left-to-right; page count grows *n*→*n*+1, capped at 5 | rebalance triggers around **1/3** (`nFree*3 <= usableSize*2`) |
| **Apple HFS/HFS+** | `InsertLevel` tries `RotateLeft` — equalise into the **left** sibling only — then falls back to `SplitLeft`, a plain 1→2 | none enforced |
| **PostgreSQL** (nbtree) | plain 1→2; the README states outright that moving items to a sibling "seems impractical". Rightmost-page heuristic leaves the left page `fillfactor` % full (default 90 leaf, 70 internal, 96 single-value) | **none** — pages are deleted only when completely empty |
| **InnoDB** | plain 1→2; on a detected ascending run splits at the insert point rather than the middle | — |
| **LMDB** | plain 1→2 at `(nkeys+1)/2`; `MDB_APPEND` biases the split so the new page is emptier | — |
| **Berkeley DB** | plain 1→2 at the byte midpoint, with an append/prepend special case | — |
| **WiredTiger** | reconciliation writes chunks of `split_pct` of the max page size (default 90) | — |
| **`absl::btree`** | tries the **left** sibling, then the **right**, before splitting; `to_move` targets the sibling's slack, biased by insert position | ordinary merge-when-they-fit |

**No true B\* among them.** Alhomssi & Leis reach the same conclusion
independently: *"We are not aware of any practical system that uses
B\*-Trees."* The two systems usually cited as counter-examples do not survive
reading their source: SQLite's floor is ~1/3, the *opposite* of B\*, and
Apple's is loose naming for a B+tree.

`absl::btree` is the closest thing shipping to what this container now does —
and it is worth being precise that it is **not** a B\* either: it enforces no
fill floor on the insert path, falls back to a plain 1→2 split, and biases
that split three ways on insert position.

### Measured cost of a real B\*

Alhomssi & Leis, *"Contention and Space Management in B-Trees"*, CIDR 2021.
20 GiB random insert, 8-byte keys, 100-byte values, 16 KiB pages:

| | total size | instructions/key | cycles/key |
|---|---|---|---|
| B-Tree | 31.3 GiB | 2870 | 3497 |
| **B\*-Tree** | 26.6 GiB | 3594 | **4580** |
| B-Tree + XMerge | **25.1 GiB** | 2995 | 3645 |

B\* buys **−15 % space for +31 % cycles**. Deferred merging (XMerge) beats it on
**both** axes. The paper's own explanation: *"the B\*-Tree produces a compact
tree but requires many more instructions … because of the frequent
rebalancing between neighboring nodes."*

---

## 3. Why this container is not becoming a B\*

Three independent reasons, any one of which is sufficient.

### 3.1 It would be a downgrade

Local redistribution alone (§4) measures **87–90 %** on random insertion and
**99 %** on sequential — *above* B\*'s ~81 % asymptote (2·ln(3/2)). The only
thing the 2/3 invariant adds on top is an **erase-side floor**: after erasing
half the keys the redistributing tree decays to ~65 %, where a true B\* would
hold ≥66 %. That is a narrow benefit for what it costs.

### 3.2 `min_values = ceil( max / 2 )` is not a constant that can be raised

It is exactly what makes

```
2 * min_values <= max_values + 1
```

true, and *that* is what makes "either a sibling can lend a value, or the two
of them merge into one node" true — the property the entire underflow half of
the tree rests on: `handle_underflow`, `merge_right_into_left`,
`append_and_free`, and the bulk-fill partitions written as `min_values * 2`.

`ceil( 2m/3 )` breaks it for every m ≥ 4, and **the failure is silent**, not
an assertion: `merge_right_into_left` writes past the node before it asserts,
because `move_chldrn` bounds `count` and `tgt_begin` separately and never
their sum. A witness is a full 124-key predecessor merged with a 1-key
trailing leaf.

A 2/3 floor therefore does not "raise a constant" — it obliges a **3-into-2
merge** and a re-statement of this bound. The inequality is now a
`static_assert` on both node types so that this cannot be discovered at
runtime.

### 3.3 The external evidence points elsewhere

§2 above: nobody ships it, and the one careful measurement of it finds
deferred compaction strictly better on both axes.

---

## 4. What was built instead: local redistribution on overflow

Before splitting, an overflowing leaf hands values to a same-parent sibling
that still has room. This is Comer's **local redistribution**, without the
2/3 invariant and without the 2-into-3 split — so it is *not* a B\*, and the
hybrid has no established name in the literature. The clearest description of
it is structural: it is the exact **dual of `handle_underflow`'s borrow
branches**, and it is written with the same idioms.

Three details are load-bearing:

- **Sibling existence is resolved from `parent_child_idx`, never from the
  level links.** `left`/`right` are level links and cross parents; borrowing
  across a parent boundary would corrupt the separator relationship.
- **The separator follows the values**, exactly as it does when the flow is
  the other way.
- **Half the room found is taken, not all of it.** A sibling emptied of its
  slack merely moves the next split one node over, and a sibling left *full*
  would have to be split by the very insertion being relieved.

**Leaves only, deliberately.** A node's children carry a back-index into
their parent, so relocating an inner node's children re-indexes and dirties
every one of them — more expensive than the split it would save. Leaves are
the overwhelming majority of nodes at any realistic fanout, so that is where
the occupancy is.

| pattern | before | after | leaf bytes/key |
|---|---|---|---|
| random, 512-byte nodes | 69.5 % | **87.1 %** | 5.94 → 4.74 |
| sequential | 50.0 % | **99.2 %** | 8.26 → 4.16 |
| random, 4096-byte nodes | 72.8 % | **89.9 %** | 5.52 → 4.46 |
| merge, interleaved (4096) | 67.6 % | **88.2 %** | 5.94 → 4.55 |
| bulk / appended merge | 100 % / 99.9 % | unchanged | — |

Confirmed on the **resident node pool**, not just leaf bytes — `nodes_used()`
and `nodes_reserved()` exist for this: pool bytes/key 6.09 → 4.87 random,
8.53 → 4.30 sequential. The pool tracks leaf occupancy to within ~2.3 % (the
inner levels).

On a downstream workload replayed from a real modification history — rather
than a freshly bulk-built tree — occupancy decays to the 65–68 % §1 predicts
and this recovers it to 81–88 %.

### Cost

Inserts are **neutral to faster** everywhere measured. The single measured
regression is `find()` in the **512-byte** configuration, 1.08×: there
`use_linear_search_for_sorted_array` selects a linear scan
(124 × 4 = 496 bytes ≤ the 2048-byte limit), and a fuller leaf is a longer
scan. At 4096 bytes the same array is 4080 bytes, the search is
`std::lower_bound`, and the same measurement is **0.95×** — faster, because
the tree is physically smaller.

That regression is a property of **occupancy**, not of this policy: a
bulk-built tree, at ~100 %, pays exactly the same thing.

The real cost is elsewhere and is structural: **relieving leaves a node
nearly full, so it overflows again soon**, where a split leaves two half-empty
nodes that will not. The overflow *event* count rises several-fold; each event
is individually cheaper. `to_move = room / 2` is the tuning knob.

---

## 5. Giving memory back

Everything above is about how full a node is kept while the tree is being
built and modified. Neither half of the tree ever hands memory back: erasure
frees a node only once two siblings together fit into one — which leaves a
thinned-out tree well above half full and therefore unmerged — and a freed
node goes on a free list, resident, in a pool that never shrinks.

### 5.1 `release_free_nodes()`: dropping the pages of free nodes

A node is a fixed slot of a contiguous pool, addressed by its index, so the
pages under a free node can be handed back to the OS where they are, without
renumbering anything (pool compaction - relocating nodes to the front and
truncating - would have to rewrite every link that names one). What the call
takes is **whole pages**:

- with page sized nodes (`PSI_VM_BT_PAGE_SIZED_NODES`) that is every free
  node;
- with smaller ones only a page whose nodes are *all* free: runs of
  neighbouring free nodes, such as a range erase of a sequentially built
  tree leaves, but hardly ever what scattered merges leave (§5.2).

A released node leaves the free list. The list is threaded through the free
nodes themselves, and a dropped page reads back as zeros - which is node 0,
not a null link. So released nodes are kept in a sorted set beside the pool,
which `new_node()` takes from once the free list is empty (before the pool
grows), and which `reserve_additional()` puts back on the list for the bulk
paths that walk it; a node taken back is reinitialised, and its page faults in
again on the first write. The set is not persisted.

How the pages are dropped depends on who else maps them:

| storage | mechanism | refused when |
|---|---|---|
| file backed | — (a file's pages are its data) | always |
| a private view: Linux `map_memory()`, any Linux COW clone | `MADV_DONTNEED` (drops this view's pages only) | never |
| Linux `map_cow_memory()`: a shared memfd | `MADV_REMOVE` (punches the pages out of the memfd) | a COW clone of the tree is alive |
| Windows: a pagefile backed section | `DiscardVirtualMemory` | a clone is alive; a copy-on-write view rejects it outright |
| macOS: shared anonymous memory | `MADV_FREE_REUSABLE` | a clone is alive |

A tree and every clone made of it share a reference count, which is how a
shared view tells that dropping its pages would pull them from under a clone.
`commit_to()` carries a clone's released set over along with its header; a
node the target still holds resident goes back on its free list instead.

It is explicit rather than done by `free()`: a system call per freed node
would land on the erase path, and only a batch can see which pages are
entirely free. On a huge page backed pool a partial release splits the huge
page.

Measured (`bp_tree.benchmark_compact`, x86-64 Linux, clang, 4M `std::uint32_t`
keys built one at a time in random order, then a share erased at random -
erasure alone, before any compaction):

| erased | 4096-byte nodes: free nodes released | cost per node | 512-byte nodes: released of free |
|---|---|---|---|
| 50 % | 1534 of 1534 | 0.83 µs | 8 of 12558 |
| 70 % | 2520 of 2520 | 0.67 µs | 536 of 22120 |

The cost is the system call (one per run of neighbouring pages) plus the
page's zeroing on its next fault. With 512-byte nodes a page holds eight, and
erasure leaves them free in ones and twos.

### 5.2 `compact()`: XMerge

Freeing a node out of a group of X siblings at fill *f* needs

```
X >= 1 / ( 1 - f )
```

The classic pairwise merge is X = 2, and needs both siblings at or below
half full. At the 60–90 % a modified tree sits at that almost never happens,
which is why the tree stays there however much of it has been erased.
XMerge (Alhomssi & Leis, CIDR 2021, from LeanStore) merges X neighbouring
siblings into X − 1 as soon as their summed free space is a whole node, and
with fixed size entries that test is the entry counts alone: a group that
does not qualify costs the reads of its headers.

`compact()` applies it to the whole tree, on request:

- **Groups of up to 8 siblings under one parent** (X ≤ 8, so a node is freed
  wherever the fill is under 87.5 %), each merged group's entries spread
  evenly over the X − 1 nodes that remain. The first node of a group keeps
  its first entry, so the separator above it - which can be in any ancestor
  - stays valid; every other separator involved is in the parent.
- **Bottom-up**, the leaves' parents first, then each level above.
- **A parent never drops below its minimum.** LeanStore's nodes have no
  minimum fill; here the underflow half of the tree depends on one (§3.2), so
  a parent may only lose the children it can spare, and the root all but one
  (after which it hands over to that one). A parent at its minimum therefore
  blocks the merges below it until its own level has been merged, which
  refills it - so the levels are swept again until a sweep frees nothing.
  Every step of a sweep leaves a valid tree; nothing is repaired afterwards.
- **A group that does not merge is not written.** A COW clone's
  `commit_to()` copies what was written, so a compaction that finds little
  to do costs a commit little.
- Then `release_free_nodes()` (§5.1).

Differences from LeanStore: the trigger (there, a node is merged when the
buffer manager evicts it; this container has no buffer manager, so the call
is explicit), the group size, the minimum fill it keeps, and the repeated
sweeps. An amortised scan on the erase path was not built: erasure is
`noexcept` and on the hot path, a scan there reads up to eight siblings'
headers per erasure to free a node rarely, and a single explicit pass sees
the whole tree - where it is worth doing (after bulk erasure, before a
snapshot) is the caller's knowledge, not the container's.

Same benchmark as §5.1, the other copy of the tree compacted instead:

| erased | node size | leaf fill | nodes in use | resident pages | `compact()` | `find()` |
|---|---|---|---|---|---|---|
| 30 % | 4096 | 63.1 → 92.9 % | 4364 → 2963 | 4386 → 2963 | 4.4 ms, 1.0 µs/node | −35 % |
| 50 % | 4096 | 69.0 → 93.7 % | 2852 → 2099 | 4386 → 2099 | 3.1 ms, 1.1 µs/node | −14 % |
| 70 % | 4096 | 63.3 → 94.6 % | 1866 → 1247 | 4386 → 1247 | 3.3 ms, 1.8 µs/node | −13 % |
| 30 % | 512 | 62.3 → 94.5 % | 37251 → 24320 | 4752 → 4751 | 7.1 ms, 0.19 µs/node | −11 % |
| 50 % | 512 | 65.1 → 94.3 % | 25451 → 17393 | 4752 → 4713 | 5.9 ms, 0.23 µs/node | −10 % |
| 70 % | 512 | 62.5 → 94.8 % | 15889 → 10389 | 4752 → 4401 | 5.5 ms, 0.35 µs/node | −1 % |

(cost per node in use before the call, its release included; `find()` is a
random lookup of every remaining key, best of three blocks per arm with the
order alternated.) With page sized nodes the resident pool follows the nodes
in use down. With 512-byte nodes the tree shrinks by a third to a half and is
faster to search, but hardly any page is left with all eight of its nodes
free, so resident memory barely moves: getting it back there would take
moving nodes, which §5.1 rules out.

On Windows `DiscardVirtualMemory` costs 10–20 µs a page, an order of
magnitude more than `madvise`, and dominates `compact()` there.

## 6. Open

- **Devector nodes.** `node_header` carries `start`, where a node's live
  entries begin, and every accessor honours it; nothing opens a gap yet. The
  step that pays is `handle_underflow`'s borrow and §4's relief handing entries
  over at the front instead of moving the whole node: both currently carry a
  **full-node** memmove to relocate `to_move` entries, and `to_move` is
  smallest exactly when the policy fires most.
- **Map support.** The prerequisites have landed: entries move and shift as
  whole entries rather than as keys, and intra-node search takes the searched
  node's own capacity rather than the leaf's — the latter is a latent
  miscompile the moment a leaf holds fewer entries than an inner node, which
  a map causes on day one.

## References

- D. Comer, *The Ubiquitous B-Tree*, ACM Computing Surveys 11(2), 1979.
- D. Knuth, *TAOCP* Vol. 3, §6.2.4 (B\* definition; cited via Comer).
- A. Alhomssi, V. Leis, *Contention and Space Management in B-Trees*, CIDR 2021 (XMerge).
- A. C. Yao, *On random 2-3 trees*, Acta Informatica 9, 1978 (the ln 2 result).
