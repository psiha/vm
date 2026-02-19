////////////////////////////////////////////////////////////////////////////////
/// Codegen comparison for small_vector layouts.
///
/// Not a test — standalone functions for disassembly inspection.
/// Build in Release, disassemble with:
///   objdump -d -M intel --no-show-raw-insn small_vector_codegen.o | c++filt
///   dumpbin /disasm small_vector_codegen.obj
///
/// Compare the generated code for each layout variant:
///   compact (MSB flag):  size() = mask, data() = branch on MSB, inc = ++size_
///   compact_lsb (LSB):   size() = shift, data() = branch on LSB, inc = size_ += 2
///   pointer_based:       size() = load, data() = load, inc = ++size_
////////////////////////////////////////////////////////////////////////////////

#include <psi/vm/containers/small_vector.hpp>

namespace psi::vm::codegen
{

inline constexpr small_vector_options lsb_opts{ .layout = small_vector_layout::compact_lsb   };
inline constexpr small_vector_options pb_opts { .layout = small_vector_layout::pointer_based };

using sv_compact = small_vector<int, 8>;
using sv_lsb     = small_vector<int, 8, std::size_t, lsb_opts>;
using sv_pointer = small_vector<int, 8, std::size_t, pb_opts>;

// --- size() ---
[[ gnu::noinline ]] std::size_t get_size_compact( sv_compact const & v ) { return v.size(); }
[[ gnu::noinline ]] std::size_t get_size_lsb    ( sv_lsb     const & v ) { return v.size(); }
[[ gnu::noinline ]] std::size_t get_size_pointer ( sv_pointer const & v ) { return v.size(); }

// --- data() ---
[[ gnu::noinline ]] int const * get_data_compact( sv_compact const & v ) { return v.data(); }
[[ gnu::noinline ]] int const * get_data_lsb    ( sv_lsb     const & v ) { return v.data(); }
[[ gnu::noinline ]] int const * get_data_pointer ( sv_pointer const & v ) { return v.data(); }

// --- push_back (exercises grow path + size increment) ---
[[ gnu::noinline ]] void push_compact( sv_compact & v, int x ) { v.push_back( x ); }
[[ gnu::noinline ]] void push_lsb    ( sv_lsb     & v, int x ) { v.push_back( x ); }
[[ gnu::noinline ]] void push_pointer( sv_pointer & v, int x ) { v.push_back( x ); }

// --- operator[] (data + offset) ---
[[ gnu::noinline ]] int read_compact( sv_compact const & v, std::size_t i ) { return v[ i ]; }
[[ gnu::noinline ]] int read_lsb    ( sv_lsb     const & v, std::size_t i ) { return v[ i ]; }
[[ gnu::noinline ]] int read_pointer( sv_pointer const & v, std::size_t i ) { return v[ i ]; }

// --- capacity() ---
[[ gnu::noinline ]] std::size_t get_cap_compact( sv_compact const & v ) { return v.capacity(); }
[[ gnu::noinline ]] std::size_t get_cap_lsb    ( sv_lsb     const & v ) { return v.capacity(); }
[[ gnu::noinline ]] std::size_t get_cap_pointer ( sv_pointer const & v ) { return v.capacity(); }

// --- push_back loop (realistic hot path) ---
[[ gnu::noinline ]] void push_loop_compact( sv_compact & v, int n )
{
    for ( int i{ 0 }; i < n; ++i )
        v.push_back( i );
}
[[ gnu::noinline ]] void push_loop_lsb( sv_lsb & v, int n )
{
    for ( int i{ 0 }; i < n; ++i )
        v.push_back( i );
}
[[ gnu::noinline ]] void push_loop_pointer( sv_pointer & v, int n )
{
    for ( int i{ 0 }; i < n; ++i )
        v.push_back( i );
}

} // namespace psi::vm::codegen
