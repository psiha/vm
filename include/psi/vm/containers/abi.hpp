#pragma once

#include <psi/build/disable_warnings.hpp>

#include <boost/config.hpp>

#include <span>
#include <string>
#include <string_view>
#include <type_traits>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

PSI_WARNING_DISABLE_PUSH()
PSI_WARNING_MSVC_DISABLE( 5030 ) // unrecognized attribute

////////////////////////////////////////////////////////////////////////////////
// Modern(ized) attempt at 'automatized' boost::call_traits primarily to support
// efficient transparent comparators & non-inlined generic lookup functions
// which cause neither unnecessary copies of non-trivial types nor pass-by-ref
// of trivial ones.
// Largely still WiP...
// Essentially this is 'explicit IPA SROA'.
// https://gcc.gnu.org/onlinedocs/gccint/passes-and-files-of-the-compiler/inter-procedural-optimization-passes.html
////////////////////////////////////////////////////////////////////////////////

template <typename T>
bool constexpr can_be_passed_in_reg
{
    (
        std::is_trivial_v<T> &&
        ( sizeof( T ) <= 2 * sizeof( void * ) ) // assuming a sane ABI like SysV (ignoring the MS x64 disaster)
    )
#if defined( __GNUC__ ) || defined( __clang__ )
    || // detect SIMD types (this could also produce false positives for large compiler-native vectors that do not fit into the register file)
    requires{ __builtin_convertvector( T{}, T ); }
#endif
    // This is certainly not an exhaustive list/'trait' - certain types that can
    // be passed in reg cannot be detected as such by existing compiler
    // functionality, e.g. Homogeneous Vector Aggregates
    // https://devblogs.microsoft.com/cppblog/introducing-vector-calling-convention
    // users are encouraged to provide specializations for such types.
}; // can_be_passed_in_reg

template <typename T>
bool constexpr statically_sized_container{
    requires { T::static_capacity; } or
    requires { T::static_size;     } or
    requires { T::capacity();      } or
    requires { requires( std::integral_constant<std::size_t, T{}.size()>::value != 0 ); }
};

/// Detects types that behave as strings (basic_string and its derived types).
/// Requires traits_type to distinguish from generic char containers (e.g. vector<char>).
template <typename T>
concept string_viewable = requires {
    typename T::value_type;
    typename T::traits_type;
} && requires( T const & t ) {
    std::basic_string_view<typename T::value_type, typename T::traits_type>{ t };
};

template <typename T>
struct optimal_const_ref { using type = T const &; };

template <string_viewable T>
struct optimal_const_ref<T> { using type = std::basic_string_view<typename T::value_type, typename T::traits_type>; };

template <std::ranges::contiguous_range Rng>
requires( not std::ranges::borrowed_range<Rng> and not statically_sized_container<Rng> and not string_viewable<Rng> )
struct optimal_const_ref<Rng> { using type = std::span<std::ranges::range_value_t<Rng> const>; };

template <typename T>
struct [[ clang::trivial_abi ]] pass_in_reg
{
    static auto constexpr pass_by_val{ can_be_passed_in_reg<T> };

    using  value_type = T;
    using stored_type = std::conditional_t<pass_by_val, T, typename optimal_const_ref<T>::type>;
    BOOST_FORCEINLINE
    constexpr pass_in_reg( auto const &... args ) noexcept requires requires { stored_type{ args... }; } : value{ args... } {}
    constexpr pass_in_reg( pass_in_reg const &  ) noexcept = default;
    constexpr pass_in_reg( pass_in_reg       && ) noexcept = default;

    stored_type value;

    [[ gnu::pure ]] BOOST_FORCEINLINE
    constexpr operator stored_type const &() const noexcept { return value; }
}; // pass_in_reg
template <typename T>
pass_in_reg( T ) -> pass_in_reg<T>;

