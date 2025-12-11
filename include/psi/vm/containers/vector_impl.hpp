////////////////////////////////////////////////////////////////////////////////
/// Base CRTP-like implementation of shared, standard C++latest functionality
/// for vector-like containers. Also provides extensions like default vs value
/// initialization and explicit grow and shrink (noexcept) vs resize methods,
/// stable/try-expansion (TODO), checked iterators, configurable size_type,
/// pass-by-value pass-in-reg ABI for supported trivial types...
/// w/ special emphasis on code reuse and bloat reduction.
/// (requires deducing this, P0847 support)
/// TODO extract the entire container subdirectory into a separate library or
/// merge with prior art
///  https://github.com/arturbac/small_vectors
///  https://github.com/AmadeusITGroup/amc
///  https://github.com/Quuxplusone/SG14/?tab=readme-ov-file#allocator-aware-in-place-vector-future--c20
////////////////////////////////////////////////////////////////////////////////
///
/// Copyright (c) Domagoj Saric.
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

#include <psi/vm/align.hpp>
#include <psi/vm/containers/abi.hpp>
#include <psi/vm/containers/is_trivially_moveable.hpp>

#include <psi/build/disable_warnings.hpp>

#include <boost/assert.hpp>
#include <boost/config_ex.hpp>

#include <algorithm>
#include <concepts>
#include <climits>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <ranges>
#include <span>
#include <type_traits>
#include <std_fix/const_iterator.hpp>
//------------------------------------------------------------------------------
#ifdef _MSC_VER
namespace std { template <class T, class A> class list; }
#endif
namespace boost::container { struct try_emplace_t; }
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

PSI_WARNING_DISABLE_PUSH()
PSI_WARNING_MSVC_DISABLE( 5030 ) // unrecognized attribute

namespace detail
{
    [[ noreturn, gnu::cold ]] void throw_out_of_range();
#if PSI_MALLOC_OVERCOMMIT != PSI_OVERCOMMIT_Full
    [[ noreturn, gnu::cold ]] void throw_bad_alloc   ();
#else
    [[ gnu::cold ]] inline void throw_bad_alloc() noexcept
    {
        BOOST_ASSERT_MSG( false, "Unexpected allocation failure" );
        std::unreachable();
    }
#endif

    template <typename T>
    constexpr T * mutable_iter( T const * const ptr ) noexcept { return const_cast<T *>( ptr ); }

    template <typename Base>
    constexpr Base mutable_iter( std::basic_const_iterator<Base> const iter ) noexcept { return iter.base(); }
#if defined( _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR )
    template <typename T>
    auto mutable_iter( std::__bounded_iter<T const *> const iter ) noexcept { return reinterpret_cast<std::__bounded_iter<T *> const &>( iter ); }
#elif _ITERATOR_DEBUG_LEVEL
    template <typename T>
    auto mutable_iter( std::_Span_iterator<T const> const iter ) noexcept { return reinterpret_cast<std::_Span_iterator<T> const &>( iter ); }
#endif

    struct init_policy_tag{};
} // namespace detail

template <std::unsigned_integral Target>
[[ gnu::const ]] constexpr Target verified_cast( std::unsigned_integral auto const source ) noexcept
{
    auto constexpr target_max{ std::numeric_limits<Target>::max() };
    BOOST_ASSUME( source <= target_max );
    auto const result{ static_cast<Target>( source ) };
    BOOST_ASSUME( result == source );
    return result;
}

struct no_init_t      : detail::init_policy_tag{}; inline constexpr no_init_t      no_init     ;
struct default_init_t : detail::init_policy_tag{}; inline constexpr default_init_t default_init;
struct value_init_t   : detail::init_policy_tag{}; inline constexpr value_init_t   value_init  ;

template <typename T>
concept init_policy = std::is_base_of_v<detail::init_policy_tag, T>;


// Try to avoid calls to destructors of empty objects (knowing that even latest
// compilers sometimes fail to elide all calls to free/delete even w/ inlined
// destructors and move constructors in contexts where it is 'obvious' the obj
// is moved out of prior to destruction) - 'absolutely strictly' this would
// require a new, separate type trait. Absent that, the closest thing would be
// to detect the existence of an actual move constructor/assignment and assume
// these leave the object in an 'empty'/'free'/'destructed' state (this may be
// true for the majority of types but does not hold in general - e.g. for node
// based containers that allocate the sentinel node on the heap - as in other
// similar cases in the, fast-moving/WiP, containers part of the library we
// opt for an 'unsafe heuristic compromise' at this stage of development -
// catch performance early and rely on sanitizers, tests and asserts to catch
// bugs/types that need special care). There is no standardized way to detect
// even this so for starters settle for nothrow-move-assignability (assuming
// it implies a nothrow, therefore no allocation, copy if there is no
// actual/separate move constructor/assignment), adding trivial destructibility
// to encompass the stray contrived type that might not be included otherwise.
// https://github.com/psiha/vm/pull/34#discussion_r1909052203
// https://quuxplusone.github.io/blog/2025/01/10/trivially-destructible-after-move
// (crossing ways with
//   https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2839r0.html Nontrivial Relocation via a New owning reference Type
//   https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p1029r3.pdf  move = bitcopies
// )
// TODO impl https://stackoverflow.com/questions/51901837/how-to-get-if-a-type-is-truly-move-constructible/51912859#51912859
template <typename T>
constexpr bool trivially_destructible_after_move_assignment{ std::is_nothrow_move_assignable_v<T> || std::is_trivially_destructible_v<T> };
#ifdef _MSC_VER // assuming this implies MS STL
template <typename T, typename A>
constexpr bool trivially_destructible_after_move_assignment<std::list<T, A>>{ false }; // retains a heap allocated sentinel node
#endif
#ifdef __GLIBCXX__
// libstdc++'s string ('gets' and) retains the allocator and storage from the moved-to string
// https://gcc.gnu.org/git/?p=gcc.git;a=blobdiff;f=libstdc%2B%2B-v3/include/bits/basic_string.h;h=c81dc0d425a0ae648c46f520b603971978413281;hp=aa018262c98b6633bc347bfa75492fce38f6c631;hb=540a22d243966d1b882db26b17fe674467e2a169;hpb=49e52115b09b477382fef6f04fd7b4d1641f902c
template <typename E, typename T, typename A>
bool constexpr trivially_destructible_after_move_assignment<std::basic_string<E, T, A>>{ false };
#endif

