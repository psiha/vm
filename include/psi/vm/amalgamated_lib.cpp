////////////////////////////////////////////////////////////////////////////////
///
/// \file amalgamated_lib.cpp
/// -------------------------
///
/// Copyright (c) Domagoj Saric 2011 - 2024.
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

#ifdef PSI_VM_HEADER_ONLY
    #error this file is meant for non header only builds
#endif // PSI_VM_HEADER_ONLY

//...mrmlj...ugh...to be cleaned up
#ifdef _WIN32
#include "allocation/impl/allocation.win32.cpp"
#include "flags/win32/flags.inl"
#include "flags/win32/mapping.inl"
#include "flags/win32/opening.inl"
#include "mappable_objects/shared_memory/win32/flags.inl"
#else
#include "allocation/impl/allocation.posix.cpp"
#include "flags/posix/flags.inl"
#include "flags/posix/mapping.inl"
#include "flags/posix/opening.inl"
#include "mappable_objects/shared_memory/posix/flags.inl"
#endif

//..zzz...by utility.inl...#include "mappable_objects/file/file.inl"
#include "mappable_objects/file/utility.inl"

#include "mapped_view/mapped_view.inl"
//------------------------------------------------------------------------------
namespace psi::vm
{
    template class basic_mapped_view<true >;
    template class basic_mapped_view<false>;
}
//------------------------------------------------------------------------------