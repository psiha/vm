////////////////////////////////////////////////////////////////////////////////
/// Codegen comparison: pass_in_reg vs raw const& for non-inlined lower_bound
///
/// Compile with:
///   clang++ -std=c++26 -stdlib=libc++ -O2 -S -o codegen_compare.s codegen_compare.cpp \
///     -I<include-path-to-psi/vm> -I<boost-includes>
///
/// Or use the CMake build: cmake --build . --target codegen_compare
///
/// Look for these functions in the assembly:
///   lower_bound_ref_*    — baseline: Key const &
///   lower_bound_reg_*    — proposed: pass_in_reg<Key>
////////////////////////////////////////////////////////////////////////////////

#include <psi/vm/containers/flat_set.hpp>
#include <psi/vm/containers/flat_map.hpp>

#include <string>
#include <vector>

using namespace psi::vm;

//==============================================================================
// Type aliases for the test containers
//==============================================================================
using int_vec    = std::vector<int>;
using string_vec = std::vector<std::string>;

//==============================================================================
// Variant A: Key passed as const& (current approach)
//==============================================================================

template <typename It, typename Key, typename Comp>
[[ gnu::noinline ]] It lower_bound_ref( It first, It last, Key const & key, Comp const & comp ) {
    return std::lower_bound( first, last, key, comp );
}

template <typename It, typename Key, typename Comp>
[[ gnu::noinline ]] It upper_bound_ref( It first, It last, Key const & key, Comp const & comp ) {
    return std::upper_bound( first, last, key, comp );
}

//==============================================================================
// Variant B: Key passed as pass_in_reg (proposed approach)
//==============================================================================

template <typename It, typename Key, typename Comp>
[[ gnu::noinline ]] It lower_bound_reg( It first, It last, pass_in_reg<Key> key, Comp const & comp ) {
    return std::lower_bound( first, last, key.value, comp );
}

template <typename It, typename Key, typename Comp>
[[ gnu::noinline ]] It upper_bound_reg( It first, It last, pass_in_reg<Key> key, Comp const & comp ) {
    return std::upper_bound( first, last, key.value, comp );
}

//==============================================================================
// Explicit instantiations to force codegen
//==============================================================================

// --- int key, std::less<> (transparent) ---
template int_vec::const_iterator lower_bound_ref<int_vec::const_iterator, int, std::less<>>( int_vec::const_iterator, int_vec::const_iterator, int const &, std::less<> const & );
template int_vec::const_iterator lower_bound_reg<int_vec::const_iterator, int, std::less<>>( int_vec::const_iterator, int_vec::const_iterator, pass_in_reg<int>, std::less<> const & );

template int_vec::const_iterator upper_bound_ref<int_vec::const_iterator, int, std::less<>>( int_vec::const_iterator, int_vec::const_iterator, int const &, std::less<> const & );
template int_vec::const_iterator upper_bound_reg<int_vec::const_iterator, int, std::less<>>( int_vec::const_iterator, int_vec::const_iterator, pass_in_reg<int>, std::less<> const & );

// --- string key, std::less<> (transparent → pass_in_reg stores string_view) ---
template string_vec::const_iterator lower_bound_ref<string_vec::const_iterator, std::string, std::less<>>( string_vec::const_iterator, string_vec::const_iterator, std::string const &, std::less<> const & );
template string_vec::const_iterator lower_bound_reg<string_vec::const_iterator, std::string, std::less<>>( string_vec::const_iterator, string_vec::const_iterator, pass_in_reg<std::string>, std::less<> const & );

template string_vec::const_iterator upper_bound_ref<string_vec::const_iterator, std::string, std::less<>>( string_vec::const_iterator, string_vec::const_iterator, std::string const &, std::less<> const & );
template string_vec::const_iterator upper_bound_reg<string_vec::const_iterator, std::string, std::less<>>( string_vec::const_iterator, string_vec::const_iterator, pass_in_reg<std::string>, std::less<> const & );

// NOTE: std::less<std::string> (non-transparent) + pass_in_reg<string> is intentionally
// NOT tested — pass_in_reg converts string to string_view, which non-transparent
// comparators can't handle. This is exactly why key_const_arg only uses pass_in_reg
// when transparent_comparator is true or the key is trivially passable in reg.


//==============================================================================
// Callers — to see how the call site codegen differs
//==============================================================================

[[ gnu::noinline ]]
auto caller_int_ref( int_vec const & v, int key ) {
    return lower_bound_ref( v.begin(), v.end(), key, std::less<>{} );
}

[[ gnu::noinline ]]
auto caller_int_reg( int_vec const & v, int key ) {
    return lower_bound_reg<int_vec::const_iterator, int, std::less<>>( v.begin(), v.end(), pass_in_reg<int>{ key }, std::less<>{} );
}

[[ gnu::noinline ]]
auto caller_string_ref( string_vec const & v, std::string const & key ) {
    return lower_bound_ref( v.begin(), v.end(), key, std::less<>{} );
}

[[ gnu::noinline ]]
auto caller_string_reg( string_vec const & v, std::string const & key ) {
    return lower_bound_reg<string_vec::const_iterator, std::string, std::less<>>( v.begin(), v.end(), pass_in_reg<std::string>{ key }, std::less<>{} );
}

// caller with string_view directly (ideal case — no string object at all)
[[ gnu::noinline ]]
auto caller_string_reg_sv( string_vec const & v, std::string_view key ) {
    return lower_bound_reg<string_vec::const_iterator, std::string, std::less<>>( v.begin(), v.end(), pass_in_reg<std::string>{ key }, std::less<>{} );
}


//==============================================================================
// flat_set integration test — how the container's own lower_bound looks
//==============================================================================

using int_set       = flat_set<int>;
using string_set    = flat_set<std::string>;
using string_set_tr = flat_set<std::string, std::less<>>; // transparent

[[ gnu::noinline ]]
auto set_find_int( int_set const & s, int key ) {
    return s.find( key );
}

[[ gnu::noinline ]]
auto set_find_string( string_set const & s, std::string const & key ) {
    return s.find( key );
}

[[ gnu::noinline ]]
auto set_find_string_tr( string_set_tr const & s, std::string_view key ) {
    return s.find( key );
}