// see the note for up() on why the Impl parameter is used/required (even with deducing this support)
template <typename Impl, typename T, typename sz_t>
class [[ nodiscard, clang::trivial_abi ]] vector_impl
{
public:
    using value_type             = T;
    using       pointer          = value_type       *;
    using const_pointer          = value_type const *;
    using       reference        = value_type       &;
    using const_reference        = value_type const &;
#if 0 // fails for incomplete T
    using param_const_ref        = std::conditional_t<can_be_passed_in_reg<value_type>, value_type const, pass_in_reg<value_type>>;
#else
    using param_const_ref        = pass_in_reg<value_type>;
#endif
    using       size_type        = sz_t;
    using difference_type        = std::make_signed_t<size_type>;
#if defined( _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR )
    using       iterator         = std::__bounded_iter<pointer>;
    using const_iterator         = std::basic_const_iterator<iterator>; // __bounded_iter<T> is not convertible to __bounded_iter<T const> so we have to use the std wrapper
#elif _ITERATOR_DEBUG_LEVEL
    // checked_array_iterator is deprecated so use the span iterator wrapper
    using       iterator         = std::_Span_iterator<value_type>;
    using const_iterator         = std::basic_const_iterator<iterator>; // same as for __bounded_iter
#else
    using       iterator         =       pointer;
    using const_iterator         = const_pointer;
#endif
    using       reverse_iterator = std::reverse_iterator<      iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

private:
    // Workaround for a deducing this defect WRT to private inheritance: the
    // problem&solution described in the paper do not cover this case - where
    //  * the base class is empty
    //  * the implementation details are in an immediately derived class D0
    //  * there is a class Dx which then privately inherits from D0 and calls
    //    vector_impl methods
    // - then the type of the self argument gets deduced as Dx (const &) -
    // through which members of D0 cannot be accessed due to private
    // inheritance. Moreover in that case we (the base class) do not know of the
    // D0 (implementation) type and cannot cast self to it - therefore in order
    // to support this use case we have to require that the 'actual
    // implementation' derived type be explicitly specified as template
    // parameter (just like in the classic CRTP).
    // Explicitly using the Impl type for self then has the added benefit of
    // eliminating the extra compile-time and binary size hit of instantiating
    // base/vector_impl methods for/with all the possible derived types.
    // It also has the negative effect of compilation errors when invoking non-
    // const methods on rvalues - as rvalue references cannot bind to a an
    // 'Impl &'...in search of a solution (other than having two overloads
    // everywhere or using yet-another ref-proxy; those using 'auto & self' can
    // at least simply be modified to use 'auto && self').
    // https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html#the-shadowing-mitigation-private-inheritance-problem
    // https://stackoverflow.com/questions/844816/c-style-upcast-and-downcast-involving-private-inheritance (tunneling magic of C-style casts)
    template <typename U> [[ gnu::const ]] static constexpr Impl       & up( U       & self ) noexcept { static_assert( std::is_base_of_v<Impl, U> ); return (Impl &)self; }
    template <typename U> [[ gnu::const ]] static constexpr Impl const & up( U const & self ) noexcept { return up( const_cast<U &>( self ) ); }

private:
    constexpr auto begin_ptr( this auto & self ) noexcept { return self.data(); }
    constexpr auto   end_ptr( this auto & self ) noexcept { return self.data() + self.size(); }
    constexpr iterator make_iterator( [[ maybe_unused ]] this Impl & self, value_type * const ptr ) noexcept
    {
        return
#   if defined( _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR )
            std::__make_bounded_iter( ptr, self.begin_ptr(), self.end_ptr() );
#   elif _ITERATOR_DEBUG_LEVEL
            std::_Span_iterator{ ptr, self.begin_ptr(), self.end_ptr() };
#   else
            ptr;
#   endif
    }
    constexpr iterator make_iterator( this Impl & self, size_type const offset ) noexcept
    {
        auto const begin{ self.data() };
        return self.make_iterator( begin + offset );
    }
    constexpr const_iterator make_iterator( this Impl const & self, size_type const offset ) noexcept
    {
        return { const_cast<Impl &>( self ).make_iterator( offset ) };
    }

