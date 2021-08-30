////////////////////////////////////////////////////////////////////////////////
///
/// \file posix/mem.inl
/// -------------------
///
/// Copyright (c) Domagoj Saric 2015 - 2021.
///
/// Use, modification and distribution is subject to the
/// Boost Software License, Version 1.0.
/// (See accompanying file LICENSE_1_0.txt or copy at
/// http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef __ANDROID__
#include "mem.hpp"

#include "boost/mmap/detail/posix.hpp"

// semaphores
#include <semaphore.h>
#include <sys/sem.h>
#include <sys/ipc.h>

#include <cstdint>
#include <cstring>
#include <string_view>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
BOOST_MMAP_POSIX_INLINE
namespace posix
{
//------------------------------------------------------------------------------

namespace detail
{
    // http://man7.org/linux/man-pages/man2/memfd_create.2.html
    // http://stackoverflow.com/questions/13377982/remove-posix-shared-memory-when-not-in-use
    // http://rhaas.blogspot.hr/2012/06/absurd-shared-memory-limits.html

    BOOST_IMPL_INLINE BOOST_ATTRIBUTES( BOOST_MINSIZE )
    named_semaphore::named_semaphore
    (
        char                                            const * const name,
        flags::access_privileges               ::system         const access_privileges,
        flags::named_object_construction_policy                 const creation_disposition
    ) noexcept
        :
        semid_( -1 )
    {
        /// \note No robust mutexes on Android and OSX, boost::hash does not
        /// give the same value in 32 bit and 64 bit processes (and we want to
        /// support mixing those in IPC), and the default prescribed ain't so
        /// pretty either (http://nikitathespider.com/python/shm/#ftok)...so
        /// what's a poor programer to do?.
        ///                                   (10.11.2015.) (Domagoj Saric)
        ::key_t sem_key;
        {
            /// \note Allow a prexisting key file regardless of the specified
            /// creation_disposition to handle zombie files from killed/crashed
            /// processes. The opening/creation of the semaphore object should
            /// properly handle the specified creation_disposition.
            ///                                   (17.11.2015.) (Domagoj Saric)
            static char constexpr key_file_prefix[] = "/var/tmp/boost_mmap_";
            auto const length{ std::strlen( name ) };
            char prefixed_name[ sizeof( key_file_prefix ) + length + 1 ];
            std::strcpy( prefixed_name, key_file_prefix );
            std::strcat( prefixed_name, name            );
            auto const key_file( create_file( prefixed_name, flags::opening::create( {{},{},access_privileges}, flags::named_object_construction_policy::open_or_create, 0 ) ) );
            if ( BOOST_UNLIKELY( !key_file ) )
                return;
            char seed( 0 ); for ( auto const character : std::string_view{ name } ) seed ^= character;
            sem_key = ::ftok( prefixed_name, seed );
            if ( BOOST_UNLIKELY( sem_key == -1 ) )
                return;
            /// \todo Add key file cleanup!!
            ///                               (17.11.2015.) (Domagoj Saric)
        }

        // no need to cleanup the semaphore or shared memory if orphan - simply reuse - but report as created, instead of opened, for orphans

        /// \note Workaround for SysV semaphores not being automatically
        /// (and atomically) initialised upon creation (i.e. there is a race
        /// window between creation and 'initialisation' (implicitly done by the
        /// first semop() call) aka "SysV semaphore fatal flaw": for
        /// create-or-open calls we have to separate/distinguish between the
        /// create and open cases (and thus add the EXCL flag for the create
        /// case/attempt).
        /// http://boost.2283326.n4.nabble.com/boost-interprocess-Semaphore-cleanup-after-crash-td2581594.html
        /// http://compgroups.net/comp.unix.programmer/the-sysv-semaphore-paradox/456395
        /// http://stackoverflow.com/questions/1242367/correctly-destroying-named-system-v-semaphores
        ///                                   (10.10.2015.) (Domagoj Saric)
        if ( ( static_cast<flags::flags_t>( creation_disposition ) & ( O_CREAT | O_EXCL ) ) == O_CREAT ) // create or open
        {
            std::uint8_t const maximum_retries( 8 );
            for ( std::uint8_t retry( 0 ); ( semid_ == -1 ) && ( retry < maximum_retries ); ++retry )
            {
                semid_ = ::semget( sem_key, 1, access_privileges.flags | IPC_CREAT | IPC_EXCL );
                if ( semid_ >= 0 )
                {
                    if ( BOOST_UNLIKELY( !semadd( 1 ) ) )
                    {
                        /// \note We use SEM_UNDO which may require memory
                        /// allocation so we have to check for a possible failure
                        /// (with questionable reliability due to the overcommit
                        /// blunder).
                        ///                       (09.10.2015.) (Domagoj Saric)
                        BOOST_ASSERT( errno == ENOMEM );
                        remove();
                        BOOST_ASSERT( errno == ENOMEM );
                        return;
                    }
                    BOOST_ASSERT( value() == 1 );
                    break;
                }
                else
                if ( error::is<EEXIST>() )
                {   // someone else got it first
                    BOOST_ASSERT( semid_ == -1 );

                    semid_ = ::semget( sem_key, 1, access_privileges );
                    if ( BOOST_UNLIKELY( semid_ == -1 ) )
                    {
                        if ( error::is<ENOENT>() )
                            /// \note Race condition: the preexisting semaphore
                            /// disappeared before we could open it - retry.
                            ///                   (09.10.2015.) (Domagoj Saric)
                            continue; // retry
                        else
                            return  ; // fail
                    }

                    if ( !is_initialised() )
                    {
                        /// \note Semaphore not yet initialised by its creator - we do not enter
                        /// into a retry loop here as one has to be created by the user anyway
                        ///                                   (09.10.2015.) (Domagoj Saric)
                        /// \note We do not enter a retry loop here.
                        ///                                   (09.10.2015.) (Domagoj Saric)
                        //error::set( ENOENT );
                        //semid_ = -1;
                        remove();
                        continue;
                        //return; // fail
                    }

                    if ( BOOST_UNLIKELY( !semadd( 1 ) ) )
                    {
                        semid_ = -1;
                        return; // fail
                    }
                    break; // success
                }
                else
                    return; // fail
            } // retry loop
        }
        else
        {
            auto          const posix_creation_disposition( static_cast<flags::flags_t>( creation_disposition ) );
            std::uint16_t        sysv_creation_disposition( 0 );
            if ( posix_creation_disposition & O_CREAT )
                sysv_creation_disposition |= IPC_CREAT;
            if ( posix_creation_disposition & O_EXCL )
                sysv_creation_disposition |= IPC_EXCL;
            semid_ = ::semget( sem_key, 1, access_privileges | sysv_creation_disposition );

            if ( !( posix_creation_disposition & O_CREAT ) ) // open
            {
                std::uint8_t const maximum_retries( 8 );
                for ( std::uint8_t retry( 0 ); ( retry < maximum_retries ); ++retry )
                {
                    if ( is_initialised() )
                        return; // success
                }
                error::set( ETIME );
                semid_ = -1;
                return; // fail
            }
        }
        // success
    }