////////////////////////////////////////////////////////////////////////////////
// Unified lookup pattern for sorted associative containers
//
// C++23 sorted containers provide two overloads per lookup function:
//   iterator find( key_type const & );                            // always
//   template<class K> iterator find( K const & ) requires transp; // conditional
//
// This library merges them into a single constrained template (see lookup.hpp:
// LookupType concept) + a private _impl taking Reg auto const.
//
// Correctness for all three accepted key categories:
//
// 1. K == key_type (any comparator):
//    pass_in_reg{ key } (CTAD) → pass_in_reg<key_type>. stored_type is either
//    key_type by value (trivial/small) or optimal_const_ref (e.g. string →
//    string_view).  The comparator always handles these — this is the
//    standard case.
//
// 2. K != key_type, transparent comparator:
//    pass_in_reg{ key } (CTAD) → pass_in_reg<K>. Preserves the heterogeneous
//    type. The transparent comparator accepts it directly. This is the
//    standard heterogeneous lookup case.
//
// 3. K != key_type, K convertible to key_type (non-transparent comparator):
//    pass_in_reg{ key } (CTAD) → pass_in_reg<K>.  stored_type wraps K
//    optimally (by value if trivial, otherwise optimal_const_ref<K> which
//    defaults to K const &).  The non-transparent comparator (e.g.
//    std::less<key_type>) receives K and performs implicit conversion to
//    key_type at each comparison call.  This is identical to what happens
//    inside the standard non-template overload — the only difference is that
//    the implicit conversion happens at the comparator call site rather than
//    at the outer function boundary.  For callers who want single-conversion
//    semantics, explicitly constructing key_type before calling find() is
//    trivial; callers who want zero-copy heterogeneous lookup should use a
//    transparent comparator.
//
// Optimality:
//    The _impl function takes Reg auto const — pass_in_reg ensures the key
//    is passed in registers (by value for trivials/SIMD, optimal_const_ref
//    for strings/ranges).  This avoids both unnecessary copies and
//    unnecessary indirection through const-ref for small types.  The public
//    wrapper is a thin inline template that only constructs the pass_in_reg
//    and forwards — no code duplication in the _impl body.
////////////////////////////////////////////////////////////////////////////////

template <typename T>
struct [[ clang::trivial_abi ]] pass_rv_in_reg
{
    static auto constexpr pass_by_val{ can_be_passed_in_reg<T> };

    using  value_type = T;
    using stored_type = std::conditional_t<pass_by_val, T, T &&>;

    constexpr pass_rv_in_reg( T && u ) noexcept : value{ std::move( u ) } {} // move for not-trivially-moveable yet trivial_abi types (that can be passed in reg)

    stored_type value;

    [[ gnu::pure ]] BOOST_FORCEINLINE constexpr operator stored_type const & () const &  noexcept { return            value  ; }
    [[ gnu::pure ]] BOOST_FORCEINLINE constexpr operator stored_type       &&()       && noexcept { return std::move( value ); }
}; // pass_rv_in_reg

template <typename T> bool constexpr reg                   { can_be_passed_in_reg<T> };
template <typename T> bool constexpr reg<pass_in_reg   <T>>{ true };
template <typename T> bool constexpr reg<pass_rv_in_reg<T>>{ true };

template <typename T>
concept Reg = reg<T>;

/// Wraps a value in pass_in_reg if it isn't already Reg-compatible.
/// Returns by value — the result is always Reg and can be passed in registers.
template <typename T>
[[nodiscard]] constexpr auto enreg( T const & v ) noexcept {
    if constexpr ( Reg<T> )
        return v;
    else
        return pass_in_reg{ v };
}

template <typename T> [[nodiscard]] constexpr decltype( auto ) unwrap( pass_in_reg<T> const obj ) noexcept { return obj.value; }
template <typename T> [[nodiscard]] constexpr T &              unwrap( T &                  obj ) noexcept { return obj; }


// utility for passing non trivial predicates to algorithms which pass them around by-val
template <typename Pred>
constexpr decltype( auto ) make_trivially_copyable_predicate( Pred && __restrict pred ) noexcept {
    if constexpr ( can_be_passed_in_reg<std::remove_cvref<Pred>> ) {
        return std::forward<Pred>( pred );
    } else {
        return [&pred]( auto const & ... args ) noexcept( noexcept( pred( args... ) ) ) {
            return pred( args... );
        };
    }
} // make_trivially_copyable_predicate


namespace detail { [[ noreturn, gnu::cold ]] void throw_out_of_range( char const * msg ); }

PSI_WARNING_DISABLE_POP()

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
