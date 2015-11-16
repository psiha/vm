////////////////////////////////////////////////////////////////////////////////
///
/// \file win32/mem.hpp
/// -------------------
///
/// Copyright (c) Domagoj Saric 2015.
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
#ifndef mem_hpp__6DB85D55_CCA0_493D_AB14_78064457885B
#define mem_hpp__6DB85D55_CCA0_493D_AB14_78064457885B
#pragma once
//------------------------------------------------------------------------------
#include "flags.hpp"

#include <boost/mmap/detail/impl_selection.hpp>
#include <boost/mmap/mapping/mapping.hpp>
#include <boost/mmap/error/error.hpp>
#include <boost/mmap/handles/handle.hpp>
#include <boost/mmap/mappable_objects/file/utility.hpp>
#include <boost/mmap/mappable_objects/shared_memory/policies.hpp>

#include <boost/core/ignore_unused.hpp>
#include <boost/detail/winapi/system.hpp>
#include <boost/utility/string_ref.hpp>

#include <cstddef>
#include <type_traits>
//------------------------------------------------------------------------------
namespace boost
{
//------------------------------------------------------------------------------
namespace mmap
{
//------------------------------------------------------------------------------
namespace win32
{
//------------------------------------------------------------------------------

namespace detail
{
    class shm_path
    {
    public:
        explicit shm_path( char const * const name ) { apply( name ); }

        char const * c_str() const { return &buffer_[ 0            ]; }
        char const * name () const { return &buffer_[ name_offset_ ]; }

    private:
        void BOOST_CC_REG apply( char const * BOOST_RESTRICTED_PTR const name )
        {
            auto const length( static_cast<std::uint8_t>( ::GetWindowsDirectoryA( &buffer_[ 0 ], static_cast<std::uint16_t>( buffer_.size() ) ) ) );
            BOOST_ASSUME( length );
            static char constexpr prefix[] = "\\Temp\\shm\\";
            std::strcpy( &buffer_[ length ], prefix );
            name_offset_ = static_cast<std::uint8_t>( std::strlen( &buffer_[ 0 ] ) );
            BOOST_VERIFY( ::CreateDirectoryA( &buffer_[ 0 ], nullptr ) || err::last_win32_error::is<ERROR_ALREADY_EXISTS>() );
            std::strcpy( &buffer_[ length + _countof( prefix ) - 1 ], name );
        }

    private:
        std::array<char, MAX_PATH> buffer_;
        std::uint8_t               name_offset_;
    }; // class shm_path
} // namespace detail


namespace detail
{
    ////////////////////////////////////////////////////////////////////////////
    // named_memory_base
    ////////////////////////////////////////////////////////////////////////////
    class named_memory_base
        : public mapping
    {
    private:
        using ap     = flags::access_privileges;
        using mflags = flags::shared_memory    ;

    public:
        static
        named_memory_base BOOST_CC_REG create
        (
            shm_path    const & BOOST_RESTRICTED_REF       name,
            std::size_t                              const size,
            mflags                                   const flags,
            DWORD                                    const extra_hints
        ) noexcept
        {
            flags::access_privileges const ap = { flags.object_access, flags.child_access, flags.system_access };

            using hints = flags::access_pattern_optimisation_hints;
            auto file
            (
                mmap::create_file
                (
                    name.c_str(),
                    flags::opening
                    {
                        ap,
                        flags.creation_disposition,
                        hints::random_access | /*hints::avoid_caching |*/ FILE_ATTRIBUTE_TEMPORARY | extra_hints
                    }
                )
            );
            if ( BOOST_UNLIKELY( !file ) )
                return {};
            auto const preexisting_file( err::last_win32_error::is<ERROR_ALREADY_EXISTS>() );

            /// \note CreateFileMapping will resize (enlarge) the mapped file as
            /// necessary _unless_ the file's size is zero (making that feature
            /// useless here) so we have to manually/explicitly set the desired
            /// file size.
            ///                               (07.09.2015.) (Domagoj Saric)
            if ( !set_size( file, size ) )
            {
                BOOST_VERIFY( remove( name.c_str() ) );
                return {};
            }

            auto new_mapping =
                create_mapping
                (
                    file,
                    flags,
                    size,
                    name.name()
                );
            auto const creation_disposition( flags.creation_disposition );
            auto const preexisting_mapping ( err::last_win32_error::is<ERROR_ALREADY_EXISTS>() );
            BOOST_ASSERT( preexisting_file == preexisting_mapping );
            switch ( creation_disposition )
            {
                using disposition = flags::named_object_construction_policy;
                case disposition::open_existing                  : 
                case disposition::open_or_create                 : break;
                case disposition::create_new_or_truncate_existing: break;
                case disposition::open_and_truncate_existing     : if ( preexisting_mapping && get_section_size( new_mapping.get() ) != size ) return {};
                case disposition::create_new                     : if ( preexisting_mapping                                                  ) return {};
            }

            return named_memory_base
            (
                std::move( new_mapping ),
                std::move( file        )
            );
        }

        fallible_result<std::size_t> size() const noexcept
        {
            LARGE_INTEGER sz;
            if ( BOOST_UNLIKELY( !::GetFileSizeEx( file_.get(), &sz ) ) )
                return error();
            /// \note Memory mappings cannot ever be larger than
            /// std::numeric_limits<std::size_t>::max().
            ///                               (29.08.2015.) (Domagoj Saric)
            return static_cast<std::size_t>( sz.QuadPart );
        }

        static
        bool BOOST_CC_REG cleanup( char const * const name ) noexcept { return delete_file( shm_path( name ).c_str() ); }

    protected:
        named_memory_base( mapping && mapping_param, file_handle && file )
            : mapping( std::move( mapping_param ) ), file_( std::move( file ) ) {}
        named_memory_base( named_memory_base && ) = default;

        static void save_flags( flags::mapping const & ) {}

        named_memory_base() : mapping( nullptr, {} ) {};
    protected:
        file_handle file_;
    }; // named_memory_base

