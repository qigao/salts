#include <cflow/cflow.h>

#include "adapters_internal.h"
#include "tinytest.h"

typedef struct adapter_source_probe {
    size_t destroys;
} adapter_source_probe;

static const char *adapter_source_name(void *state) {
    (void)state;
    return "adapter-source-probe";
}

static const cmeta_type_desc *adapter_source_type(void *state) {
    return state ? &cmeta_type_int : NULL;
}

static cflow_step adapter_source_resume(void *state,
                                        cflow_publish_context *context,
                                        void *out_value) {
    (void)state;
    (void)context;
    (void)out_value;
    return (cflow_step){CFLOW_STEP_DONE, {0}, NULL};
}

static void adapter_source_noop(void *state) { (void)state; }

static void adapter_source_destroy(void *state) {
    adapter_source_probe *probe = (adapter_source_probe *)state;
    if (probe) ++probe->destroys;
}

static void adapter_source_bind(void *state, cflow_waker waker) {
    (void)state;
    (void)waker;
}

static cflow_publisher_terminal adapter_source_poll(void *state,
                                                 const char **error) {
    if (error) *error = NULL;
    return state ? CFLOW_PUBLISHER_OPEN : CFLOW_PUBLISHER_ERROR;
}

CMETA_IMPLEMENTS(cflow_publisher, adapter_source_probe_interface,
    CFLOW_PUBLISHER_CAP_CONSTRUCTS_VALUES,
    .name = adapter_source_name,
    .output_type = adapter_source_type,
    .resume = adapter_source_resume,
    .cancel = adapter_source_noop,
    .destroy = adapter_source_destroy,
    .bind_terminal_waker = adapter_source_bind,
    .poll_terminal = adapter_source_poll
);

spec("CFlow synchronous adapter Source ownership") {
    it("destroys its owned Source once when Graph normalization fails") {
        adapter_source_probe probe = {0};
        cflow_graph graph = {0};
        cflow_graph normalized = {0};
        cflow_publisher source =
            adapter_source_probe_interface_as_cflow_publisher(&probe);
        const cflow_graph *executable = &graph;

        cflow_graph_init(&graph, &cmeta_type_int);
        normalized.root = CMETA_INVALID_ID;
        graph.subgraphs[graph.root].nodes[0].op = CFLOW_OP_ZIP;

        check_false(cflow_adapter_prepare_owned_source_graph(
            &normalized, &graph, &source, &executable));
        check_equal(probe.destroys, (size_t)1u);
        check_false(cflow_publisher_valid(&source));
        check_null(executable);

        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&graph);
    }
}
