////////////////////////////////////////////////////////////////////////////////
/// psi::vm::erased_ref_predicate -- the type-erased comparator predicate for
/// the sort/merge type-erasure machinery (psi/vm/sort.hpp,
/// psi/vm/inplace_merge.hpp).
///
/// The by-ref wrapper closure minted by make_trivially_copyable_predicate is a
/// DISTINCT type per specialization (i.e. per deduced Pred -- each comparator
/// type, and each cv/ref flavour it is deduced with, mints its own closure
/// type), so algorithms taking the predicate as a deduced template parameter
/// (pdqsort & co) get re-stamped along the comparator axis even though every
/// wrapper does the exact same thing. This named 16-byte (fn pointer +
/// predicate word), still trivially copyable, predicate collapses that axis to
/// a single type per key. The indirect call target is unique per sort call and
/// thus perfectly predicted.
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

#include <bit>
#include <cstddef>
#include <cstring>
#include <memory>  // start_lifetime_as
#include <new>
#include <type_traits>

#ifdef __has_builtin
#   if __has_builtin( __builtin_is_implicit_lifetime )
#       define PSI_VM_HAS_IMPLICIT_LIFETIME_BUILTIN 1
#   endif
#endif
#ifndef PSI_VM_HAS_IMPLICIT_LIFETIME_BUILTIN
#   define PSI_VM_HAS_IMPLICIT_LIFETIME_BUILTIN 0
#endif
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

// Use the SysV convention for the comparator thunk even under the MS ABI:
// more argument registers and far fewer callee-saved XMMs make the indirect
// leaf call cost close to a plain jump. Silently ignored on non-x86-64
// targets, so no architecture gate — but the GNU attribute syntax (required
// in the fn-pointer declarator position, where [[gnu::sysv_abi]] does not
// parse) is unavailable under MSVC, which keeps its native convention.
// `leaf` additionally promises the callee never calls back into the calling
// translation unit (a comparator thunk only reads the predicate and the
// keys), letting the caller keep values it could otherwise have to assume
// clobbered by re-entry live across the indirect call.
#if defined( __GNUC__ ) || defined( __clang__ )
#   define PSI_VM_ERASED_PRED_ABI __attribute__(( sysv_abi, leaf ))
#else
#   define PSI_VM_ERASED_PRED_ABI
#endif

template <typename Key>
struct erased_ref_predicate
{
    // [[clang::noescape]] is part of the function type: without it here the
    // pointer's type differs from thunk's actual type and every call through
    // fn is formally UB (caught by UBSan's 'function' check).
    using fn_t = bool ( PSI_VM_ERASED_PRED_ABI * )( [[ clang::noescape ]] void const * __restrict pred, Key left, Key right ) noexcept;

    fn_t         fn;
    void const * pred;

    [[ gnu::hot, gnu::pure ]] bool operator()( Key const left, Key const right ) const noexcept {
        return fn( pred, left, right );
    }

    // function_ref-style small-functor optimization (cf. psi::functionoid):
    // a trivially copyable predicate that fits the pred slot is stored IN it
    // by value, so the thunk receives the functor itself in a register — no
    // memory indirection per comparison, no lifetime tie to the caller's
    // object. Reachable: the erasure gates admit non-reg-passable
    // comparators, and can_be_passed_in_reg requires full triviality — a
    // pointer-sized trivially-copyable comparator with a non-trivial (e.g.
    // reference-member-deleted) default constructor is erased yet fits here.
    // Implicit-lifetime-ness makes the memcpy in inline_thunk create the
    // object (aggregates — even with reference members — and anything with a
    // trivial eligible copy constructor + trivial destructor qualify; that is
    // every trivially copyable comparator in practice, so the fallback for
    // pre-__builtin_is_implicit_lifetime compilers accepts all of them).
    template <typename Pred>
#if PSI_VM_HAS_IMPLICIT_LIFETIME_BUILTIN
    static bool constexpr implicit_lifetime{ __builtin_is_implicit_lifetime( Pred ) };
#else
    static bool constexpr implicit_lifetime{ true };
#endif
    template <typename Pred>
    static bool constexpr stored_inline{
        std::is_trivially_copyable_v<Pred> && implicit_lifetime<Pred>
     && sizeof ( Pred ) <= sizeof ( void const * )
     && alignof( Pred ) <= alignof( void const * )
    };

    template <typename Pred>
    static erased_ref_predicate bind( Pred const & __restrict p ) noexcept {
        if constexpr ( stored_inline<Pred> ) {
            void const * slot{ nullptr };
            std::memcpy( &slot, &p, sizeof( p ) );
            return { &inline_thunk<Pred>, slot };
        } else {
            return { &thunk<Pred>, &p };
        }
    }

private:
    template <typename Pred>
    [[ gnu::pure ]] static PSI_VM_ERASED_PRED_ABI bool thunk
    (
        [[ clang::noescape ]] void const * __restrict pp,
        Key const left, Key const right
    ) noexcept {
        return static_cast<bool>( (*static_cast<Pred const *>( pp ))( left, right ) );
    }

    template <typename Pred>
    [[ gnu::pure ]] static PSI_VM_ERASED_PRED_ABI bool inline_thunk
    (
        // same parameter type as thunk's so inline_thunk's type equals fn_t
        // (noescape trivially holds: the VALUE in pp never escapes either)
        [[ clang::noescape ]] void const * __restrict const pp, // holds the Pred VALUE, not an address
        Key const left, Key const right
    ) noexcept {
        // Implicit-lifetime reconstitution of the Pred stored in pp's own
        // storage (see the stored_inline gate) — no default constructor
        // required (reference-member aggregates have it deleted). Prefer
        // start_lifetime_as (not yet in this MS STL — feature-tested), else
        // bit_cast for the exact-size case: both stay in registers, while the
        // memcpy+launder dance (kept only for the sub-pointer-size remainder)
        // is known to still trip optimizers into an actual stack round-trip.
#ifdef __cpp_lib_start_lifetime_as
        // un-restrict first: &pp is pointer-to-__restrict-pointer, which does
        // not convert to (const) void * (clang rejects the qualification
        // conversion — broke the Linux libstdc++ build, where the feature
        // macro is defined, while MS STL without it took the bit_cast path).
        void const * const slot{ pp };
        return static_cast<bool>( ( *std::start_lifetime_as<Pred>( &slot ) )( left, right ) );
#else
        if constexpr ( sizeof( Pred ) == sizeof( pp ) ) {
            return static_cast<bool>( std::bit_cast<Pred>( pp )( left, right ) );
        } else {
            alignas( Pred ) std::byte buf[ sizeof( Pred ) ];
            std::memcpy( buf, &pp, sizeof( Pred ) );
            return static_cast<bool>( ( *std::launder( reinterpret_cast<Pred const *>( buf ) ) )( left, right ) );
        }
#endif
    }
}; // erased_ref_predicate

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
