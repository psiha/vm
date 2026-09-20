// Standalone correctness check for the psi.vm module (module.cmake, PSI_VM_MODULE): builds a small
// executable that only `import`s psi.vm and exercises a few real container APIs, so a broken module
// boundary (a symbol silently absent, a template that fails to instantiate through the BMI) shows up
// as a build or runtime failure here rather than surfacing only downstream in a consumer project.
import psi.vm;

#include <boost/assert.hpp>
#include <cstdio>

int main()
{
    psi::vm::fc_vector<int, 8> fc;
    fc.push_back( 1 );
    fc.push_back( 2 );
    fc.push_back( 3 );
    BOOST_ASSERT( fc.size() == 3 );

    psi::vm::heap_vector<int> heap;
    heap.push_back( 10 );
    heap.push_back( 20 );

    int const sum{ fc[ 0 ] + fc[ 1 ] + fc[ 2 ] + heap[ 0 ] + heap[ 1 ] };
    std::printf( "%d\n", sum );
    return sum == 36 ? 0 : 1;
}