    // constructor helpers - for initializing the derived Impl class
    // (simplifying or minimizing the need to write specialized Impl
    // constructors) - to be used only in vector_impl constructors!
    constexpr Impl & initialized_impl( size_type const initial_size, no_init_t ) noexcept
    {
        auto & impl{ static_cast<Impl &>( *this ) };
        impl.storage_init( initial_size );
        BOOST_ASSUME( impl.size() == initial_size );
        return impl;
    }
    constexpr Impl & initialized_impl( size_type const initial_size, default_init_t ) noexcept
    {
        auto & impl{ initialized_impl( initial_size, no_init ) };
        std::uninitialized_default_construct_n( impl.data(), impl.size() );
        return impl;
    }
    constexpr Impl & initialized_impl( size_type const initial_size, value_init_t ) noexcept
    {
        auto & impl{ initialized_impl( initial_size, no_init ) };
        if constexpr ( std::is_trivially_constructible_v<value_type> && Impl::storage_zero_initialized )
        {
            BOOST_ASSERT_MSG
            (
                !initial_size || ( *std::max_element( impl.data(), impl.size() ) == 0 ),
                "Broken storage promise to zero-init"
            );
        }
        else
        {
            std::uninitialized_value_construct_n( impl.data(), impl.size() );
        }
        return impl;
    }

    constexpr vector_impl( Impl & self, Impl const & other ) noexcept( noexcept_storage() && std::is_nothrow_copy_constructible_v<value_type> )
    {
        BOOST_ASSUME( self.empty() );
        auto const sz{ other.size() };
        auto const self_data{ self.grow_to( sz, no_init ) };
        std::uninitialized_copy_n( other.begin(), sz, self_data );
    }

protected:
    constexpr  vector_impl(                      ) noexcept = default;
    constexpr  vector_impl( vector_impl const &  ) noexcept = default;
    constexpr  vector_impl( vector_impl       && ) noexcept = default;
    constexpr ~vector_impl(                      ) noexcept = default;

public:
    // MSVC (VS 17.12.3) fails compilation if this is a variable
    static bool consteval noexcept_storage() { return noexcept( std::declval<Impl &>().storage_grow_to( size_type( 123 ) ) ); };

    // non standard default: default-initialization
    constexpr explicit vector_impl( size_type const initial_size ) noexcept( noexcept_storage() && std::is_nothrow_default_constructible_v<T> ) : vector_impl( initial_size, default_init ) {}
    constexpr vector_impl( size_type const initial_size, init_policy auto const policy ) noexcept( noexcept_storage() && std::is_nothrow_default_constructible_v<T> )
    {
        initialized_impl( initial_size, policy );
    }

    constexpr vector_impl( size_type const count, param_const_ref const value ) noexcept( noexcept_storage() && std::is_nothrow_copy_constructible_v<value_type> )
    {
        auto & impl{ initialized_impl( count, no_init ) };
        std::uninitialized_fill_n( impl.data(), count, value );
        BOOST_ASSUME( impl.size() == count );
    }

    template <std::input_iterator It>
    constexpr vector_impl( It const first, It const last ) noexcept( noexcept_storage() && std::is_nothrow_copy_constructible_v<value_type> )
    {
        if constexpr ( std::random_access_iterator<It> )
        {
            auto const sz{ static_cast<size_type>( std::distance( first, last ) ) };
            auto & impl{ initialized_impl( sz, no_init ) };
            // STL utility functions handle EH safety - no need to catch to
            // reset size as Impl/the derived class should not attempt cleanup
            // if this (its base constructor) fails.
            std::uninitialized_copy_n( first, sz, impl.data() );
        }
        else
        {
            auto & impl{ initialized_impl( 0, no_init ) };
            std::copy( first, last, std::back_inserter( impl ) );
        }
    }

    constexpr vector_impl( std::initializer_list<value_type> const initial_values ) noexcept( noexcept_storage() && std::is_nothrow_copy_constructible_v<value_type> )
        : vector_impl( initial_values.begin(), initial_values.end() )
    {}


    constexpr vector_impl & operator=( vector_impl && ) noexcept = default;
    constexpr vector_impl & operator=( this Impl & self, Impl const & other ) noexcept( noexcept_storage() && std::is_nothrow_copy_constructible_v<value_type> )
    {
        BOOST_ASSUME( &self != &other ); // not going to support self assignment
        self.assign( other );
        return self;
    }

   constexpr vector_impl & operator=( this Impl & self, std::initializer_list<value_type> const data ) { self.assign( data ); return self; }

    //! <b>Effects</b>: Assigns the the range [first, last) to *this.
    //!
    //! <b>Throws</b>: If memory allocation throws or T's copy/move constructor/assignment or
    //!   T's constructor/assignment from dereferencing InpIt throws.
    //!
    //! <b>Complexity</b>: Linear to n.
    template <std::input_iterator It>
    void assign( this Impl & self, It first, It const last )
    {
        // Overwrite all elements we can from [first, last)
        auto       cur   { self.begin() };
        auto const end_it{ self.end  () };
        if constexpr ( std::random_access_iterator<It> ) {
            auto const overwrite_size{ std::min( self.size(), static_cast<typename Impl::size_type>( std::distance( first, last ) ) ) };
            auto const next_iters{ std::ranges::copy_n( first, overwrite_size, cur ) };
            first = next_iters.in;
            cur   = next_iters.out;
        } else {
            while ( ( first != last ) && ( cur != end_it ) ) {
                *cur++ = *first;
                ++first;
            }
        }

        if ( first == last )
        {
            // There are no more elements in the sequence, erase remaining
            auto const target_size{ static_cast<size_type>( cur - self.begin() ) };
            std::destroy( cur, end_it );
            self.storage_shrink_to( target_size );
        }
        else
        {
            // There are more elements in the range, insert the remaining ones
            self.append_range( std::ranges::subrange( first, last ) );
        }
    }

