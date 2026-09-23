#include <cmeta/function.h>
#include "tinytest.h"

FunctionDecl(value, int, cmeta_direct_function_header_probe,
    (int, value, CMETA_PARAM_IN));

suite("CMeta direct function header") {
  it("exposes function and interface reflection in a stable order") {
    const cmeta_function_desc *fn =
        FunctionMeta(cmeta_direct_function_header_probe);

    check_true(cmeta_function_desc_valid(fn));
    check_equal(fn->param_count, (size_t)1);
  }
}
