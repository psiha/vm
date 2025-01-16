#pragma once

#include <version>

#include <psi/build/disable_warnings.hpp>
#if !defined( _LIBCPP_VERSION ) || ( _LIBCPP_VERSION < 190000 ) // weird compilation errors mentioning va_list (tested w/ 19.1.6 and 19.1.7)
#include <psi/err/fallible_result.hpp>
#endif

#include <boost/assert.hpp>
#if !defined( _LIBCPP_VERSION ) || ( _LIBCPP_VERSION < 190000 )
#include <boost/config_ex.hpp>

#include <boost/stl_interfaces/iterator_interface.hpp>
#include <boost/stl_interfaces/sequence_container_interface.hpp>
#endif

#include <algorithm>
#include <array>
#include <std_fix/bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#if !defined( _LIBCPP_VERSION ) || ( _LIBCPP_VERSION < 190000 )
#include <functional>
#endif
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <tuple>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#undef ERROR
#endif