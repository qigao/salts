#ifndef CFLOW_IO_ACTOR_INTERNAL_H
#define CFLOW_IO_ACTOR_INTERNAL_H

#include <cflow/io_actor.h>

/*
 * Native backends publish one copied terminal completion through this bounded
 * MPSC boundary. A successful call reserves the request terminal state before
 * returning, so backend-owned result resources may be transferred exactly as
 * with cflow_io_actor_complete().
 */
cflow_io_complete_status cflow_io_actor_publish_completion(
    cflow_io_actor *actor,
    cflow_io_request_id request_id,
    const cflow_io_completion *completion);

#endif /* CFLOW_IO_ACTOR_INTERNAL_H */
