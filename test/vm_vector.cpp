#include <psi/vm/containers/vm_vector.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <system_error>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

TEST( vm_vector, anon_memory_backed )
{
    psi::vm::vm_vector<double, std::uint32_t> vec;
    vec.map_memory();
    EXPECT_EQ( vec.size(), 0 );
    vec.append_range({ 3.14, 0.14, 0.04 });
    EXPECT_EQ( vec.size(), 3 );
    EXPECT_EQ( vec[ 0 ], 3.14 );
    EXPECT_EQ( vec[ 1 ], 0.14 );
    EXPECT_EQ( vec[ 2 ], 0.04 );
    vec.grow_by( 12345678, default_init );
    // test growth (with 'probable' relocation) does not destroy contents
    EXPECT_EQ( vec[ 0 ], 3.14 );
    EXPECT_EQ( vec[ 1 ], 0.14 );
    EXPECT_EQ( vec[ 2 ], 0.04 );
}

TEST( vm_vector, file_backed )
{
    auto const test_vec{ "test.vec" };
    {
        psi::vm::vm_vector<double, std::uint16_t> vec;
        vec.map_file( test_vec, flags::named_object_construction_policy::create_new_or_truncate_existing );
        EXPECT_EQ( vec.size(), 0 );
        vec.append_range({ 3.14, 0.14, 0.04 });
        EXPECT_EQ( vec.size(), 3 );
        EXPECT_EQ( vec[ 0 ], 3.14 );
        EXPECT_EQ( vec[ 1 ], 0.14 );
        EXPECT_EQ( vec[ 2 ], 0.04 );
    }
    {
        psi::vm::vm_vector<double, std::uint16_t> vec;
        vec.map_file( test_vec, flags::named_object_construction_policy::open_existing );
        EXPECT_EQ( vec.size(), 3 );
        EXPECT_EQ( vec[ 0 ], 3.14 );
        EXPECT_EQ( vec[ 1 ], 0.14 );
        EXPECT_EQ( vec[ 2 ], 0.04 );
    }
}

// A whole-container flush has to keep working after the storage has grown past
// its initially mapped view: growth expands in place where it can, which on
// Win32 maps the extra pages over the trailing placeholder - i.e. as a second,
// adjacent view. The span stays contiguous to read and write through, but
// FlushViewOfFile() only accepts a range lying within a single view and fails
// with ERROR_INVALID_ADDRESS for anything crossing the boundary.
TEST( vm_vector, flush_after_in_place_growth )
{
    auto const test_vec{ "test_flush.vec" };
    psi::vm::vm_vector<std::uint32_t, std::uint32_t> vec;
    vec.map_file( test_vec, flags::named_object_construction_policy::create_new_or_truncate_existing );
    // Grow in small steps, well past any initial view size, flushing the whole
    // container every time.
    for ( unsigned i{ 0 }; i < 512; ++i )
    {
        vec.grow_by( 1024, default_init );
        auto result{ vec.flush_blocking()() };
        EXPECT_TRUE( result ) << "whole-container flush failed at growth step " << i;
    }
}

