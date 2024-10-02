#pragma once

#include "b+tree.hpp"

#include <cstdint>
#include <print>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

template <typename Key>
void bptree_base_wkey<Key>::print() const
{
    if ( empty() )
    {
        std::puts( "The tree is empty." );
        return;
    }

    // BFS, one level of the tree at a time.
    auto p_node{ &as<inner_node>( root() ) };
    for ( auto level{ 0U }; !is_leaf_level( level ); ++level )
    {
        std::print( "Level {}:\t", std::uint16_t( level ) );

        auto p_next_level{ &node<inner_node>( children( *p_node ).front() ) };

        std::uint32_t level_node_count{ 0 };
        size_type     level_key_count { 0 };
        // Process all nodes at the current level
        for ( ; ; )
        {
            // Internal node, print keys and add children to the queue
            level_key_count += num_vals( *p_node );
            std::putchar( '<' );
            for ( auto i{ 0U }; i < num_vals( *p_node ); ++i )
            {
                std::print( "{}", keys( *p_node )[ i ] );
                if ( i < num_vals( *p_node ) - 1U )
                    std::print( ", " );
            }
            std::print( "> " );

            ++level_node_count;
            if ( !p_node->right )
                break;
            p_node = &node<inner_node>( p_node->right );
        }
        std::println( " [{} nodes w/ {} values]", level_node_count, level_key_count );

        p_node = p_next_level;
    }

    {
        std::print( "Leaf level ({}):\t", leaf_level() );
        std::uint32_t level_node_count{ 0 };
        size_type     level_key_count { 0 };
        auto p_leaf{ &as<leaf_node>( *p_node ) };
        for ( ; ; )
        {
            level_key_count += num_vals( *p_leaf );
            std::putchar( '[' );
            for ( auto i{ 0U }; i < num_vals( *p_leaf ); ++i ) {
                std::print( "{}", keys( *p_leaf )[ i ] );
                if ( i < num_vals( *p_leaf ) - 1U ) {
                    std::print( ", " );
                }
            }
            std::print( "] " );

            ++level_node_count;
            if ( !p_leaf->right )
                break;
            p_leaf = &node<leaf_node>( p_leaf->right );
        }
        std::println( " [{} nodes w/ {} values]", level_node_count, level_key_count );
    }
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
