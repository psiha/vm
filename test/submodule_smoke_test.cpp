// Per-subsystem module correctness smoke test (module.cmake, PSI_VM_MODULE): a consumer that
// only needs containers pulls in none of shared_memory/mapping, and a consumer combining several
// submodules (as a real, fuller-featured user of the library would) links without conflict, even
// though each submodule independently #includes whatever upstream headers it needs (verified in
// module.cmake's own comment: no explicit `import` between these submodules is required).
import psi.vm.containers;
import psi.vm.mapping;
import psi.vm.mapped_view;
import psi.vm.file;
import psi.vm.shared_memory;
import psi.vm.containers.vm_backed;

int main()
{
    psi::vm::heap_vector<int> v;
    v.push_back( 42 );
    return v[ 0 ] == 42 ? 0 : 1;
}
