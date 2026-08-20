#include <cflow/cflow.h>
#include "tinytest.h"

spec("CFlow Graph Tests") {
  it("should initialize a typed source graph") {
    cflow_graph graph;

    cflow_graph_init(&graph, &cmeta_type_int);

    check_equal(graph.error == NULL, 1);
    check_equal(graph.root != CMETA_INVALID_ID, 1);
    check_equal(cmeta_type_equal(cflow_graph_source_type(&graph), &cmeta_type_int), 1);
    check_equal(cmeta_type_equal(cflow_graph_output_type(&graph), &cmeta_type_int), 1);
    check_equal(cflow_graph_is_one_to_one(&graph), 1);

    cflow_graph_destroy(&graph);
  }
}
