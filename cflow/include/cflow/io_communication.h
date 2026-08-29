#ifndef CFLOW_IO_COMMUNICATION_H
#define CFLOW_IO_COMMUNICATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file io_communication.h
 * Value-only messages shared by CFlow I/O frontends and model-specific
 * drivers. These structures do not own native handles, buffers, or callback
 * state and may be copied between bounded queues.
 *
 * Model types are deliberately distinct: callers must not reinterpret one
 * command or event structure as another model's structure.
 */

typedef enum cflow_io_communication_model {
    CFLOW_IO_COMMUNICATION_READINESS = 0,
    CFLOW_IO_COMMUNICATION_COMPLETION,
    CFLOW_IO_COMMUNICATION_BLOCKING
} cflow_io_communication_model;

/** Stable endpoint identity used by lifecycle control commands. */
typedef struct cflow_io_endpoint_identity {
    uint64_t endpoint_id;
    uint64_t generation;
} cflow_io_endpoint_identity;

/**
 * Validate a lifecycle endpoint identity.
 * @param identity Borrowed endpoint identity; may be NULL.
 * @return true only when endpoint and generation are nonzero.
 */
bool cflow_io_endpoint_identity_valid(
    const cflow_io_endpoint_identity *identity);

typedef enum cflow_io_control_command_kind {
    CFLOW_IO_CONTROL_COMMAND_CLOSE = 0
} cflow_io_control_command_kind;

/**
 * Model-independent lifecycle control. Close stops endpoint admission; it is
 * not a request command and therefore carries no request or operation ID.
 */
typedef struct cflow_io_control_command {
    cflow_io_control_command_kind kind;
    cflow_io_endpoint_identity endpoint;
} cflow_io_control_command;

/**
 * Validate a model-independent lifecycle command.
 * @param command Borrowed command; may be NULL.
 * @return true only for CLOSE with a valid endpoint identity.
 */
bool cflow_io_control_command_valid(
    const cflow_io_control_command *command);

/**
 * Stable cross-queue identity. Every field is nonzero; generation changes
 * before a reused endpoint or request slot can publish another event.
 */
typedef struct cflow_io_identity {
    uint64_t endpoint_id;
    uint64_t request_id;
    uint64_t generation;
} cflow_io_identity;

/**
 * Validate a stable identity.
 * @param identity Borrowed identity; may be NULL.
 * @return true only when identity is non-NULL and every field is nonzero.
 */
bool cflow_io_identity_valid(const cflow_io_identity *identity);

typedef uint32_t cflow_io_readiness_events;

enum {
    CFLOW_IO_READY_READ = 1u << 0u,
    CFLOW_IO_READY_WRITE = 1u << 1u,
    CFLOW_IO_READY_ERROR = 1u << 2u,
    CFLOW_IO_READY_HANGUP = 1u << 3u
};

typedef enum cflow_io_readiness_command_kind {
    CFLOW_IO_READINESS_COMMAND_WATCH = 0,
    CFLOW_IO_READINESS_COMMAND_MODIFY,
    CFLOW_IO_READINESS_COMMAND_UNWATCH
} cflow_io_readiness_command_kind;

typedef struct cflow_io_readiness_command {
    cflow_io_readiness_command_kind kind;
    cflow_io_identity identity;
    cflow_io_readiness_events interests;
} cflow_io_readiness_command;

typedef enum cflow_io_readiness_event_kind {
    CFLOW_IO_READINESS_EVENT_READY = 0,
    CFLOW_IO_READINESS_EVENT_FAILED,
    CFLOW_IO_READINESS_EVENT_CLOSED
} cflow_io_readiness_event_kind;

typedef struct cflow_io_readiness_event {
    cflow_io_readiness_event_kind kind;
    cflow_io_identity identity;
    cflow_io_readiness_events events;
    int error;
} cflow_io_readiness_event;

/**
 * Validate a readiness command without consulting native registration state.
 * WATCH/MODIFY require a nonempty supported interest mask; UNWATCH requires
 * a zero mask. Endpoint close uses cflow_io_control_command.
 * @param command Borrowed command; may be NULL.
 * @return true when all value-level invariants hold, otherwise false.
 */
