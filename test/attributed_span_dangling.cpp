////////////////////////////////////////////////////////////////////////////////
/// psi::vm::attributed_span, sorted_range and sorted_view: the lifetime
/// annotations. Compiled by clang only, with -Xclang -verify: every line that
/// dangles has to draw the warning it is annotated with, and no other line may
/// draw one.
////////////////////////////////////////////////////////////////////////////////

#include <psi/vm/attributed_span.hpp>
#include <psi/vm/sorted_view.hpp>

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <span>
#include <type_traits>
#include <vector>

//------------------------------------------------------------------------------
namespace psi::vm::dangling_test {
//------------------------------------------------------------------------------

std::vector<int> make_vector();
void             use( std::span<int const> );

// The constraints are std::span's: a temporary container converts to a span of
// const elements (and so can dangle), not to one of mutable ones.
static_assert( !std::is_constructible_v<attributed_span<int      >, std::vector<int> &&> );
static_assert(  std::is_constructible_v<attributed_span<int const>, std::vector<int> &&> );
static_assert( !std::is_constructible_v<std::span      <int      >, std::vector<int> &&> );
static_assert(  std::is_constructible_v<std::span      <int const>, std::vector<int> &&> );
// std::ranges::dangling guards only what a range algorithm returns.
static_assert( std::same_as<decltype( std::ranges::find( make_vector(), 1 ) ), std::ranges::dangling> );

//------------------------------------------------------------------------------
// What dangles.
//------------------------------------------------------------------------------

int from_temporary_container()
{
    attributed_span<int const> const s{ make_vector() }; // expected-warning {{will be destroyed at the end of the full-expression}}
    return s[ 0 ];
}

int copy_initialized_from_temporary_container()
{
    attributed_span<int const> const s = std::vector<int>{ 1, 2, 3 }; // expected-warning {{will be destroyed at the end of the full-expression}}
    return s[ 0 ];
}

attributed_span<int const> of_a_local()
{
    std::vector<int> const local{ 1, 2, 3 };
    return attributed_span<int const>{ local }; // expected-warning {{address of stack memory associated with}}
}

attributed_span<int const> of_a_by_value_parameter( std::vector<int> const by_value )
{
    return attributed_span<int const>{ by_value }; // expected-warning {{address of stack memory associated with}}
}

attributed_span<int const> of_a_local_array()
{
    int const local[]{ 1, 2, 3 };
    return { local, 3 }; // expected-warning {{address of stack memory associated with}}
}

int from_temporary_owning_sorted_range()
{
    attributed_span<int const> const s{ sorted_range{ make_vector() } }; // expected-warning {{will be destroyed at the end of the full-expression}}
    return s[ 0 ];
}

attributed_span<int const> sorted_view_of_local_scratch( attributed_span<int const> const keys )
{
    std::vector<int> scratch;
    return sorted_view( keys, scratch ); // expected-warning {{address of stack memory associated with}}
}

//------------------------------------------------------------------------------
// What does not.
//------------------------------------------------------------------------------

int from_lvalue_container( std::vector<int> const & v )
{
    attributed_span<int const> const s{ v };
    return s[ 0 ];
}

int from_temporary_std_span( std::vector<int> & v )
{
    attributed_span<int> const s{ std::span<int>{ v } };
    return s[ 0 ];
}

int from_temporary_subrange( std::vector<int> & v )
{
    attributed_span<int> const s{ std::ranges::subrange( v.begin(), v.end() ) };
    return s[ 0 ];
}

int from_temporary_referring_sorted_range( std::vector<int> & v )
{
    attributed_span<int> const by_ref { sorted_range{ v } };
    attributed_span<int> const by_view{ sorted_range{ std::span<int>{ v } } };
    return by_ref[ 0 ] + by_view[ 0 ];
}

int base_of_temporary_sorted_range( std::vector<int> & v )
{
    // base() of an rvalue moves the range out: a container by value is
    // returned by value (and bound here), a reference as the reference.
    auto const & owned   { sorted_range{ make_vector() }.base() };
    auto const & referred{ sorted_range{ v             }.base() };
    return owned[ 0 ] + referred[ 0 ];
}

attributed_span<int const> subspan_of_parameter( attributed_span<int const> const span )
{
    return span.subspan( 1 );
}

attributed_span<int const> of_a_pointer_parameter( int const * const first, std::uint32_t const size )
{
    return { first, size };
}

attributed_span<int const> sorted_view_of_parameter_scratch( attributed_span<int const> const keys, std::vector<int> & scratch )
{
    return sorted_view( keys, scratch );
}

void temporary_within_the_full_expression()
{
    use( attributed_span<int const>{ make_vector() } );
    use( attributed_span<int const>{ sorted_range{ make_vector() } } );
}

//------------------------------------------------------------------------------
} // namespace psi::vm::dangling_test
//------------------------------------------------------------------------------