    ////////////////////////////////////////////////////////////////////////////
    // resizable_named_memory_base
    ////////////////////////////////////////////////////////////////////////////

    class resizable_named_memory_base : public named_memory_base
    {
    public:
        resizable_named_memory_base( resizable_named_memory_base && ) = default;

        fallible_result<void> BOOST_CC_REG resize( std::size_t const new_size )
        {
            // https://msdn.microsoft.com/en-us/library/windows/desktop/aa365531(v=vs.85).aspx "erratum comment" SetEndOfFile can be used to enlarge a mapped file...
            // http://blogs.msdn.com/b/oldnewthing/archive/2015/01/30/10589818.aspx            TONT Creating a shared memory block that can grow in size
            // http://blogs.technet.com/b/markrussinovich/archive/2008/11/17/3155406.aspx      Pushing the Limits of Windows: Virtual Memory
            // 
            // Unix2Win32 migration guides
            // http://www.microsoft.com/en-us/download/details.aspx?id=6904
            // https://msdn.microsoft.com/en-us/library/y23kc048.aspx

            // Recreate/fetch the various attributes required for reopening the
            // mapping handle:

            mapping & this_mapping( *this );

            // * child process inheritance
            DWORD flags; BOOST_VERIFY( ::GetHandleInformation( this_mapping.get(), &flags ) );
            auto const child_access( ( flags & HANDLE_FLAG_INHERIT ) != 0 );
           
            // * file name
            // Obtaining a File Name From a File Handle
            // https://msdn.microsoft.com/en-us/library/windows/desktop/aa366789(v=vs.85).aspx
            std::array<char, MAX_PATH> file_path;
            auto const file_path_length( ::GetFinalPathNameByHandleA( file_.get(), &file_path[ 0 ], static_cast<DWORD>( file_path.size() - 1 ), FILE_NAME_OPENED ) );
            if ( !file_path_length )
                return error();
            BOOST_ASSERT( file_path_length < file_path.size() );
            auto const p_name( &file_path[ string_ref( &file_path[ 0 ], file_path_length ).rfind( '\\' ) + 1 ] );

            // * security attributes
            ::SECURITY_ATTRIBUTES sa = { sizeof( sa ), nullptr, child_access };
            {
                auto const result
                (   // ...or https://msdn.microsoft.com/en-us/library/aa446641(v=vs.85).aspx...
                    ::GetSecurityInfo( this_mapping.get(), SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION, nullptr, nullptr, nullptr, nullptr, /*reinterpret_cast<PSECURITY_DESCRIPTOR *>*/( &sa.lpSecurityDescriptor ) )
                );
                if ( result != ERROR_SUCCESS )
                {
                    error::set( result );
                    return error();
                }
            }

            this_mapping.close();
            auto const file_resize_success( set_size( file_, new_size ) );
            /// \note Continue even if resize failed in order to offer a
            /// stronger error-safety guarantee (i.e. try to at least recreate
            /// the 'original' mapping).
            ///                               (05.09.2015.) (Domagoj Saric)

            // * handle access privileges
        #if 0
            // http://blog.aaronballman.com/2011/08/how-to-check-access-rights
            // http://stackoverflow.com/questions/9442436/windows-how-do-i-get-the-mode-access-rights-of-an-already-opened-file
            // http://fsfilters.blogspot.hr/2011/11/filters-and-irpmjqueryinformation.html
            FILE_BASIC_INFO file_info;
            if ( !::GetFileInformationByHandleEx( file_.get(), FileBasicInfo, &file_info, sizeof( file_info ) ) )
                return error();
        #else
            auto const viewing_flags( this_mapping.view_mapping_flags );
            BOOST_ASSERT( !viewing_flags.is_cow() );
            auto const mapping_flags( flags::detail::object_access_to_page_access( { viewing_flags.map_view_flags }, flags::viewing::share_mode::shared ) );
        #endif

            static_cast<handle &>( this_mapping ) =
                handle( detail::create_mapping_impl::call_create( p_name, file_.get(), &sa, mapping_flags, 0, 0 ) );
            BOOST_VERIFY( ::LocalFree( sa.lpSecurityDescriptor ) == nullptr );

            if ( BOOST_LIKELY( file_resize_success && this_mapping ) )
                return err::success;
            return error();
        }

