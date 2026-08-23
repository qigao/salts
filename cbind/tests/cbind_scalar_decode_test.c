#include <cbind/cbind.h>
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>

spec("CBind public ABI records") {
  it("initializes a caller-sized context") {
    unsigned char scratch[3] = {0};
    cbind_context context = CBIND_CONTEXT_INIT(scratch, sizeof(scratch), 2u);

    check_equal(context.struct_size, sizeof(cbind_context));
    check_equal(context.abi_version, (uint32_t)CBIND_CONTEXT_ABI_VERSION);
    check_true(context.scratch == scratch);
    check_equal(context.scratch_size, sizeof(scratch));
    check_equal(context.max_depth, (size_t)2u);
  }

  it("initializes a caller-sized error record") {
    cbind_error error = CBIND_ERROR_INIT;

    check_equal(error.struct_size, sizeof(cbind_error));
    check_equal(error.abi_version, (uint32_t)CBIND_ERROR_ABI_VERSION);
    check_equal(error.status, CBIND_OK);
    check_equal(error.source_status, CSERDE_OK);
    check_null(error.shape);
    check_null(error.field);
    check_equal(error.depth, (size_t)0u);
  }

  it("uses stable v1 enum and record ABI versions") {
    check_equal(CBIND_OK, 0);
    check_equal(CBIND_CONTEXT_ABI_VERSION, 1u);
    check_equal(CBIND_ERROR_ABI_VERSION, 1u);
  }
}