    template <std::random_access_iterator It>
    void assign( this Impl & self, It const first, It const last )
    requires( std::is_trivially_destructible_v<value_type> )
    {
        auto const input_size{ static_cast<size_type>( std::distance( first, last ) ) };
        self.resize( input_size, no_init );
        std::uninitialized_copy_n( first, input_size, self.begin() );
    }
    template <std::ranges::range Rng>
    void assign( this Impl & self, Rng && data )
    {
        if constexpr ( requires{ std::size( data ); } )
        {
            // TODO specialized path for non-random-access ranges with 'cached' size information
        }
        auto const begin{ std::begin( data ) };
        auto const end  { std::end  ( data ) };
        if constexpr ( std::is_rvalue_reference_v<Rng> )
            self.assign( std::make_move_iterator( begin ), std::make_move_iterator( end ) );
        else
            self.assign( begin, end );
    }
    void assign( this Impl & self, Impl && other ) noexcept( std::is_nothrow_move_assignable_v<Impl> )
    {
        self = std::move( other );
    }
    template <std::ranges::range Rng>
    void assign_range( this Impl & self, Rng && data ) { self.assign( std::forward<Rng>( data ) ); }

    //! <b>Effects</b>: Assigns the n copies of val to *this.
    //!
    //! <b>Throws</b>: If memory allocation throws or
    //!   T's copy/move constructor/assignment throws.
    //!
    //! <b>Complexity</b>: Linear to n.
    void assign( this Impl & self, size_type const n, param_const_ref val ) = /*TODO*/ delete;

    //////////////////////////////////////////////
    //
    //                iterators
    //
    //////////////////////////////////////////////

