////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric 2010 - 2024.
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
#include <psi/vm/flags/flags.posix.hpp>

#include <boost/assert.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------
inline namespace posix
{
//------------------------------------------------------------------------------
namespace flags
{
//------------------------------------------------------------------------------

flags_t access_privileges::oflag() const noexcept
{
    //...zzz...use fadvise...
    // http://stackoverflow.com/questions/2299402/how-does-one-do-raw-io-on-mac-os-x-ie-equivalent-to-linuxs-o-direct-flag

    flags_t result{ ( object_access.privileges >> procsh ) & 0xFF };

#if O_RDWR != ( O_RDONLY | O_WRONLY )
    auto constexpr o_rdwr{ O_RDONLY_ | O_WRONLY };
    if ( ( result & o_rdwr ) == o_rdwr )
        result &= ~o_rdwr | O_RDWR;
    else
    {
        /// \note Remove the "Undetectable combined O_RDONLY" workaround flag.
        ///                                   (09.11.2015.) (Domagoj Saric)
#   if !O_RDONLY
        result &= ~O_RDONLY_;
#   endif // !O_RDONLY
    }
#endif // no O_RDWR GNUC extension

    result |= static_cast<flags_t>( child_access );

    return result;
}


flags_t access_privileges::system::read_umask() noexcept
{
    // Broken/not thread safe
    // http://man7.org/linux/man-pages/man2/umask.2.html @ notes
    // https://groups.google.com/forum/#!topic/comp.unix.programmer/v6nv-oP9IJQ
    // https://stackoverflow.com/questions/53227072/reading-umask-thread-safe/53288382
    auto const mask{ ::umask( 0 ) };
    BOOST_VERIFY( ::umask( mask ) == 0 );
    return static_cast<flags_t>( mask );
}

//------------------------------------------------------------------------------
} // flags
//------------------------------------------------------------------------------
} // posix
//------------------------------------------------------------------------------
} // psi::vm
//------------------------------------------------------------------------------
