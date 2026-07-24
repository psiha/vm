#if !defined( _WIN32 ) && !defined( _GNU_SOURCE )
#   define _GNU_SOURCE // sync_file_range()/syncfs() are GNU extensions
#endif

#include <psi/vm/mapped_view/flush_manifest.hpp>

#include <psi/vm/mapped_view/ops.hpp>

#include <psi/err/errno.hpp>

#include <boost/assert.hpp>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <tuple>

#if __has_include( <unistd.h> )
#   include <fcntl.h>
#   include <unistd.h>
#else
#   include <psi/vm/detail/nt.hpp>
#   include <psi/err/win32.hpp>
#endif
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

#ifndef NDEBUG
bool sync_all_force_failure{ false }; // declared in flush_manifest.hpp
#endif

void flush_manifest::add( native_handle_t const handle, mapped_span const dirty_range ) PSI_NOEXCEPT_EXCEPT_BADALLOC
{
#ifdef __linux__
    if ( syncfs_armed() ) {
        // syncfs(2) barrier: files_ is never read and the write-back kick would
        // be a documented no-op - see the declaration's comment. Keep the
        // registration in debug builds only, for sync_all()'s closed-before-
        // the-barrier check.
#   ifndef NDEBUG
        files_.push_back( handle ); // may throw bad_alloc - see the declaration's comment
#   else
        std::ignore = handle;
#   endif
        return;
    }
#endif
    // Fire-and-forget: kicks off write-back now so device I/O overlaps the rest
    // of this phase's CPU work. Durability is established later, by sync_all() -
    // this call's outcome is deliberately not observed.
    flush_async( dirty_range );
    files_.push_back( handle ); // may throw bad_alloc - see the declaration's comment
}

#if __has_include( <unistd.h> )
#   if defined( __APPLE__ )
namespace {
    // F_BARRIERFSYNC orders writes (everything before it is guaranteed to reach
    // media before anything written after it) without itself forcing an
    // immediate device-cache flush - cheaper than F_FULLFSYNC per file; the
    // actual device-cache flush is left to the direct header flush the caller
    // issues immediately after each barrier.
    bool barrier_fsync( int const fd ) noexcept {
        if ( ::fcntl( fd, F_BARRIERFSYNC ) != -1 ) [[ likely ]]
            return true;
        return ::fsync( fd ) == 0; // F_BARRIERFSYNC unavailable (e.g. some network filesystems): fall back
    }
} // anonymous namespace
#   endif
#endif

namespace {
    // Records at most the first underlying failure's platform error (best
    // diagnostic value for the least bookkeeping - every file in a barrier is
    // still attempted regardless, matching a "best-effort every file"
    // semantics) and throws it as a real std::runtime_error so callers get the
    // actual OS error text instead of a bare pass/fail.
    struct failure_tracker {
        bool                     ok{ true };
        std::optional<vm::error> first_error;

        void record( bool const succeeded ) noexcept {
            if ( succeeded ) [[ likely ]] return;
            ok = false;
            // emplace(), not assignment: vm::error (last_errno/last_win32_error)
            // carries a `value_type const value` member, which disables copy/
            // move assignment - only construction works.
            if ( !first_error ) first_error.emplace();
        }

        [[ noreturn ]] void throw_if_failed() const {
            if ( first_error ) throw err::make_exception( *first_error );
            throw std::runtime_error( "commit: device flush barrier failed" );
        }
    }; // struct failure_tracker
} // anonymous namespace

