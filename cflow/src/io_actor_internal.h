#ifndef CFLOW_IO_ACTOR_INTERNAL_H
#define CFLOW_IO_ACTOR_INTERNAL_H

#include <cflow/io_actor.h>

typedef struct cflow_io_completion_batch {
    void *impl;
} cflow_io_completion_batch;

/*
 * A native backend keeps one batch token for one native completion batch.
 * publish() lazily opens the Actor publisher scope and switches scopes when a
 * backend batch contains requests from different Actors. end() releases the
 * final scope and emits at most one wake for the gathered Actor completions.
 */
cflow_io_complete_status cflow_io_actor_completion_batch_publish(
    cflow_io_completion_batch *batch,
    cflow_io_actor *actor,
    cflow_io_request_id request_id,
    const cflow_io_completion *completion);

void cflow_io_actor_completion_batch_end(
    cflow_io_completion_batch *batch);

/*
 * Native backends publish one copied terminal completion through this bounded
 * MPSC boundary. This single-item wrapper closes its batch before returning.
 * A successful call reserves the request terminal state before returning, so
 * backend-owned result resources may be transferred exactly as with
 * cflow_io_actor_complete().
 */
cflow_io_complete_status cflow_io_actor_publish_completion(
    cflow_io_actor *actor,
    cflow_io_request_id request_id,
    const cflow_io_completion *completion);

#endif /* CFLOW_IO_ACTOR_INTERNAL_H */