    //! <b>Effects</b>: Returns an iterator to the first element contained in the vector.
    //! <b>Throws</b>: Nothing.
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] auto            begin( this auto       && self ) noexcept { return up( self ).make_iterator( size_type{ 0 } ); }
    [[ nodiscard ]] const_iterator cbegin( this Impl const &  self ) noexcept { return self.begin(); }

    //! <b>Effects</b>: Returns an iterator to the end of the vector.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] auto            end( this auto       && self ) noexcept { return up( self ).make_iterator( self.size() ); }
    [[ nodiscard ]] const_iterator cend( this Impl const &  self ) noexcept { return self.end(); }

    //! <b>Effects</b>: Returns a reverse_iterator pointing to the beginning
    //! of the reversed vector.
    //! <b>Throws</b>: Nothing.
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] auto                    rbegin( this auto       && self ) noexcept { return std::make_reverse_iterator( self.end() ); }
    [[ nodiscard ]] const_reverse_iterator crbegin( this Impl const &  self ) noexcept { return self.rbegin(); }

    //! <b>Effects</b>: Returns a reverse_iterator pointing to the end
    //! of the reversed vector.
    //! <b>Throws</b>: Nothing.
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] auto                    rend( this auto       && self ) noexcept { return std::make_reverse_iterator( self.begin() ); }
    [[ nodiscard ]] const_reverse_iterator crend( this Impl const &  self ) noexcept { return self.rend(); }

    //////////////////////////////////////////////
    //
    //                capacity
    //
    //////////////////////////////////////////////

    //! <b>Effects</b>: Returns true if the vector contains no elements.
    //! <b>Throws</b>: Nothing.
    //! <b>Complexity</b>: Constant.
    [[ nodiscard, gnu::pure ]] bool empty( this Impl const & self ) noexcept { return BOOST_UNLIKELY( self.size() == 0 ); }

    //! <b>Effects</b>: Returns the largest possible size of the vector.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] static constexpr size_type max_size() noexcept { return static_cast<size_type>( std::numeric_limits<size_type>::max() / sizeof( value_type ) ); }

    void resize( this Impl & self, size_type const new_size, auto const init_policy )
    {
        if ( new_size > self.size() ) self.  grow_to( new_size, init_policy );
        else                          self.shrink_to( new_size              );
    }
    // intentional non-standard behaviour: default_init by default
    void resize( this Impl & self, size_type const new_size ) { self.resize( new_size, default_init ); }

    //! <b>Effects</b>: Inserts or erases elements at the end such that
    //!   the size becomes n. New elements are copy constructed from x.
    //!
    //! <b>Throws</b>: If memory allocation throws, or T's copy/move constructor throws.
    //!
    //! <b>Complexity</b>: Linear to the difference between size() and new_size.
    void resize( this Impl & self, size_type const new_size, param_const_ref x )
    {
        auto const current_size{ self.size() };
        BOOST_ASSUME( new_size >= current_size );
        self.resize( new_size, default_init );
        auto uninitialized_span{ self.span().subspan( current_size ) };
        std::uninitialized_fill( uninitialized_span.begin(), uninitialized_span.end(), x );
    }

    void shrink_to_fit( this Impl & self ) noexcept { self.storage_shrink_to( self.size() ); }

    //////////////////////////////////////////////
    //
    //               element access
    //
    //////////////////////////////////////////////

    //! <b>Requires</b>: !empty()
    //!
    //! <b>Effects</b>: Returns a reference to the first
    //!   element of the container.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] auto & front( this auto && self ) noexcept { return up( self ).span().front(); }

    //! <b>Requires</b>: !empty()
    //!
    //! <b>Effects</b>: Returns a reference to the last
    //!   element of the container.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] auto & back( this auto && self ) noexcept { return up( self ).span().back(); }

    //! <b>Requires</b>: size() > n.
    //!
    //! <b>Effects</b>: Returns a reference to the nth element
    //!   from the beginning of the container.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] auto & operator[]( this auto && self, size_type const n ) noexcept { return up( self ).span()[ n ]; }

    //! <b>Requires</b>: size() >= n.
    //!
    //! <b>Effects</b>: Returns an iterator to the nth element
    //!   from the beginning of the container. Returns end()
    //!   if n == size().
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    //!
    //! <b>Note</b>: Non-standard extension
    [[ nodiscard ]] auto nth( this auto && self, size_type const n ) noexcept
    {
        BOOST_ASSUME( n <= self.size() );
        return self.begin() + static_cast<difference_type>( n );
    }

    //! <b>Requires</b>: begin() <= p <= end().
    //!
    //! <b>Effects</b>: Returns the index of the element pointed by p
    //!   and size() if p == end().
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant.
    //!
    //! <b>Note</b>: Non-standard extension
    [[ nodiscard ]] size_type index_of( this Impl const & self, const_iterator const p ) noexcept
    {
        self.verify_iterator( p );
        return static_cast<size_type>( p - self.begin() );
    }

    //! <b>Requires</b>: size() > n.
    //!
    //! <b>Effects</b>: Returns a reference to the nth element
    //!   from the beginning of the container.
    //!
    //! <b>Throws</b>: range_error if n >= size()
    //!
    //! <b>Complexity</b>: Constant.
    [[ nodiscard ]] auto & at( this auto && self, size_type const n )
    {
#   if __cpp_lib_span >= 202311L
        return self.span().at( n );
#   else
        if ( n >= self.size() )
            detail::throw_out_of_range();
        return self[ n ];
#   endif
    }

    //////////////////////////////////////////////
    //
    //                 data access
    //
    //////////////////////////////////////////////

    template <typename Self>
    [[ nodiscard, gnu::pure ]] const_pointer data( this Self const & self ) noexcept { return const_cast<Self &>( self ).Self::data(); }

    [[ nodiscard, gnu::pure ]] auto span( this auto && self ) noexcept { return std::span{ self.data(), self.size() }; }

    //////////////////////////////////////////////
    //
    //                modifiers
    //
    //////////////////////////////////////////////

    //! <b>Effects</b>: Inserts an object of type T constructed with
    //!   std::forward<Args>(args)... at the end of the vector.
    //!
    //! <b>Returns</b>: A reference to the created object.
    //!
    //! <b>Throws</b>: If memory allocation throws or the in-place constructor throws or
    //!   T's copy/move constructor throws.
    //!
    //! <b>Complexity</b>: Amortized constant time.

    template <class ...Args>
    static reference construct_at( value_type & placeholder, Args &&...args ) noexcept( std::is_nothrow_constructible_v<value_type, Args...> )
    {
        return *std::construct_at( &placeholder, std::forward<Args>( args )... );
    }
    template <typename Key, typename...Args> // support for boost::container::flat_tree implementation(s)
    static reference construct_at( value_type & placeholder, boost::container::try_emplace_t &&, Key && key, Args &&...args )
    noexcept( std::is_nothrow_constructible_v<value_type, std::piecewise_construct_t, std::tuple<Key>, std::tuple<Args...>> )
    {
              std::construct_at( &placeholder.first , std::forward<Key>( key ) );
        try { std::construct_at( &placeholder.second, std::forward<Args>( args )... ); }
        catch( ... ) { std::destroy_at( &placeholder.first ); throw; }
        return placeholder;
    }
    static reference construct_at( value_type & placeholder ) noexcept( std::is_nothrow_default_constructible_v<value_type> )
    {
        return *(new (&placeholder) value_type); // default to default init
    }

    template <class ...Args>
    reference emplace_back( this Impl & self, Args &&...args )
    {
        auto const current_size{ self.size() };
        if constexpr ( sizeof...( Args ) || true ) // grow_by( 1, default_init ) would call uninitialized_default_construct (i.e. a loop) - TODO grow_by_one and shrink_by_one methods for push&pop-back operations
        {
            auto const data{ self.grow_by( 1, no_init ) };
            auto & placeholder{ data[ current_size ] };
            try {
                return construct_at( placeholder, std::forward<Args>( args )... );
            } catch( ... ) {
                self.shrink_by( 1 );
                throw;
            }
        }
        else
        {
            return self.grow_by( 1, default_init )[ current_size ];
        }
    }

    //! <b>Effects</b>: Inserts an object of type T constructed with
    //!   std::forward<Args>(args)... in the end of the vector.
    //!
    //! <b>Throws</b>: If the in-place constructor throws.
    //!
    //! <b>Complexity</b>: Constant time.
    //!
    //! <b>Note</b>: Non-standard extension.
    template <typename... Args>
    bool stable_emplace_back( this Impl & self, Args &&... args ) = /*TODO*/delete;

    //! <b>Requires</b>: position must be a valid iterator of *this.
    //!
    //! <b>Effects</b>: Inserts an object of type T constructed with
    //!   std::forward<Args>(args)... before position
    //!
    //! <b>Throws</b>: If memory allocation throws or the in-place constructor throws or
    //!   T's copy/move constructor/assignment throws.
    //!
    //! <b>Complexity</b>: If position is end(), amortized constant time
    //!   Linear time otherwise.
    template <typename... Args>
    iterator emplace( this Impl & self, const_iterator const position, Args &&... args )
    {
        auto const iter{ self.make_space_for_insert( position, 1 ) };
        if constexpr ( trivially_destructible_after_move_assignment<T> )
            construct_at( *iter, std::forward<Args>( args )... );
        else
            *iter = { std::forward<Args>( args )... };
        return iter;
    }

    //! <b>Effects</b>: Inserts a copy of x at the end of the vector.
    //!
    //! <b>Throws</b>: If memory allocation throws or
    //!   T's copy/move constructor throws.
    //!
    //! <b>Complexity</b>: Amortized constant time.
    void push_back( this Impl & self, param_const_ref x ) noexcept( noexcept_storage() && std::is_nothrow_copy_constructible_v<value_type> )
    {
        self.emplace_back( x );
    }

    //! <b>Effects</b>: Constructs a new element in the end of the vector
    //!   and moves the resources of x to this new element.
    //!
    //! <b>Throws</b>: If memory allocation throws or
    //!   T's copy/move constructor throws.
    //!
    //! <b>Complexity</b>: Amortized constant time.
    void push_back( this Impl & self, value_type && x ) noexcept( noexcept_storage() && std::is_nothrow_move_constructible_v<value_type> )
    requires( !std::is_trivial_v<value_type> ) // otherwise better to go through the pass-in-reg overload
    {
        self.emplace_back( std::move( x ) );
    }

    //! <b>Requires</b>: position must be a valid iterator of *this.
    //!
    //! <b>Effects</b>: Insert a copy of x before position.
    //!
    //! <b>Throws</b>: If memory allocation throws or T's copy/move constructor/assignment throws.
    //!
    //! <b>Complexity</b>: If position is end(), amortized constant time
    //!   Linear time otherwise.
    iterator insert( this Impl & self, const_iterator const position, param_const_ref x ) { return self.emplace( position, x ); }

    //! <b>Requires</b>: position must be a valid iterator of *this.
    //!
    //! <b>Effects</b>: Insert a new element before position with x's resources.
    //!
    //! <b>Throws</b>: If memory allocation throws.
    //!
    //! <b>Complexity</b>: If position is end(), amortized constant time
    //!   Linear time otherwise.
    iterator insert( this Impl & self, const_iterator const position, value_type && x ) requires( !std::is_trivially_move_constructible_v<value_type> ) { return self.emplace( position, std::move( x ) ); }

    //! <b>Requires</b>: position must be a valid iterator of *this.
    //!
    //! <b>Effects</b>: Insert n copies of x before pos.
    //!
    //! <b>Returns</b>: an iterator to the first inserted element or p if n is 0.
    //!
    //! <b>Throws</b>: If memory allocation throws or T's copy/move constructor throws.
    //!
    //! <b>Complexity</b>: Linear to n.
    iterator insert( this Impl & self, const_iterator const position, size_type const n, param_const_ref x )
    {
        auto const iter{ self.make_space_for_insert( position, n ) };
        if constexpr ( trivially_destructible_after_move_assignment<T> )
            std::uninitialized_fill_n( iter, n, x );
        else
            std::fill_n( iter, n, x );
        return iter;
    }

    //! <b>Requires</b>: position must be a valid iterator of *this.
    //!
    //! <b>Effects</b>: Insert a copy of the [first, last) range before pos.
    //!
    //! <b>Returns</b>: an iterator to the first inserted element or pos if first == last.
    //!
    //! <b>Throws</b>: If memory allocation throws, T's constructor from a
    //!   dereferenced InpIt throws or T's copy/move constructor/assignment throws.
    //!
    //! <b>Complexity</b>: Linear to boost::container::iterator_distance [first, last).
    template <std::input_iterator InIt>
    iterator insert( this Impl & self, const_iterator const position, InIt const first, InIt const last )
    {
        auto const n{ static_cast<size_type>( std::distance( first, last ) ) };
        auto const iter{ self.make_space_for_insert( position, n ) };
        if constexpr ( trivially_destructible_after_move_assignment<T> )
            std::uninitialized_copy_n( first, n, iter );
        else
            std::copy_n( first, n, iter );
        return iter;
    }

    //! <b>Requires</b>: position must be a valid iterator of *this.
    //!
    //! <b>Effects</b>: Insert a copy of the [il.begin(), il.end()) range before position.
    //!
    //! <b>Returns</b>: an iterator to the first inserted element or position if first == last.
    //!
    //! <b>Complexity</b>: Linear to the range [il.begin(), il.end()).
    iterator insert( this Impl & self, const_iterator const position, std::initializer_list<value_type> const il )
    {
        return self.insert( position, il.begin(), il.end() );
    }

    template <std::ranges::range Rng>
    void append_range( this Impl & self, Rng && __restrict rng )
    {
        auto const current_size{ self.size() };
        if constexpr ( requires{ std::size( rng ); } )
        {
            auto const additional_size{ verified_cast<size_type>( std:: size( rng ) ) };
            auto const input_begin    {                           std::begin( rng )   };
            auto const target_position{ self.grow_by( additional_size, no_init ) + current_size };
            try
            {
                if constexpr ( std::is_rvalue_reference_v<Rng> )
                    std::uninitialized_move_n( input_begin, additional_size, target_position );
                else
                    std::uninitialized_copy_n( input_begin, additional_size, target_position );
            }
            catch (...)
            {
                self.storage_shrink_size_to( current_size );
                throw;
            }
        }
        else
        {
            try
            {
                if constexpr ( std::is_rvalue_reference_v<Rng> )
                    std::move( std::begin( rng ), std::end( rng ), std::back_inserter( self ) );
                else
                    std::copy( std::begin( rng ), std::end( rng ), std::back_inserter( self ) );
            }
            catch (...)
            {
                self.storage_shrink_size_to( current_size );
                throw;
            }
        }
    }
