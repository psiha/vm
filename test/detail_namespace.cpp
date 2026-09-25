// psi::vm::detail must stay unambiguous in a translation unit that includes a
// shared memory header between headers that declare and use psi::vm::detail
// (sort, the containers): the platform backends live in inline namespaces
// (win32, posix), so a namespace of theirs named `detail` would be found
// alongside psi::vm::detail by every later lookup of `detail` in psi::vm.

#include <psi/vm/containers/abi.hpp> // declares psi::vm::detail
#include <psi/vm/mappable_objects/shared_memory/mem.hpp>

#include <psi/vm/containers/flat_map.hpp>
#include <psi/vm/containers/strided_vector.hpp>
#include <psi/vm/containers/vector.hpp>
#include <psi/vm/sort.hpp>

#include <gtest/gtest.h>

#include <array>
#include <functional>
#include <type_traits>
//------------------------------------------------------------------------------
namespace psi::vm
{
//------------------------------------------------------------------------------

static_assert( !detail::erasure_applies<std::less<>, int> );
static_assert( !::psi::vm::detail::erasure_applies<std::less<>, int> );
static_assert( std::is_class_v<named_memory<lifetime_policy::scoped, resizing_policy::fixed>> );

TEST( detail_namespace, shared_memory_before_sort_and_containers )
{
    std::array<int, 4> values{ 3, 1, 4, 2 };
    vm::sort( values.begin(), values.end(), std::less<>{} );
    EXPECT_EQ( values, ( std::array<int, 4>{ 1, 2, 3, 4 } ) );
}

//------------------------------------------------------------------------------
} // namespace psi::vm
//------------------------------------------------------------------------------
