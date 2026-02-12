#pragma once

#include <psi/build/disable_warnings.hpp>

#include <boost/config.hpp>

#include <ranges>
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

template <typename T>
struct optimal_const_ref { using type = T const &; };

template <typename Char>
struct optimal_const_ref<std::basic_string<Char>> { using type = std::basic_string_view<Char>; };

template <std::ranges::contiguous_range Rng>
requires( not std::ranges::borrowed_range<Rng> and not statically_sized_container<Rng> ) // no need to convert 'views' to spans
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

// utility for passing non trivial predicates to algorithms which pass them around by-val
template <typename Pred>
decltype( auto ) make_trivially_copyable_predicate( Pred && __restrict pred ) noexcept {
    if constexpr ( can_be_passed_in_reg<std::remove_cvref<Pred>> ) {
        return std::forward<Pred>( pred );
    } else {
        return [&pred]( auto const & ... args ) noexcept( noexcept( pred( args... ) ) ) {
            return pred( args... );
        };
    }
} // make_trivially_copyable_predicate

namespace detail
{
    [[ noreturn, gnu::cold ]] void throw_out_of_range( char const * msg );
} // namespace detail

PSI_WARNING_DISABLE_POP()

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
