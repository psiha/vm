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
#pragma once

#include <psi/vm/mappable_objects/file/handle.hpp>
#include <psi/vm/span.hpp>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

void flush_blocking( mapped_span range, file_handle::const_reference source_file ) noexcept;
#ifndef _WIN32
// No way to perform this on Windows through only the view itself
// https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-flushviewoffile
void flush_blocking( mapped_span range ) noexcept;
#endif
void flush_async   ( mapped_span range ) noexcept;

// these below out to go/get special versions in allocation.hpp
void discard( mapped_span range ) noexcept;

// TODO prefetch/WILL_NEED
// https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-prefetchvirtualmemory


#ifndef _WIN32
// utility verbosity reducing wrapper
void madvise( mapped_span range, int advice ) noexcept;
#endif

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
