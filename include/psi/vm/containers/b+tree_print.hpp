#pragma once

#include "b+tree.hpp"

#include <print>
#include <queue>
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

    // Queue for performing level-order traversal (BFS).
    std::queue<inner_node const *> nodes;
    nodes.push( &as<inner_node>( root() ) );

    // Current level tracker
    depth_t level{ 0 };

    // Perform BFS, processing one level of the tree at a time.
    while ( !nodes.empty() )
    {
        auto node_count{ nodes.size() };
        std::print( "Level {} ({} nodes):\n\t", std::uint16_t( level ), node_count );

        size_t level_key_count{ 0 };
        // Process all nodes at the current level
        while ( node_count > 0 )
        {
            auto const node{ nodes.front() };
            nodes.pop();

            if ( is_leaf_level( level ) )
            {
                auto & ln{ as<leaf_node>( *node ) };
                level_key_count += num_vals( ln );
                std::putchar( '[' );
                for ( auto i{ 0U }; i < num_vals( ln ); ++i )
                {
                    std::print( "{}", keys( ln )[ i ] );
                    if ( i < num_vals( ln ) - 1 )
                        std::print( ", " );
                }
                std::print( "] " );
            }
            else
            {
                // Internal node, print keys and add children to the queue
                level_key_count += num_vals( *node );
                std::putchar( '<' );
                for ( auto i{ 0U }; i < num_vals( *node ); ++i )
                {
                    std::print( "{}", keys( *node )[ i ] );
                    if ( i < num_vals( *node ) - 1 )
                        std::print( ", " );
                }
                std::print( "> " );

                // Add all children of this internal node to the queue
                for ( auto i{ 0 }; i < num_chldrn( *node ); ++i )
                    nodes.push( &this->node<inner_node>( node->children[ i ] ) );
            }

            --node_count;
        }

        std::println( " [{} values]", level_key_count );
        ++level;
    }
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
