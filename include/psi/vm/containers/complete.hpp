#pragma once
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// True when T is a complete object type. Use to guard type-trait builtins and
// sizeof in headers that must tolerate forward-declared value_types (e.g.
// vector<heap_storage<Rec>> while Rec is still being defined).
template <typename T>
concept complete = sizeof( T ) >= 0 || false;

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
