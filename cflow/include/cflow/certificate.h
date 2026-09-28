#ifndef CFLOW_CERTIFICATE_H
#define CFLOW_CERTIFICATE_H

#include <cflow/graph.h>
#include <cflow/plan.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { CFLOW_PLAN_CERTIFICATE_V1 = 1u };

typedef enum cflow_certified_opcode {
    CFLOW_CERTIFIED_FILTER = 0,
    CFLOW_CERTIFIED_MAP = 1,
    CFLOW_CERTIFIED_FLAT_MAP = 2,
    CFLOW_CERTIFIED_REDUCE = 3,
    CFLOW_CERTIFIED_TAKE = 4,
    CFLOW_CERTIFIED_SKIP = 5
} cflow_certified_opcode;

typedef enum cflow_certified_path {
    CFLOW_CERTIFIED_PATH_SEQUENTIAL = 0,
    CFLOW_CERTIFIED_PATH_ORDERED_PARALLEL_REDUCE = 1
} cflow_certified_path;

typedef enum cflow_certificate_order {
    CFLOW_CERTIFICATE_ORDER_NOT_APPLICABLE = 0,
    CFLOW_CERTIFICATE_ORDER_ENCOUNTER = 1
} cflow_certificate_order;

typedef struct cflow_plan_certificate_row {
    uint32_t opcode;
    uint32_t instruction_index;
    uint32_t callable_index;
    uint32_t effects;
    uint32_t properties;
    const cmeta_type_desc *input_type;
    const cmeta_type_desc *output_type;
    cmeta_callable callable;
    bool has_size_parameter;
    size_t size_parameter;
} cflow_plan_certificate_row;

/* Execution-only witness. Pointer-bearing rows are owned by this instance and
 * are neither a wire format nor stable persistent data. */
typedef struct cflow_plan_certificate {
    uint32_t version;
    uint32_t path;
    uint32_t order;
    uint32_t required_capabilities;
    uint64_t graph_version;
    uint64_t graph_fingerprint;
    cflow_plan_certificate_row *rows;
    size_t row_count;
} cflow_plan_certificate;

/* certificate must be zero-initialized or previously destroyed. Failure
 * leaves the existing instance unchanged. */
bool cflow_plan_certificate_build(cflow_plan_certificate *certificate,
                                  const cflow_graph *normalized_graph,
                                  const cflow_plan *plan,
                                  cflow_certified_path path);
void cflow_plan_certificate_destroy(cflow_plan_certificate *certificate);
bool cflow_plan_certificate_check(const cflow_plan_certificate *certificate,
                                  const cflow_graph *normalized_graph,
                                  const cflow_plan *plan,
                                  const char **error);

#ifdef __cplusplus
}
#endif
#endif /* CFLOW_CERTIFICATE_H */