#ifndef __cpp_lib_span_initializer_list
    void append_range( this Impl & self, std::initializer_list<value_type> const rng ) { self.append_range( std::span{ rng.begin(), rng.end() } ); }
#endif

    //! <b>Effects</b>: Removes the last element from the container.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Constant time.
    void pop_back( this Impl & self ) noexcept
    {
        BOOST_ASSUME( !self.empty() );
        std::destroy_at( &self.back() );
        self.storage_dec_size();
    }

    //! <b>Effects</b>: Erases the element at position pos.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Linear to the elements between pos and the
    //!   last element. Constant if pos is the last element.
    iterator erase( this Impl & self, const_iterator const position ) noexcept
    {
        self.verify_iterator( position );
        auto const pos_index{ self.index_of( position ) };
        auto const mutable_pos{ self.nth( pos_index ) };
        std::shift_left( mutable_pos, self.end(), 1 );
        if constexpr ( trivially_destructible_after_move_assignment<T> )
            self.storage_dec_size();
        else
            self.pop_back();
        return self.nth( pos_index );
    }

    //! <b>Effects</b>: Erases the elements pointed by [first, last).
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Linear to the distance between first and last
    //!   plus linear to the elements between pos and the last element.
    iterator erase( this Impl & self, const_iterator const first, const_iterator const last ) noexcept
    {
        self.verify_iterator( first );
        self.verify_iterator( last  );
        BOOST_ASSERT( first <= last );
        auto const first_index{ self.index_of( first ) };
        auto const mutable_start{ detail::mutable_iter( first ) };
        auto const mutable_end  { detail::mutable_iter( last  ) };
        auto const new_end      { std::move( mutable_end, self.end(), mutable_start ) };
        auto const new_size     { static_cast<size_type>( new_end - self.begin() ) };
        if constexpr ( trivially_destructible_after_move_assignment<T> )
            self.storage_shrink_size_to( new_size );
        else
            self.shrink_to( new_size );
        return self.nth( first_index );
    }
    //! <b>Effects</b>: Erases all the elements of the vector.
    //!
    //! <b>Throws</b>: Nothing.
    //!
    //! <b>Complexity</b>: Linear to the number of elements in the container.
    [[ gnu::cold ]]
    void clear( this Impl & self ) noexcept
    {
        std::destroy( self.begin(), self.end() );
        self.storage_shrink_to( 0 );
    }

    void swap( this auto && self, auto & other ) noexcept { std::swap( self, other ); }


    ///////////////////////////////////////////////////////////////////////////
    // Extensions
    ///////////////////////////////////////////////////////////////////////////

    //! <b>Effects</b>: If n is less than or equal to capacity(), this call has no
    //!   effect. Otherwise, it is a request for allocation of additional memory
    //!   (memory expansion) that will not invalidate iterators.
    //!   If the request is successful, then capacity() is greater than or equal to
    //!   n; otherwise, capacity() is unchanged. In either case, size() is unchanged.
    //!
    //! <b>Throws</b>: If memory allocation throws or T's copy/move constructor throws.
    //!
    //! <b>Note</b>: Non-standard extension.
    bool stable_reserve( this Impl & self, size_type new_cap ) = /*TODO*/ delete;

    value_type * grow_to( this Impl & self, size_type const target_size, no_init_t ) { return self.storage_grow_to( target_size ); }
    value_type * grow_to( this Impl & self, size_type const target_size, default_init_t )
    {
        auto const current_size{ self.size() };
        auto const data{ self.grow_to( target_size, no_init ) };
        if constexpr ( !std::is_trivially_default_constructible_v<value_type> ) {
            try {
                std::uninitialized_default_construct( &data[ current_size ], &data[ target_size ] );
            } catch(...) {
                self.storage_shrink_size_to( current_size );
                throw;
            }
        }
        return data;
    }

    value_type * grow_to( this Impl & self, size_type const target_size, value_init_t )
    {
        auto const current_size{ self.size() };
        BOOST_ASSUME( target_size >= current_size );
        auto const data{ self.grow_to( target_size, no_init ) };
        auto const uninitialized_space_begin{ &data[ current_size ] };
        auto const uninitialized_space_size { target_size - current_size };
        if constexpr ( std::is_trivially_constructible_v<value_type> )
        {
            auto const new_space_bytes    { reinterpret_cast<std::uint8_t const *>( data + current_size ) };
            auto const new_space_byte_size{ uninitialized_space_size * sizeof( value_type ) };
            if constexpr ( Impl::storage_zero_initialized )
            {
                BOOST_ASSERT_MSG
                (
                    *std::max_element( new_space_bytes, new_space_bytes + new_space_byte_size ) == 0,
                    "Broken storage promise to zero-extend"
                );
            }
            else
            {
                std::memset( new_space_bytes, 0, new_space_byte_size );
            }
        }
        else
        {
            try {
                std::uninitialized_value_construct_n( uninitialized_space_begin, uninitialized_space_size );
            } catch(...) {
                self.storage_shrink_size_to( current_size );
                throw;
            }
        }
        return data;
    }
    template <typename U>
    value_type * grow_to( this Impl & self, size_type const target_size, U && default_value )
    requires std::constructible_from<T, U>
    {
        auto const current_size{ self.size() };
        BOOST_ASSUME( target_size >= current_size );
        auto const data{ self.grow_to( target_size, no_init ) };
        auto const uninitialized_space_begin{ &data[ current_size ] };
        auto const uninitialized_space_size { target_size - current_size };
        try {
            std::uninitialized_fill_n( uninitialized_space_begin, uninitialized_space_size, std::forward<U>( default_value ) );
        } catch(...) {
            self.storage_shrink_size_to( current_size );
            throw;
        }
        return data;
    }

    value_type * grow_by( this Impl & self, size_type const delta, auto const init_policy )
    {
        return self.grow_to( self.size() + delta, init_policy );
    }

    void shrink_to( this Impl & self, size_type const target_size ) noexcept
    {
        BOOST_ASSUME( target_size <= self.size() );
        std::destroy( self.nth( target_size ), self.end() );
        self.storage_shrink_size_to( target_size ); // std::vector behaviour: never release/shrink capacity
    }
    void shrink_by( this Impl & self, size_type const delta ) noexcept { self.shrink_to( self.size() - delta ); }

