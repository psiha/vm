#pragma once

#include <psi/build/disable_warnings.hpp>
#include <psi/err/fallible_result.hpp>

#include <boost/assert.hpp>
#include <boost/config_ex.hpp>
#include <boost/stl_interfaces/iterator_interface.hpp>
#include <boost/stl_interfaces/sequence_container_interface.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
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
#include <std_fix/bit>

#ifdef _WIN32
#include <windows.h>
#undef ERROR
#endif