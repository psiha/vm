#pragma once

#include <psi/vm/containers/heap_vector.hpp>
#include <psi/vm/mappable_objects/file/handle.hpp>
#include <psi/vm/span.hpp>

#include <boost/assert.hpp>

#include <cstdint>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// Batches the many per-file device flushes a transaction/commit against a
// memory-mapped store needs into a small, fixed number of barrier syscalls,
// instead of one blocking flush per dirty file (a workload writing thousands
// of small files per commit collapses to a handful of device-flush calls).
// add() registers a file's dirty range and kicks off asynchronous write-back
// so device I/O overlaps the rest of that phase's CPU work; sync_all() is the
// barrier - a single blocking pass that waits for every registered file's data
// to actually reach the device. See flush_manifest.cpp for the platform-
// specific batching strategy.
//
// Intended lifetime: a long-lived object reused across every commit (kept as a
// member of the owning store), not a per-commit local - files_ then keeps
// whatever capacity the busiest commit ever needed instead of re-growing from
// empty every time.
//
// sync_all() throws (rather than aggregating every underlying flush's outcome
// into a bool) so the first real OS error reaches the caller with an actual
// message instead of a bare pass/fail the caller can only translate into a
// generic "the barrier failed".
//
// Hazard class this deferral introduces (audit for it whenever touching a
// commit-path flush() call site in the owning store): with a manifest active,
// flush() only *registers* the file and returns - the actual device flush is
// deferred to the next sync_all() barrier - so any code that flushes an object
// and then closes/destroys it *before* that barrier runs leaves a dangling
// registration, and sync_all() then tries to fsync an already-closed handle
// (see the debug-only "closed before the barrier" check below). Without a
// manifest every flush() is synchronous and immediately durable, so
// "flush a file, then close it" is order-independent; the batching is what
// makes ordering matter.
#ifndef NDEBUG
// Test-only fault-injection seam: when set, sync_all() fails immediately
// (before issuing any real device flush), exercising the failure-propagation
// path a genuine device error during the barrier would take - unlike
// flush_force_failure (ops.hpp), which gates flush_blocking() specifically and
// does not intercept sync_all()'s direct fdatasync()/FlushFileBuffers() calls.
// Deliberately not thread_local: tests using this are expected to be single-
// threaded for the duration they set it.
extern bool sync_all_force_failure; // defined in flush_manifest.cpp
#endif

class flush_manifest
{
public:
    using native_handle_t = file_handle::native_handle_t;

    flush_manifest            ( flush_manifest const & ) = delete;
    flush_manifest & operator=( flush_manifest const & ) = delete;

    // syncfs(2) mode (Linux): if a filesystem fd is supplied at construction, a
    // single ::syncfs() on it replaces the per-file fdatasync loop entirely.
    // syncfs is only *sufficient* when the filesystem holding the store is
    // dedicated to it: it is always correctness-safe (it flushes strictly more
    // than needed), but on a shared mount it also waits on every unrelated
    // writer's dirty data, so commit latency becomes a function of what else
    // lives on that mount. "Is this mount dedicated" is a deployment fact no
    // runtime probe can establish, so the decision is the caller's - it passes
    // the fd only when it wants syncfs mode. No fd (fs_fd < 0) => the per-file
    // path. On non-Linux platforms the argument is ignored (there is no
    // syncfs).
    //
    // The fd is NOT owned: the caller must keep it open for at least as long as
    // this manifest lives (a stale/closed fd is a silent-durability hazard -
    // fd numbers are recycled, so a later ::syncfs() could flush an unrelated
    // filesystem and report success). Because syncfs operates on the mount, not
    // the directory inode, an fd captured once remains correct even if the
    // directory it names is renamed or replaced, as long as the store stays on
    // the same mount.
    explicit flush_manifest( [[ maybe_unused ]] int const fs_fd = -1 ) noexcept
#ifdef __linux__
        : syncfs_fd_{ fs_fd }
#endif
    {}

    // Registers a file for the next barrier and starts its write-back now
    // (best-effort, fire-and-forget - actual durability is established later by
    // sync_all(), not by this call).
    //
    // In syncfs mode both halves of that are pointless and are skipped: the
    // barrier syncs the whole filesystem and never reads files_, and the write-
    // back kick would be an MS_ASYNC msync(), documented as a no-op on Linux.
    // The registration is retained in debug builds only, to keep sync_all()'s
    // closed-before-the-barrier check covering this (default) path.
    //
    // May throw std::bad_alloc if files_ can't grow to hold the registration -
    // deliberately not salvaged here: a commit that can't even register a flush
    // target is abandoned by the caller's existing unwind path, and there is no
    // durability value in finishing this one file's flush anyway (the write it
    // belongs to is about to be discarded).
    void add( native_handle_t handle, mapped_span dirty_range ) PSI_NOEXCEPT_EXCEPT_BADALLOC;

    // The barrier: deduplicates registered handles, issues the platform batched
    // device-durable sync, then clears the manifest for reuse. Throws on the
    // first underlying flush failure (see flush_manifest.cpp).
    void sync_all();

    [[ nodiscard ]] bool empty() const noexcept { return files_.empty(); }

#ifdef __linux__
    [[ gnu::pure ]] bool syncfs_armed() const noexcept { return syncfs_fd_ >= 0; }
#endif

    // Ambient "which manifest, if any, is currently batching this store's commit
    // flushes" pointer. A plain (not thread_local) pointer: the intended use is
    // a single-writer commit path that never dispatches flush work onto another
    // thread. flush() call sites with no direct handle back to the owning store
    // read this to learn a manifest is active. A static member of the class
    // rather than a free variable: it IS this class's concept ("which instance
    // is active").
    inline static flush_manifest * active{ nullptr };

    // RAII scope guard for `active`. Scoping the set/clear (rather than nulling
    // manually) guarantees `active` is reset on stack unwind before an exception
    // reaches a rollback path - so rollback-time flushes never silently defer
    // into a manifest that nothing will subsequently sync.
    //
    // Nesting is not supported and does not occur: the intended commit path is
    // single-writer and never opens a second scope while one is live (no
    // recursive commit), so `active` is always null on entry - clearing to null
    // on exit is therefore correct, and the debug assert enforces the
    // no-nesting invariant this relies on.
    struct scope
    {
        explicit scope( flush_manifest * const m ) noexcept
        {
            BOOST_ASSERT_MSG( !active, "flush_manifest::scope: a manifest scope is already active (nesting is not supported)" );
            active = m;
        }
        ~scope() noexcept { active = nullptr; }

        scope            ( scope const & ) = delete;
        scope & operator=( scope const & ) = delete;
    }; // struct scope

private:
    // 32-bit size: the registration count is bounded by the dirty files in one
    // commit (thousands at most), never near 2^32.
    heap_vector<native_handle_t, std::uint32_t> files_;
#ifdef __linux__
    int syncfs_fd_{ -1 }; // >=0 => dedicated-volume syncfs mode
#endif
}; // class flush_manifest

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