private:
    void verify_iterator( [[ maybe_unused ]] this Impl const & self, [[ maybe_unused ]] const_iterator const iter ) noexcept
    {
        BOOST_ASSERT( iter >= self.begin() );
        BOOST_ASSERT( iter <= self.end  () );
    }

    iterator make_space_for_insert( this Impl & self, const_iterator const position, size_type const n )
    {
        self.verify_iterator( position );
        auto const position_index{ self.index_of( position ) };
        auto const current_size  { self.size() };
        auto const new_size      { current_size + n };
        auto const data{ self.grow_to( new_size, no_init ) };
        auto const elements_to_move{ static_cast<size_type>( current_size - position_index ) };
        if constexpr ( is_trivially_moveable<value_type> )
        {
            PSI_WARNING_DISABLE_PUSH()
            PSI_WARNING_CLANG_DISABLE( -Wnontrivial-memcall ) // e.g. for trivial_abi std::string
            // does not use is_trivially_moveable/trivial_abi and is incorrect
            // (i.e. an uninitialized_move_backwards is required)
            //std::uninitialized_move_n( &data[ position_index ], elements_to_move, &data[ position_index + n ] );
            std::memmove( &data[ position_index + n ], &data[ position_index ], elements_to_move * sizeof( *data ) );
            PSI_WARNING_DISABLE_POP()
        }
        else
        {
            auto const elements_to_move_to_uninitialized_space{ n };
            auto const elements_to_move_to_the_current_end    { static_cast<size_type>( elements_to_move - elements_to_move_to_uninitialized_space ) };
            std::uninitialized_move
            (
                &data[ current_size - elements_to_move_to_uninitialized_space ],
                &data[ current_size ],
                &data[ current_size ]
            );
            std::move_backward
            (
                &data[ position_index ],
                &data[ position_index + elements_to_move_to_the_current_end ],
                &data[ position_index + elements_to_move_to_the_current_end + n ]
            );
        }
        return self.make_iterator( &data[ position_index ] );
    }
}; // class vector_impl


//! <b>Effects</b>: Returns the result of std::lexicographical_compare_three_way
//!
//! <b>Complexity</b>: Linear to the number of elements in the container.
[[ nodiscard ]] constexpr auto operator<=>( std::ranges::range auto const & left, std::ranges::range auto const & right ) noexcept { return std::lexicographical_compare_three_way( left.begin(), left.end(), right.begin(), right.end() ); }
[[ nodiscard ]] constexpr auto operator== ( std::ranges::range auto const & left, std::ranges::range auto const & right ) noexcept { return std::equal                            ( left.begin(), left.end(), right.begin(), right.end() ); }

PSI_WARNING_DISABLE_POP()

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
