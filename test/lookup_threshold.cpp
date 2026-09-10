////////////////////////////////////////////////////////////////////////////////
///
/// \file lookup_threshold.cpp
///
/// Does the shipped linear-vs-binary threshold actually hold, on this platform,
/// for this key type?
///
/// lookup.hpp dispatches on a single constant, `linear_search_max_values`, and
/// the comment above it records a crossover measured "between 1 and 4 KiB of
/// scanned data -- the same BYTE size for 32-bit and 64-bit keys, so the limit
/// is expressed in bytes, not element count", with the linear path disabled
/// outright on AArch64 because a branchless binary search was found to win at
/// every size.  Those are three separate empirical claims - a value, a unit,
/// and a platform exception - and none of them is checked anywhere.
///
/// This times the two primitives directly, per type, across lengths that
/// bracket the crossover, and reports where they actually cross.  It is a
/// benchmark, not an assertion: it prints a table and leaves the judgement to
/// whoever reads it, because the right threshold is a policy decision and the
/// numbers differ per ISA.
///
/// Copyright (c) Domagoj Saric.
///
////////////////////////////////////////////////////////////////////////////////

#include <psi/vm/containers/lookup.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <numeric>
#include <print>
#include <random>
#include <vector>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

#ifdef NDEBUG // bench only release builds

#ifndef PSI_VM_BENCH_SEED
#   define PSI_VM_BENCH_SEED 0x5eed1234u
#endif

namespace
{
    using timer = std::chrono::high_resolution_clock;
    using ns    = std::chrono::duration<double, std::nano>;

    // A real observable sink.  `EXPECT_GE( acc, 0u )` on an unsigned is
    // trivially true, so the compiler proved the accumulator dead and deleted
    // the entire timed loop - every figure came out 0.00 ns.  Writing through
    // volatile is the cheapest thing it may not remove.
    volatile std::size_t sink{ 0 };

    // Lengths that bracket the crossover for every type of interest.  A leaf of
    // a 512-byte node holds ~123 4-byte keys and a 4096-byte one ~1019, so the
    // shipping geometries sit inside this range at both key widths.
    constexpr std::uint32_t lengths[]
    {
        8, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048
    };

