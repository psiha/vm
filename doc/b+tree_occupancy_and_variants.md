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

B\* buys **−15 % space for +31 % cycles**. Deferred merging (§5) beats it on
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
is individually cheaper. `to_move = room / 2` is the tuning knob, and §5.1 is
what makes each event cheap enough for that to be unambiguously worth it.

---

## 5. Worth taking from LeanStore

### 5.1 XMerge — and the arithmetic that explains §1

Freeing one node out of a group of X at fill *f* requires

```
X >= 1 / (1 - f)
```

Classic **pairwise** merge is the X = 2 case, and needs *both* siblings at
≤50 %. At the 65–88 % a real tree sits at, that condition is essentially
never met — **which is precisely why the tree stays there**. Occupancy is not
a tuning failure; the merge rule cannot reach it.

XMerge merges groups of X immediate siblings into X−1, beginning once their
*summed* free space exceeds one node. LeanStore's default K = 5 covers
f ≤ 80 %; reaching 88 % wants K ≈ 9. `mergeSpaceUpperBound` collapses, for
fixed-size entries, to `count_a + count_b <= capacity`, so the group scan
touches only node headers.

Its **trigger does not transfer** — it hangs off buffer-manager eviction,
which this container does not have. Here it wants an explicit `compact()`
entry point and/or an amortised erase-side scan.

### 5.2 Returning freed nodes to the OS

A freed node returns a 32-bit index to a free list; resident memory is
unchanged. But a node is **exactly one page** in a contiguous pool, so
`madvise(MADV_DONTNEED)` / `MADV_FREE` on a freed node drops resident memory
**without renumbering a single `node_slot`**. Pool compaction — renumbering
every 32-bit index — is the invasive alternative and is not worth it.

### 5.3 Key heads, reinterpreted

LeanStore stores the first 4 key bytes inline in each slot and compares those
first, skipping the full comparison entirely when the key is ≤4 bytes. The
literal technique is inapplicable to a container whose keys are already
inline scalars.

The transferable form is for a consumer whose keys are **indices** and whose
ordering is **indirect** — a comparison is a random load into a separate
array. Caching the ordering value beside the index makes intra-node
comparison a register compare. It roughly doubles the leaf entry, so fan-out
halves — but **tree depth does not change** for realistic sizes, since
`ceil( log_510 n ) == ceil( log_1020 n )`. Two preconditions: the ordering
value's width (≤4 bytes makes the cached copy exact rather than a prefix),
and invalidation if a value can change while its index stays in the tree.

Note the tension: 5.3 grows the leaf while 5.1 shrinks the tree. The case for
5.3 is **latency**, not space.

### 5.4 Search hints

16 evenly spaced key heads in one cache line, scanned to narrow the binary
search's range before it starts (`_mm512_cmpge_epu32_mask` where available).
Worth ~25 % on integer lookups in LeanStore, but only meaningful once 5.3
exists — with no cached value there is nothing to sample. Second-order here.

### 5.5 Not applicable

- **Prefix compression** — index keys have no lexicographic prefix, and their
  bytes are not the ordering.
- **Slotted-heap node layout** — fixed-size entries need no slot indirection,
  no `data_offset`, no fragmentation, no `compactify()`.
- **Contention split, optimistic lock coupling, pointer swizzling, the buffer
  manager** — all exist to solve latch contention or page residency. This
  container has no synchronisation and is memory-mapped.
- **"Adaptive node sizes"** — not actually a LeanStore technique; its 16 KiB →
  4 KiB move was driven by NVMe write amplification.

---

## 6. Open

- **Devector nodes.** `node_header` carries a `start` TODO: *"make keys and
  children arrays function as devectors: allow empty space at the beginning to
  avoid moves for smaller borrowings."* It was written for
  `handle_underflow`'s borrow; §4's relief is its exact dual, so it pays
  twice. Both directions currently carry a **full-node** memmove to relocate
  `to_move` entries, and `to_move` is smallest exactly when the policy fires
  most. Header cost is alignment, not the field: 16 bytes is already a
  multiple of `alignof( node_slot )`, so any added member rounds to 20 —
  unless `num_vals`, `start`, `parent_child_idx` and `dirty` share one 32-bit
  unit (30 bits suffice at 4096-byte nodes with 4-byte keys), which costs no
  capacity but makes `num_vals` a bit extract.
- **XMerge / `compact()`** (§5.1) and **page release** (§5.2).
- **Map support.** The prerequisites have landed: entries move and shift as
  whole entries rather than as keys, and intra-node search takes the searched
  node's own capacity rather than the leaf's — the latter is a latent
  miscompile the moment a leaf holds fewer entries than an inner node, which
  a map causes on day one.

## References

- D. Comer, *The Ubiquitous B-Tree*, ACM Computing Surveys 11(2), 1979.
- D. Knuth, *TAOCP* Vol. 3, §6.2.4 (B\* definition; cited via Comer).
- A. Alhomssi, V. Leis, *Contention and Space Management in B-Trees*, CIDR 2021.
- V. Leis et al., *LeanStore*, ICDE 2018; *The Evolution of LeanStore*, BTW 2023;
  *B-Trees Are Back*, SIGMOD 2025.
- A. C. Yao, *On random 2-3 trees*, Acta Informatica 9, 1978 (the ln 2 result).
