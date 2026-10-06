#include "cflow_executor_dso_fixture.h"
#include "tinytest.h"

spec("CFlow static-library DSO executor dispatch") {
  it("uses the creating provider across an EXE/DLL boundary") {
    cflow_executor executor = {0};
    cflow_executor_dso_probe probe = {
        .executor = &executor,
    };

    const void *dll_worker_vtable;

    check_true(cflow_executor_worker_init_with_capacity(&executor, 1u, 4u));
    dll_worker_vtable = cflow_executor_dso_worker_vtable();
    check_not_null(dll_worker_vtable);
    check((const void *)executor.vtable != dll_worker_vtable);
    check_equal(cflow_executor_dso_submit(&executor, &probe), 0);
    check_equal(atomic_load(&probe.run_count), 2);
    check_equal(atomic_load(&probe.cancel_count), 0);
    check_equal(atomic_load(&probe.finalize_count), 2);
    check_equal(atomic_load(&probe.current_count), 2);
    cflow_executor_destroy(&executor);
  }
}