    // Enough probes that the timed region is far longer than the clock's
    // resolution even at the shortest range, and the same count everywhere so
    // the per-probe figures are comparable across lengths.
    constexpr std::uint32_t probes{ 200'000 };
    constexpr int           passes{ 3 };

    template <typename Key>
    [[ nodiscard ]] std::vector<Key> sorted_range( std::uint32_t const n )
    {
        std::vector<Key> v( n );
        // Keys spread over the whole domain rather than 0..n-1: for the float
        // types a dense integer sequence would be an unrepresentatively easy
        // comparison, and for the narrow integer types it would not even fit.
        for ( std::uint32_t i{ 0 }; i < n; ++i ) {
            v[ i ] = static_cast<Key>( i * 3 + 1 );
        }
        return v;
    }

    // The two primitives, timed identically.  `keys` are the probe values and
    // the result is accumulated so neither search can be elided.
    template <bool linear, typename Key>
    [[ nodiscard ]] ns time_search( std::vector<Key> const & range, std::vector<Key> const & keys )
    {
        auto best{ ns::max() };
        for ( auto pass{ 0 }; pass < passes; ++pass )
        {
            std::size_t acc{ 0 };
            auto const start{ timer::now() };
            for ( auto const key : keys )
            {
                auto const it
                {
                    linear
                        ? linear_lower_bound( range.data(), range.data() + range.size(), key, std::less<>{} )
                        :   std::lower_bound( range.data(), range.data() + range.size(), key, std::less<>{} )
                };
                acc += static_cast<std::size_t>( it - range.data() );
            }
            auto const elapsed{ ns{ timer::now() - start } / keys.size() };
            sink = acc;
            best = std::min( best, elapsed );
        }
        return best;
    }

    // The regime matters more than the length.  A resident array is searched
    // with every cache line already present, which is the best case for a
    // binary search (its scattered probes never miss) and the worst for a
    // linear scan (nothing to overlap its mispredicted exit branch with).  A
    // b+tree node is the opposite: it has just been pointer-chased to, so it is
    // cold, and a linear scan streams it under the hardware prefetcher while a
    // binary search issues dependent misses.  Measuring only the resident case
    // answers a question nobody is asking.
    //
    // COLD sweep: enough independent ranges to blow the LLC, one search each.
    template <typename Key>
    [[ nodiscard ]] ns time_search_cold( std::vector<Key> const & pool, std::uint32_t const n,
                                         std::vector<std::uint32_t> const & which,
                                         std::vector<Key> const & keys )
    {
        auto best{ ns::max() };
        for ( auto pass{ 0 }; pass < passes; ++pass )
        {
            std::size_t acc{ 0 };
            auto const start{ timer::now() };
            for ( std::size_t i{ 0 }; i < which.size(); ++i )
            {
                auto const * const first{ pool.data() + std::size_t( which[ i ] ) * n };
                auto const it{ linear_lower_bound( first, first + n, keys[ i ], std::less<>{} ) };
                acc += static_cast<std::size_t>( it - first );
            }
            best = std::min( best, ns{ timer::now() - start } / which.size() );
            sink = acc;
        }
        return best;
    }
    template <typename Key>
    [[ nodiscard ]] ns time_search_cold_binary( std::vector<Key> const & pool, std::uint32_t const n,
                                                std::vector<std::uint32_t> const & which,
                                                std::vector<Key> const & keys )
    {
        auto best{ ns::max() };
        for ( auto pass{ 0 }; pass < passes; ++pass )
        {
            std::size_t acc{ 0 };
            auto const start{ timer::now() };
            for ( std::size_t i{ 0 }; i < which.size(); ++i )
            {
                auto const * const first{ pool.data() + std::size_t( which[ i ] ) * n };
                auto const it{ std::lower_bound( first, first + n, keys[ i ], std::less<>{} ) };
                acc += static_cast<std::size_t>( it - first );
            }
            best = std::min( best, ns{ timer::now() - start } / which.size() );
            sink = acc;
        }
        return best;
    }

    template <typename Key>
    void sweep_cold( char const * const type_name )
    {
        std::mt19937 rng{ PSI_VM_BENCH_SEED };
        constexpr std::size_t pool_bytes{ 256ull * 1024 * 1024 }; // >> any LLC here
        constexpr std::uint32_t cold_probes{ 300'000 };
        std::println( "\n{} ({} bytes) COLD - one search per range, {} MB of ranges",
                      type_name, sizeof( Key ), pool_bytes / 1024 / 1024 );
        std::println( "  values |  bytes | linear ns | binary ns | winner | margin" );
        std::uint32_t cross_v{ 0 }, cross_b{ 0 };
        for ( auto const n : lengths )
        {
            auto const count{ static_cast<std::uint32_t>( pool_bytes / ( n * sizeof( Key ) ) ) };
            std::vector<Key> pool( std::size_t( count ) * n );
            for ( std::uint32_t r{ 0 }; r < count; ++r ) {
                for ( std::uint32_t i{ 0 }; i < n; ++i ) { pool[ std::size_t( r ) * n + i ] = static_cast<Key>( i * 3 + 1 ); }
            }
            std::vector<std::uint32_t> which( cold_probes );
            std::vector<Key>           keys ( cold_probes );
            std::uniform_int_distribution<std::uint32_t> pick_r{ 0, count - 1 };
            std::uniform_int_distribution<std::uint32_t> pick_k{ 0, n * 3 };
            for ( std::uint32_t i{ 0 }; i < cold_probes; ++i ) {
                which[ i ] = pick_r( rng );
                keys [ i ] = static_cast<Key>( pick_k( rng ) );
            }
            auto const lin{ time_search_cold       ( pool, n, which, keys ) };
            auto const bin{ time_search_cold_binary( pool, n, which, keys ) };
            auto const bytes{ n * sizeof( Key ) };
            auto const linear_wins{ lin <= bin };
            if ( !linear_wins && !cross_v ) { cross_v = n; cross_b = static_cast<std::uint32_t>( bytes ); }
            std::println( "  {:6} | {:6} | {:9.2f} | {:9.2f} | {:6} | {:5.1f}%",
                          n, bytes, lin.count(), bin.count(), linear_wins ? "LINEAR" : "binary",
                          ( linear_wins ? ( bin.count() / lin.count() - 1 ) : ( lin.count() / bin.count() - 1 ) ) * 100 );
        }
        if ( cross_v ) { std::println( "  => COLD crossover at {} values / {} bytes", cross_v, cross_b ); }
        else           { std::println( "  => COLD: linear never loses in this range" ); }
    }

    template <typename Key>
    void sweep( char const * const type_name )
    {
        std::mt19937 rng{ PSI_VM_BENCH_SEED };
        std::println
        (
            "\n{} ({} bytes) - eligible for the linear path: {}",
            type_name, sizeof( Key ), linear_search_eligible<std::less<>, Key> ? "yes" : "no"
        );
        std::println( "  values |  bytes | linear ns | binary ns | winner | margin | shipped" );

        std::uint32_t crossover_values{ 0 };
        std::uint32_t crossover_bytes { 0 };
        for ( auto const n : lengths )
        {
            auto const range{ sorted_range<Key>( n ) };
            std::vector<Key> keys( probes );
            std::uniform_int_distribution<std::uint32_t> pick{ 0, n * 3 };
            for ( auto & k : keys ) { k = static_cast<Key>( pick( rng ) ); }

            auto const lin{ time_search<true >( range, keys ) };
            auto const bin{ time_search<false>( range, keys ) };

            auto const bytes { n * sizeof( Key ) };
            auto const linear_wins{ lin <= bin };
            if ( !linear_wins && !crossover_values ) { crossover_values = n; crossover_bytes = static_cast<std::uint32_t>( bytes ); }
            // What the shipped constant would pick for a range of this size.
            auto const shipped_linear{ ( linear_search_max_values<Key> != 0 ) && ( n <= linear_search_max_values<Key> ) && linear_search_eligible<std::less<>, Key> };
            std::println
            (
                "  {:6} | {:6} | {:9.2f} | {:9.2f} | {:6} | {:5.1f}% | {}{}",
                n, bytes, lin.count(), bin.count(), linear_wins ? "LINEAR" : "binary",
                ( linear_wins ? ( bin.count() / lin.count() - 1 ) : ( lin.count() / bin.count() - 1 ) ) * 100,
                shipped_linear ? "LINEAR" : "binary",
                ( shipped_linear == linear_wins ) ? "" : "   <-- MISMATCH"
            );
        }
        if ( crossover_values ) {
            std::println( "  => crossover at {} values / {} bytes", crossover_values, crossover_bytes );
        } else {
            std::println( "  => linear never loses in this range" );
        }
    }
} // anonymous namespace

TEST( lookup, threshold_sweep )
{
    std::println
    (
        "linear_search_max_values<uint32> = {}  (0 means the linear path is compiled out)",
        linear_search_max_values<std::uint32_t>
    );
    std::println( "\n########## RESIDENT (array already in cache) ##########" );
    sweep<std::uint16_t>( "uint16_t" );
    sweep<std::uint32_t>( "uint32_t" );
    sweep<std::uint64_t>( "uint64_t" );
    sweep<float        >( "float"    );
    sweep<double       >( "double"   );
} // lookup.threshold_sweep

TEST( lookup, threshold_sweep_cold )
{
    std::println( "\n########## COLD (a fresh range per search, as a b+tree node is) ##########" );
    sweep_cold<std::uint32_t>( "uint32_t" );
    sweep_cold<std::uint64_t>( "uint64_t" );
    sweep_cold<double       >( "double"   );
} // lookup.threshold_sweep_cold

#endif // release build

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
