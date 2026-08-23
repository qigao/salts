#ifndef CFLOW_PROPERTY_H
#define CFLOW_PROPERTY_H

#include <cflow/graph.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Positive semantic guarantees over immutable typed IR. Zero means that no
 * guarantee has been proved. DETERMINISTIC/TOTAL/NO_ALIAS are conservatively
 * compositional. IDEMPOTENT and ASSOCIATIVE are algebraic laws on specific
 * callable shapes, not arbitrary pipeline guarantees: IDEMPOTENT is only
 * reported for a unary endomorphism (or a Subgraph consisting of exactly one
 * such semantic step), while ASSOCIATIVE remains a function-local admission
 * contract for binary endomorphisms. */
bool cmeta_callable_has_properties(cmeta_callable fn, cmeta_properties required);
/* Runtime declaration gate used by the optimizer and certificate checker.
 * This validates metadata shape; the callable producer remains responsible
 * for the mathematical idempotence law. */
bool cflow_callable_declares_idempotent_endomap(cmeta_callable fn);
/* Admission gate for ordered reassociation. This validates the declared
 * binary endomorphism contract; the producer remains responsible for the
 * mathematical associativity law. */
bool cflow_callable_declares_associative_endomap(cmeta_callable fn);
cmeta_properties cflow_node_properties(const cflow_graph *g, const cflow_node *node);
cmeta_properties cflow_subgraph_properties(const cflow_graph *g, cflow_subgraph_id subgraph);
cmeta_properties cflow_graph_properties(const cflow_graph *g);

#ifdef __cplusplus
}
#endif
#endif