// A file-backed container keeps its on-disk EOF page aligned, on both the
// growth and the shrink path, so that an extending resize never has to flush
// and wait for a dirty tail block (see file_length_for in vm_vector.cpp). The
// slack this leaves is invisible: the logical size lives in the header, so the
// contents have to read back exactly - including after a close/reopen cycle,
// where the file is deliberately longer than the data it holds.
TEST( vm_vector, file_length_stays_page_aligned_across_growth_and_shrink )
{
    auto  const test_vec{ "test_align.vec" };
    auto  const file_length{ [ & ]{ return static_cast<std::size_t>( std::filesystem::file_size( test_vec ) ); } };
    std::error_code ec;
    std::filesystem::remove( test_vec, ec );

    std::uint32_t constexpr grown_count { 40'000 };
    std::uint32_t constexpr shrunk_count{  1'000 };

    std::size_t grown_length{ 0 };
    {
        psi::vm::vm_vector<std::uint32_t, std::uint32_t> vec;
        vec.map_file( test_vec, flags::named_object_construction_policy::create_new_or_truncate_existing );

        // Drive many separate capacity expansions rather than one big reserve.
        for ( std::uint32_t i{ 0 }; i < grown_count; ++i )
            vec.emplace_back( i );

        grown_length = file_length();
        EXPECT_EQ( grown_length % commit_granularity, 0U ) << "grown file length is not page aligned";
        for ( std::uint32_t i{ 0 }; i < grown_count; ++i )
            ASSERT_EQ( vec[ i ], i ) << "contents corrupted by growth at " << i;

        vec.resize( shrunk_count );
        vec.shrink_to_fit();

        auto const shrunk_length{ file_length() };
        EXPECT_EQ( shrunk_length % commit_granularity, 0U ) << "shrunk file length is not page aligned";
        EXPECT_LE( shrunk_length, grown_length ) << "a shrink must never grow the file";
        for ( std::uint32_t i{ 0 }; i < shrunk_count; ++i )
            ASSERT_EQ( vec[ i ], i ) << "contents corrupted by shrink at " << i;
    }

    // The file outlives the mapping longer than its logical content: reopening
    // must report the stored size, not the (larger, aligned) file length.
    {
        psi::vm::vm_vector<std::uint32_t, std::uint32_t> vec;
        vec.map_file( test_vec, flags::named_object_construction_policy::open_existing );
        ASSERT_EQ( vec.size(), shrunk_count );
        for ( std::uint32_t i{ 0 }; i < shrunk_count; ++i )
            ASSERT_EQ( vec[ i ], i ) << "contents corrupted across reopen at " << i;
    }

    std::filesystem::remove( test_vec, ec );
}


// The persisted length denotes the last COMMITTED extent, not an in-flight
// cursor: growth alone must not move it - only a header-covering flush (or an
// orderly detach) publishes it.
TEST( vm_vector, committed_size_is_not_moved_by_growth )
{
    auto const test_vec{ "committed_size.vec" };
    std::filesystem::remove( test_vec );
    psi::vm::vm_vector<double, std::uint16_t> vec;
    vec.map_file( test_vec, flags::named_object_construction_policy::create_new_or_truncate_existing );
    EXPECT_EQ( vec.size(), 0 );
    EXPECT_EQ( vec.committed_size(), 0 );

    vec.append_range({ 3.14, 0.14, 0.04 });
    // live length moved, committed one did not
    EXPECT_EQ( vec.size(), 3 );
    EXPECT_EQ( vec.committed_size(), 0 );

    (void)vec.flush_blocking()();
    // after a header-covering flush the two agree
    EXPECT_EQ( vec.size(), 3 );
    EXPECT_EQ( vec.committed_size(), 3 );

    // ...and further growth again only moves the live one
    vec.append_range({ 1.0, 2.0 });
    EXPECT_EQ( vec.size(), 5 );
    EXPECT_EQ( vec.committed_size(), 3 );
}

// The point of the change: a length that was never published is not visible to
// a reopen - the header only ever holds a length some commit point published,
// never an in-flight cursor. This is a statement about PUBLISHING, not about
// fsync: the test asserts what the mapping's header describes, and makes no
// claim that any particular value was forced to the device. A process dying
// without publishing is simulated faithfully by leaking the container so that
// no destructor (and therefore no write-through) ever runs.
TEST( vm_vector, unpublished_growth_is_not_visible_to_a_reopen )
{
    auto const test_vec{ "committed_size_crash.vec" };
    std::filesystem::remove( test_vec );
    {
        auto * const leaked{ new psi::vm::vm_vector<double, std::uint16_t>{} }; // deliberately never deleted
        leaked->map_file( test_vec, flags::named_object_construction_policy::create_new_or_truncate_existing );
        leaked->append_range({ 3.14, 0.14, 0.04 });
        (void)leaked->flush_blocking()(); // publishes (and flushes) length 3
        leaked->append_range({ 1.0, 2.0 });                      // unpublished growth: live 5
        EXPECT_EQ( leaked->size(), 5 );
        EXPECT_EQ( leaked->committed_size(), 3 );
        // no destruction, no close, no further flush: nothing publishes the 5
    }
    {
        psi::vm::vm_vector<double, std::uint16_t> reopened;
        reopened.map_file( test_vec, flags::named_object_construction_policy::open_existing );
        EXPECT_EQ( reopened.size(), 3 ); // the published extent, NOT the in-flight 5
        EXPECT_EQ( reopened.committed_size(), 3 );
        EXPECT_EQ( reopened[ 0 ], 3.14 );
        EXPECT_EQ( reopened[ 2 ], 0.04 );
    }
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
