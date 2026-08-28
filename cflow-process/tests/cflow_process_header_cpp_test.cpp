#include <cflow/process.h>

#include "tinytest.h"

#include <type_traits>

static_assert(std::is_standard_layout<cflow_process>::value,
              "process adapter must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_process_config>::value,
              "process config must remain C-compatible");
static_assert(std::is_standard_layout<cflow_process_submit_result>::value,
              "process submit result must remain C-compatible");
static_assert(std::is_standard_layout<cflow_process_stats>::value,
              "process stats must remain C-compatible");

spec("CFlow process C++ header") {
  it("preserves the public C enum ordering") {
    check_true(CFLOW_PROCESS_STDIN == 0);
    check_true(CFLOW_PROCESS_STDOUT == 1);
    check_true(CFLOW_PROCESS_STDERR == 2);
  }
}
