#ifndef CFLOW_COORD_H
#define CFLOW_COORD_H

#include <cflow/reactive.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

Enum(cflow_coord_mode,
    (CFLOW_COORD_ALL,      "all"),
    (CFLOW_COORD_ALL_DONE, "all_done"),
    (CFLOW_COORD_ANY,      "any"),
    (CFLOW_COORD_LATEST,   "latest"),
    (CFLOW_COORD_SEQUENCE, "sequence")
);

typedef struct cflow_coord_event {
    size_t child_index; /* SIZE_MAX means an ALL barrier became ready. */
    size_t generation;
} cflow_coord_event;

extern const cmeta_type_desc cflow_type_coord_event;

/* Move-style construction. On success the coordinator owns every child and
 * clears children[0..count). All child output types may differ. Retained
 * values use each output type's trivial or COPY/MOVE/DESTROY lifecycle. */
bool cflow_resumable_from_coordination(cflow_resumable *out,
                                        cflow_coord_mode mode,
                                        cflow_resumable *children,
                                        size_t count);

/* One-shot typed value machine, useful as a participant in coordination.
 * The machine owns an independent copy of value. */
bool cflow_resumable_from_value(cflow_resumable *out,
                                 const cmeta_type_desc *type,
                                 const void *value);

/* Latest retained typed value for one child. The pointer remains owned by the
 * coordinator and is valid until the next resume/destroy. */
bool cflow_coord_value(const cflow_resumable *coord,
                       size_t child_index,
                       const cmeta_type_desc **type,
                       const void **value);

#ifdef __cplusplus
}
#endif
#endif
