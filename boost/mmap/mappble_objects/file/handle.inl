////////////////////////////////////////////////////////////////////////////////
///
/// \file handle.inl
/// ----------------
///
/// Copyright (c) Domagoj Saric 2010.-2011.
///
///  Use, modification and distribution is subject to the Boost Software License, Version 1.0.
///  (See accompanying file LICENSE_1_0.txt or copy at
///  http://www.boost.org/LICENSE_1_0.txt)
///
/// For more information, see http://www.boost.org
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifdef _WIN32
    #include "../win32_file/handle.inl"
#else
    #include "../posix_file/handle.inl"
#endif
//------------------------------------------------------------------------------
