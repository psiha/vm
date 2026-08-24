////////////////////////////////////////////////////////////////////////////////
/// Codegen comparison for small_vector layouts.
///
/// Not a test -- standalone functions for disassembly inspection.
/// Build in Release, disassemble with:
///   objdump -d -M intel --no-show-raw-insn small_vector_codegen.o | c++filt
///   dumpbin /disasm small_vector_codegen.obj
///
/// Compare the generated code for each layout variant:
///   compact (MSB flag):  size() = mask, data() = branch on MSB, inc = ++size_
///   compact_lsb (LSB):   size() = shift, data() = branch on LSB, inc = size_ += 2
///   embedded (CIS):      size() = shift, data() = branch on LSB, inc = sz_ += 2 (no external size_)
////////////////////////////////////////////////////////////////////////////////

#include <psi/vm/containers/small_vector.hpp>

namespace psi::vm::codegen
{

inline constexpr sbo_options msb_opts{ .layout = sbo_layout::compact       };
inline constexpr sbo_options lsb_opts{ .layout = sbo_layout::compact_lsb   };
inline constexpr sbo_options emb_opts{ .layout = sbo_layout::embedded      };

using sv_compact  = small_vector<int, 8, std::uint32_t, msb_opts>;
using sv_lsb      = small_vector<int, 8, std::size_t  , lsb_opts>;
using sv_embedded = small_vector<int, 8, std::size_t  , emb_opts>;

// --- size() ---
[[ gnu::noinline ]] std::size_t get_size_compact( sv_compact const & v ) { return v.size(); }
[[ gnu::noinline ]] std::size_t get_size_lsb    ( sv_lsb     const & v ) { return v.size(); }

// --- data() ---
[[ gnu::noinline ]] int const * get_data_compact( sv_compact const & v ) { return v.data(); }
[[ gnu::noinline ]] int const * get_data_lsb    ( sv_lsb     const & v ) { return v.data(); }

// --- push_back (exercises grow path + size increment) ---
[[ gnu::noinline ]] void push_compact( sv_compact & v, int x ) { v.push_back( x ); }
[[ gnu::noinline ]] void push_lsb    ( sv_lsb     & v, int x ) { v.push_back( x ); }

// --- operator[] (data + offset) ---
[[ gnu::noinline ]] int read_compact( sv_compact const & v, std::size_t i ) { return v[ i ]; }
[[ gnu::noinline ]] int read_lsb    ( sv_lsb     const & v, std::size_t i ) { return v[ i ]; }

// --- capacity() ---
[[ gnu::noinline ]] std::size_t get_cap_compact( sv_compact const & v ) { return v.capacity(); }
[[ gnu::noinline ]] std::size_t get_cap_lsb    ( sv_lsb     const & v ) { return v.capacity(); }

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

// --- embedded: size() ---
[[ gnu::noinline ]] std::size_t get_size_embedded( sv_embedded const & v ) { return v.size(); }

// --- embedded: data() ---
[[ gnu::noinline ]] int const * get_data_embedded( sv_embedded const & v ) { return v.data(); }

// --- embedded: push_back ---
[[ gnu::noinline ]] void push_embedded( sv_embedded & v, int x ) { v.push_back( x ); }

// --- embedded: operator[] ---
[[ gnu::noinline ]] int read_embedded( sv_embedded const & v, std::size_t i ) { return v[ i ]; }

// --- embedded: capacity() ---
[[ gnu::noinline ]] std::size_t get_cap_embedded( sv_embedded const & v ) { return v.capacity(); }

// --- embedded: push_back loop ---
[[ gnu::noinline ]] void push_loop_embedded( sv_embedded & v, int n )
{
    for ( int i{ 0 }; i < n; ++i )
        v.push_back( i );
}

// --- inline size field: narrowed vs left at the heap arm's width ---
//
// Same N and same size type on both sides -- only alignof( T ) decides whether
// the inline arm's size field narrows, so the pair isolates what the narrowing
// costs the accessors. Diff these two against a build that narrows nothing:
// `uniform` must come out unchanged, `narrowed` is the price of the extra
// inline capacity.

inline constexpr sbo_options packed_opts{ .pack_inline_size = true };

using sv_narrowed = small_vector<std::uint8_t , 12, std::uint32_t, packed_opts>; // alignof( T ) < sizeof( sz_t )
using sv_uniform  = small_vector<std::uint32_t,  3, std::uint32_t, packed_opts>; // alignof( T ) == sizeof( sz_t )

#define PSI_VM_CODEGEN_SBO_ACCESSORS( tag, sv )                                                      \
    [[ gnu::noinline ]] std::uint32_t size_    ## tag( sv const & v ) { return v.size();     }        \
    [[ gnu::noinline ]] bool          empty_   ## tag( sv const & v ) { return v.empty();    }        \
    [[ gnu::noinline ]] std::uint32_t capacity_## tag( sv const & v ) { return v.capacity(); }        \
    [[ gnu::noinline ]] auto          data_    ## tag( sv const & v ) { return v.data();     }        \
    [[ gnu::noinline ]] void          push_    ## tag( sv & v, sv::value_type x ) { v.push_back( x ); } \
    [[ gnu::noinline ]] void push_loop_ ## tag( sv & v, int n )                                       \
    {                                                                                                \
        for ( int i{ 0 }; i < n; ++i )                                                                \
            v.push_back( static_cast<sv::value_type>( i ) );                                          \
    }                                                                                                \
    /* size() re-read on every iteration -- the shape the accessor cost shows in */                   \
    [[ gnu::noinline ]] std::uint32_t sum_ ## tag( sv const & v )                                     \
    {                                                                                                \
        std::uint32_t s{ 0 };                                                                         \
        for ( std::uint32_t i{ 0 }; i < v.size(); ++i )                                               \
            s += v[ i ];                                                                              \
        return s;                                                                                     \
    }

PSI_VM_CODEGEN_SBO_ACCESSORS( narrowed, sv_narrowed )
PSI_VM_CODEGEN_SBO_ACCESSORS( uniform , sv_uniform  )

} // namespace psi::vm::codegen
