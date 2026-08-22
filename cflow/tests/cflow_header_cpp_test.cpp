#include <cflow/cflow.h>
#include "tinytest.h"

#include <type_traits>

static_assert(std::is_standard_layout<cflow_run>::value,
              "cflow_run must remain a C-compatible handle");
static_assert(std::is_standard_layout<cmeta_callable>::value,
              "cmeta_callable must remain a C-compatible value");

suite("CFlow C++ public header") {
    it("exposes the aggregate API to C++ consumers") {
        cflow_run run = {};
        cflow_source source = {};
        cflow_graph graph = {};
        cflow_stream stream = {};
        cflow_subscription subscription = {};
        cflow_result result = {};

        check_null(run.impl);
        check_false(cflow_source_valid(&source));
        cflow_graph_init(&graph, &cmeta_type_int);
        check_true(cmeta_type_equal(
            cflow_graph_source_type(&graph), &cmeta_type_int));
        check_true(cflow_graph_structural_equal(&graph, &graph));
        cflow_graph_destroy(&graph);

        check_true(cflow_stream_init(&stream, &cmeta_type_int) == &stream);
        cflow_stream_destroy(&stream);
        check_false(cflow_subscription_is_done(&subscription));
        cflow_result_destroy(&result);
    }
}