bool cflow_io_readiness_command_valid(
    const cflow_io_readiness_command *command);
/**
 * Validate a readiness observation. READY carries supported events and no
 * error, FAILED carries only a nonzero error, and CLOSED carries neither.
 * @param event Borrowed event; may be NULL.
 * @return true when exactly one readiness outcome is encoded.
 */
bool cflow_io_readiness_event_valid(
    const cflow_io_readiness_event *event);

typedef enum cflow_io_completion_command_kind {
    CFLOW_IO_COMPLETION_COMMAND_SUBMIT = 0,
    CFLOW_IO_COMPLETION_COMMAND_CANCEL
} cflow_io_completion_command_kind;

typedef struct cflow_io_completion_command {
    cflow_io_completion_command_kind kind;
    cflow_io_identity identity;
    uint64_t operation_id;
} cflow_io_completion_command;

typedef enum cflow_io_completion_event_kind {
    CFLOW_IO_COMPLETION_EVENT_COMPLETED = 0,
    CFLOW_IO_COMPLETION_EVENT_EOF,
    CFLOW_IO_COMPLETION_EVENT_CANCELLED,
    CFLOW_IO_COMPLETION_EVENT_FAILED
} cflow_io_completion_event_kind;

typedef struct cflow_io_completion_event {
    cflow_io_completion_event_kind kind;
    cflow_io_identity identity;
    size_t transferred;
    int error;
} cflow_io_completion_event;

/**
 * Validate a completion command. SUBMIT/CANCEL require a nonzero operation
 * ID. Endpoint close uses cflow_io_control_command.
 * @param command Borrowed command; may be NULL.
 * @return true when all value-level invariants hold, otherwise false.
 */
bool cflow_io_completion_command_valid(
    const cflow_io_completion_command *command);
/**
 * Validate a terminal completion event.
 * @param event Borrowed event; may be NULL.
 * @return true for completed(bytes, no error), EOF(no bytes, no error),
 * cancelled(no bytes, no error), or failed(no bytes, nonzero error);
 * otherwise false.
 */
bool cflow_io_completion_event_valid(
    const cflow_io_completion_event *event);

typedef enum cflow_io_blocking_command_kind {
    CFLOW_IO_BLOCKING_COMMAND_EXECUTE = 0,
    CFLOW_IO_BLOCKING_COMMAND_CANCEL
} cflow_io_blocking_command_kind;

typedef struct cflow_io_blocking_command {
    cflow_io_blocking_command_kind kind;
    cflow_io_identity identity;
    uint64_t job_id;
} cflow_io_blocking_command;

typedef enum cflow_io_blocking_event_kind {
    CFLOW_IO_BLOCKING_EVENT_COMPLETED = 0,
    CFLOW_IO_BLOCKING_EVENT_EOF,
    CFLOW_IO_BLOCKING_EVENT_CANCELLED,
    CFLOW_IO_BLOCKING_EVENT_FAILED
} cflow_io_blocking_event_kind;

typedef struct cflow_io_blocking_event {
    cflow_io_blocking_event_kind kind;
    cflow_io_identity identity;
    size_t transferred;
    int error;
} cflow_io_blocking_event;

/**
 * Validate a blocking-worker command. EXECUTE/CANCEL require a nonzero job
 * ID. Endpoint close uses cflow_io_control_command.
 * @param command Borrowed command; may be NULL.
 * @return true when all value-level invariants hold, otherwise false.
 */
bool cflow_io_blocking_command_valid(
    const cflow_io_blocking_command *command);
/**
 * Validate a terminal blocking-worker event.
 * @param event Borrowed event; may be NULL.
 * @return true for completed(bytes, no error), EOF(no bytes, no error),
 * cancelled(no bytes, no error), or failed(no bytes, nonzero error);
 * otherwise false.
 */
bool cflow_io_blocking_event_valid(
    const cflow_io_blocking_event *event);

/**
 * Example:
 * @code
 * cflow_io_completion_command command = {
 *     CFLOW_IO_COMPLETION_COMMAND_SUBMIT, {1u, 7u, 2u}, 11u};
 * bool valid = cflow_io_completion_command_valid(&command);
 * @endcode
 */

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_IO_COMMUNICATION_H */