    bool named_semaphore::is_initialised() const noexcept
    {
        /// \todo Investigate a possible problem here on OSX:
        /// http://lists.apple.com/archives/darwin-dev/2005/Mar/msg00147.html sem_otime getting reset to 0
        /// http://calculix-rpm.sourceforge.net/sysvsem.html Review of System V Semaphore details
        ///                                   (10.10.2015.) (Domagoj Saric)

        struct semid_ds sem_info;
        BOOST_VERIFY( ::semctl( semid_, 0, IPC_STAT, &sem_info ) == 0 );
        return BOOST_LIKELY( sem_info.sem_otime != 0 );
    }

    named_semaphore::~named_semaphore() noexcept
    {
        /// \note We can't perform the cleanup procedure here as it's too late
        /// (considering this is a helper class designed to be used by a parent
        /// object) - the parent object needs to insert its own cleanup between
        /// the checked-decrement and the removal of the semaphore.
        ///                                   (18.10.2015.) (Domagoj Saric)
    #if 0
        BOOST_VERIFY( --( *this ) );
        if ( value() == 0 )
            remove();
    #endif
    }

    void named_semaphore::remove() noexcept
    {
        BOOST_VERIFY( ::semctl( semid_, 0, IPC_RMID ) == 0 || error::is<EIDRM>() );
        semid_ = -1;
    }

    std::uint16_t named_semaphore::value() const noexcept
    {
        auto const result( ::semctl( semid_, 0, GETVAL, 0 ) );
        BOOST_ASSERT( result >= 0 );
        return static_cast<std::uint16_t>( result );
    }

    bool named_semaphore::semadd( int const value, bool const nowait /*= false*/ ) noexcept
    {
        auto const result( semop( value, nowait ) );
        BOOST_ASSERT( result || error::get() == ENOMEM || ( nowait && error::get() == EAGAIN ) );
        return result;
    }

    BOOST_ATTRIBUTES( BOOST_MINSIZE, BOOST_RESTRICTED_FUNCTION_L2, BOOST_EXCEPTIONLESS, BOOST_WARN_UNUSED_RESULT )
    bool named_semaphore::semop( int const opcode, bool const nowait /*= false*/ ) noexcept
    {
        // http://linux.die.net/man/2/semop
        ::sembuf sb;
        sb.sem_num = 0;
        sb.sem_op  = opcode;
        sb.sem_flg = SEM_UNDO;
        if ( nowait ) sb.sem_flg |= IPC_NOWAIT;

        while ( BOOST_UNLIKELY( ::semop( semid_, &sb, 1 ) != 0 ) )
        {
            // interrupted by a signal - retry
            // http://stackoverflow.com/questions/9579158/semop-failed-with-errno-4-dose-semop-support-threads-race-inside-a-proces
            if ( !error::is<EINTR>() )
                return false;
        }
        return true;
    }
} // namespace detail

//------------------------------------------------------------------------------
} // posix
//------------------------------------------------------------------------------
} // mmap
//------------------------------------------------------------------------------
} // boost
//------------------------------------------------------------------------------
#endif // __ANDROID__
