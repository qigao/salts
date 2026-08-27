#include <turbostl/stream.h>

#include "tinytest.hpp"

#include <algorithm>
#include <type_traits>

using turbostl_async_collect_fn = cflow_stream_execution_status (*)(
    turbostl_stream_execution_t *, const turbostl_stream_t *,
    cflow_scheduler *, cmeta_collector);
using turbostl_count_fn = turbostl_count_result (*)(
    const turbostl_stream_t *);

static_assert(std::is_standard_layout<turbostl_stream_execution_t>::value,
              "the async Stream handle must remain C-compatible");
static_assert(std::is_same<decltype(&turbostl_stream_collect_async),
                           turbostl_async_collect_fn>::value,
              "the C++ facade must preserve the C async terminal signature");
static_assert(std::is_same<decltype(&turbostl_stream_count),
                           turbostl_count_fn>::value,
              "the C++ facade must preserve the C count terminal signature");

spec("TurboSTL C++ Stream facade") {
    it("exposes the generic asynchronous collector macro") {
        turbostl_stream_execution_t execution = {};
        turbostl_stream_t stream = {};
        cflow_scheduler scheduler = {};
        list_t output = TURBO_STL_LIST_INITIALIZER(int);

        check_equal(collect_async(
                        &execution, &stream, &scheduler, &output, 4u),
                    CFLOW_STREAM_EXECUTION_INVALID_SCHEDULER);
        check_null(execution.impl);
        list_destroy(&output);
    }

    it("coexists with the standard count algorithm") {
        const int values[] = {1, 2, 2, 3};

        check_equal(std::count(values, values + 4, 2), 2);
    }
}
