////////////////////////////////////////////////////////////////////////////////
/// psi::vm::attributed_span, sorted_range and sorted_view, and psi::vm's own
/// containers: the lifetime annotations. Compiled by clang only, with -Xclang -verify: every line that
/// dangles has to draw the warning it is annotated with, and no other line may
/// draw one.
////////////////////////////////////////////////////////////////////////////////

#include <psi/vm/attributed_span.hpp>
#include <psi/vm/sorted_view.hpp>
#include <psi/vm/containers/fc_vector.hpp>
#include <psi/vm/containers/flat_map.hpp>
#include <psi/vm/containers/flat_set.hpp>
#include <psi/vm/containers/heap_vector.hpp>
#include <psi/vm/containers/small_vector.hpp>
#include <psi/vm/containers/strided_vector.hpp>
#include <psi/vm/containers/vm_vector.hpp>

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <span>
#include <type_traits>
#include <vector>

//------------------------------------------------------------------------------
namespace psi::vm::dangling_test {
//------------------------------------------------------------------------------

std::vector<int>          make_vector();
heap_vector<int>          make_heap_vector();
small_vector<int, 4>      make_small_vector();
fc_vector<int, 4>         make_fc_vector();
vm_vector<int>            make_vm_vector();
strided_vector<int>       make_strided_vector();
flat_set<int>             make_flat_set();
flat_map<int, int>        make_flat_map();
void                      use( std::span<int const> );
void                      fill( std::span<int> ) noexcept;

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

// psi::vm's own containers: each is a gsl::Owner, and what hands out a view of
// its elements is lifetimebound.

int from_temporary_native_container()
{
    attributed_span<int const> const heap { make_heap_vector () }; // expected-warning {{will be destroyed at the end of the full-expression}}
    std::span      <int const> const small{ make_small_vector() }; // expected-warning {{will be destroyed at the end of the full-expression}}
    std::span      <int const> const fixed = make_fc_vector();     // expected-warning {{will be destroyed at the end of the full-expression}}
    std::span      <int const> const vm   { make_vm_vector   () }; // expected-warning {{will be destroyed at the end of the full-expression}}
    return heap[ 0 ] + small[ 0 ] + fixed[ 0 ] + vm[ 0 ];
}

int from_a_member_of_a_temporary_native_container()
{
    // (Copy-initialized: clang does not follow lifetimebound into a class
    // object direct-initialized from the prvalue.)
    auto        const   span    = make_heap_vector().span();            // expected-warning {{temporary whose address is used as value of local variable}}
    int const * const   data    = make_heap_vector().data();            // expected-warning {{temporary whose address is used as value of local variable}}
    int const &         element = make_heap_vector()[ 0 ];              // expected-warning {{temporary bound to local reference}}
    int const &         front   = make_small_vector().front();          // expected-warning {{temporary bound to local reference}}
    int const &         at      = make_fc_vector().at( 0 );             // expected-warning {{temporary bound to local reference}}
    std::span<int const> const lane = make_strided_vector().flat_view(); // expected-warning {{will be destroyed at the end of the full-expression}}
    auto const &        keys    = make_flat_set().keys();               // expected-warning {{temporary bound to local reference}}
    auto const &        values  = make_flat_map().values();             // expected-warning {{temporary bound to local reference}}
    return span[ 0 ] + *data + element + front + at + lane[ 0 ] + keys[ 0 ] + values[ 0 ];
}

attributed_span<int const> of_a_local_native_container()
{
    heap_vector<int> const local{ 1, 2, 3 };
    return local; // expected-warning {{address of stack memory associated with}}
}

std::span<int const> span_of_a_local_native_container()
{
    small_vector<int, 4> const local{ 1, 2, 3 };
    return local.span(); // expected-warning {{address of stack memory associated with}}
}

int from_temporary_filled_sorted_range()
{
    attributed_span<int const> const s{ sorted_range<heap_vector<int>, true>::filled( 3, fill ) }; // expected-warning {{will be destroyed at the end of the full-expression}}
    return s[ 0 ];
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

int of_named_native_containers( heap_vector<int> const & heap, flat_set<int> const & set, strided_vector<int> const & strided )
{
    attributed_span<int const> const whole{ heap };
    std::span      <int const> const span { heap.span() };
    int const &                      first{ heap[ 0 ] };
    auto const &                     keys { set.keys() };
    std::span      <int const> const lanes{ strided.flat_view() };
    return whole[ 0 ] + span[ 0 ] + first + keys[ 0 ] + lanes[ 0 ];
}

int of_a_copy_or_an_element_of_a_temporary_native_container()
{
    heap_vector<int> const copy{ make_heap_vector() };
    attributed_span<int const> const s{ copy };
    int const element{ make_heap_vector()[ 0 ] };
    return s[ 0 ] + element;
}

int a_temporary_native_container_within_the_full_expression()
{
    use( make_heap_vector() );
    use( make_heap_vector().span() );
    use( attributed_span<int const>{ sorted_range<heap_vector<int>, true>::filled( 3, fill ) } );
    int sum{};
    for ( int const value : make_heap_vector() )
        sum += value;
    return sum + make_small_vector().front();
}

attributed_span<int const> of_a_native_container_parameter( heap_vector<int> const & by_reference )
{
    return by_reference;
}

//------------------------------------------------------------------------------
} // namespace psi::vm::dangling_test
//------------------------------------------------------------------------------