    protected:
        template <lifetime_policy, resizing_policy> friend class named_memory;
        resizable_named_memory_base( named_memory_base && other ) : named_memory_base( std::move( other ) ) {}

        void save_flags( flags::mapping const & mflags )
        {
        #if 0
            object_access =                                          mflags.object_access;
            child_access  =                                          mflags.child_access ;
            share_mode    = static_cast<flags::viewing::share_mode>( mflags.map_view_flags.map_view_flags );
        #else
            ignore_unused( mflags );
        #endif
        }

    private:
    #if 0
        flags::access_privileges::object        object_access;
        flags::access_privileges::child_process child_access ;
        flags::viewing          ::share_mode    share_mode   ;
    #endif
    }; // class resizable_named_memory_basey

    template <lifetime_policy lifetime_policy_param, resizing_policy resizing_policy_param>
    using named_memory_base_t = std::conditional_t
    <
        resizing_policy_param == resizing_policy::resizeable,
        resizable_named_memory_base,
        named_memory_base
    >;
} // namespace detail

template <lifetime_policy lifetime_policy_param, resizing_policy resizing_policy_param>
class file_backed_named_memory
    :
    public detail::named_memory_base_t<lifetime_policy_param, resizing_policy_param>
{
private:
    using base_t = detail::named_memory_base_t<lifetime_policy_param, resizing_policy_param>;
    using mflags = flags::shared_memory;

public:
    static
    file_backed_named_memory BOOST_CC_REG create
    (
        char        const * const name,
        std::size_t         const size,
        mflags              const flags
    ) noexcept
    {
        detail::shm_path const shm_name   ( name );
        DWORD            const extra_flags( lifetime_policy_param == lifetime_policy::scoped ? FILE_FLAG_DELETE_ON_CLOSE : 0 );
        base_t result
        (
            named_memory_base::create( shm_name, size, flags, extra_flags )
        );

        if ( lifetime_policy_param == lifetime_policy::persistent )
            // http://marc.durdin.net/2011/09/why-you-should-not-use-movefileex-with-movefile_delay_until_reboot-2
            BOOST_VERIFY
            (
                ::MoveFileExA( shm_name.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT ) ||
                error::get() == ERROR_ACCESS_DENIED // admin privileges required
            );

        // save flags for resizable if it needs
        result.save_flags( flags );

        return std::move( result );
    }

private:
    file_backed_named_memory( base_t && base ) : base_t( std::move( base ) ) {}
}; // class named_memory<persistent | resizable>

class native_named_memory //<lifetime_policy::scoped, resizing_policy::fixed>
    : public mapping
{
public:
    static
    fallible_result<native_named_memory>
    BOOST_CC_REG create
    (
        char                 const * const name,
        std::size_t                  const size,
        flags::shared_memory         const flags
    ) noexcept
    {
        return detail::create_mapping_impl::do_map( file_handle::reference{ file_handle::traits::invalid_value }, flags, size, name );
    }

    fallible_result<std::size_t> size() const noexcept
    {
        auto const p_view( ::MapViewOfFile( get(), 0, 0, 0, 0 ) );
        if ( BOOST_UNLIKELY( !p_view ) ) return error();
        MEMORY_BASIC_INFORMATION info;
        BOOST_VERIFY( ::VirtualQuery( p_view, &info, sizeof( info ) ) == sizeof( info ) );
        BOOST_VERIFY( ::UnmapViewOfFile( p_view ) );
        BOOST_ASSUME( info.RegionSize % page_size == 0 );
        return info.RegionSize;
    }

private:
    /// \note Required to enable the emplacement constructors of boost::err
    /// wrappers. To be solved in a cleaner way...
    ///                                       (28.05.2015.) (Domagoj Saric)
    friend class  err::result_or_error<native_named_memory, error>;
    friend struct std::is_constructible<native_named_memory, mapping &&>;
    native_named_memory( mapping && src ) : mapping( std::move( src ) ) {}

    //char const system_global_prefix[] = "Global\\";
    //char const user_local_prefix   [] = "Local\\" ;
}; // class named_memory<lifetime_policy::scoped, resizing_policy::fixed, win32>


namespace detail
{
    template <lifetime_policy lifetime, resizing_policy resizability>
    struct named_memory_impl : std::identity<win32::file_backed_named_memory<lifetime, resizability>> {};

    template <>
    struct named_memory_impl<lifetime_policy::scoped, resizing_policy::fixed> : std::identity<win32::native_named_memory> {};
} // namespace detail

//------------------------------------------------------------------------------
} // namespace win32
//------------------------------------------------------------------------------
} // namespace mmap
//------------------------------------------------------------------------------
} // namespace boost
//------------------------------------------------------------------------------

#ifdef BOOST_MMAP_HEADER_ONLY
    //#include "mem.inl"
#endif // BOOST_MMAP_HEADER_ONLY

#endif // mem_hpp