void flush_manifest::sync_all()
{
    // Dedupe: the same file can genuinely be registered more than once per
    // commit, by design - e.g. a metadata flush that runs once per changed
    // referent, so a file referenced by several referents changed in one
    // transaction registers that many times. The sort+unique here is a few
    // microseconds of in-memory work against what it saves: one redundant
    // fdatasync()/FlushFileBuffers() call - each orders of magnitude more
    // expensive than the dedupe pass - per extra reference to the same file.
    // Skipped in syncfs mode, which issues no per-file call for the dedupe to
    // save: there files_ is only ever populated (debug builds) to feed the
    // closed-before-the-barrier check below, which duplicates do not affect.
#ifdef __linux__
    if ( !syncfs_armed() )
#endif
    {
        std::ranges::sort( files_ );
        files_.erase( std::unique( files_.begin(), files_.end() ), files_.end() );
    }

#ifndef NDEBUG
    if ( sync_all_force_failure ) [[ unlikely ]] {
        files_.clear(); // matches the real failure path's post-condition below
        throw std::runtime_error( "commit: device flush barrier failed (forced)" );
    }
#endif

#ifndef NDEBUG
    // A handle registered with add() must still be open when sync_all() runs:
    // registration only records the handle, so closing the file in between
    // leaves this barrier fsync()ing a stale descriptor and silently drops that
    // file's durability guarantee (or, once the number is recycled, flushes an
    // unrelated file instead). See flush_manifest.hpp's hazard note.
    for ( auto const h : files_ ) {
#   if __has_include( <unistd.h> )
        BOOST_ASSERT_MSG( ::fcntl( h, F_GETFD ) != -1, "flush_manifest::sync_all: a registered handle was closed before the barrier" );
#   else
        BOOST_ASSERT_MSG( ::GetFileType( h ) != FILE_TYPE_UNKNOWN, "flush_manifest::sync_all: a registered handle was closed before the barrier" );
#   endif
    }
#endif

    failure_tracker failure;

#if defined( __linux__ )
    if ( syncfs_fd_ >= 0 ) {
        failure.record( ::syncfs( syncfs_fd_ ) == 0 );
    } else {
        for ( auto const fd : files_ ) { ::sync_file_range( fd, 0, 0, SYNC_FILE_RANGE_WRITE ); } // start write-back on all (overlap)
        for ( auto const fd : files_ ) { failure.record( ::fdatasync( fd ) == 0 ); }             // wait + device flush each
    }
#elif defined( __APPLE__ )
    for ( auto const fd : files_ ) { failure.record( barrier_fsync( fd ) ); }
#elif __has_include( <unistd.h> )
    for ( auto const fd : files_ ) { failure.record( ::fdatasync( fd ) == 0 ); }
#else // Windows
    ::IO_STATUS_BLOCK iosb;
    for ( auto const h : files_ ) {
        // FILE_DATA_SYNC_ONLY restricts this to the data plus the metadata
        // actually needed to retrieve it (the size of a just-extended file) -
        // fdatasync()'s contract, i.e. the same thing the Linux branch above
        // asks for, and everything recovery needs. It is only the timestamps
        // and the like that it lets us skip, but on a file that has just been
        // extended - which in a commit is all of them - that is most of the
        // metadata work: 109.7 vs 188.2 us per flush measured on NTFS.
        // NO_SYNC keeps the device-global hardware cache flush out of the loop;
        // the single call below issues it once for everything.
        failure.record( nt::NtFlushBuffersFileEx( h, FLUSH_FLAGS_NO_SYNC | FLUSH_FLAGS_FILE_DATA_SYNC_ONLY, nullptr, 0, &iosb ) >= 0 );
    }
    if ( !files_.empty() ) {
        // Single device-cache flush covering everything just written above (the
        // device cache is device-global, not per-handle - see
        // FlushFileBuffers()'s own docs distinguishing a file handle from a
        // volume handle: the block layer can't tell which sectors belong to
        // which file, so the only way to honor a flush request is to flush
        // everything to media).
        // Flags 0 is that same full flush through the NT entry point already
        // used above rather than the Win32 wrapper over it - measured 927.8 vs
        // 1037.4 us, both an order of magnitude above the NO_SYNC calls, which
        // is what issuing the hardware flush looks like.
        //
        // PRECONDITION: every registered handle lives on the SAME volume.
        // "Device-global" is per device: this one call reaches the media behind
        // files_.back()'s volume and no other, so a manifest spanning volumes
        // would leave every file on the other volumes with its NO_SYNC write-
        // back merely queued and never barriered - and silently, since each
        // NO_SYNC call above still reports success. A store living in one
        // directory tree is on one volume; this asserts that rather than
        // assuming it.
#   ifndef NDEBUG
        {
            ::BY_HANDLE_FILE_INFORMATION refInfo;
            if ( ::GetFileInformationByHandle( files_.back(), &refInfo ) ) {
                for ( auto const h : files_ ) {
                    ::BY_HANDLE_FILE_INFORMATION info;
                    if ( ::GetFileInformationByHandle( h, &info ) ) {
                        BOOST_ASSERT_MSG
                        (
                            info.dwVolumeSerialNumber == refInfo.dwVolumeSerialNumber,
                            "flush_manifest::sync_all: registered handles span volumes - the single closing "
                            "device-cache flush only barriers one of them"
                        );
                    }
                }
            }
        }
#   endif
        failure.record( nt::NtFlushBuffersFileEx( files_.back(), 0, nullptr, 0, &iosb ) >= 0 );
    }
#endif

    files_.clear();
    if ( !failure.ok ) [[ unlikely ]] failure.throw_if_failed();
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
