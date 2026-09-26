#ifndef CFLOW_IO_CNET_ADAPTER_H
#define CFLOW_IO_CNET_ADAPTER_H

#include <cflow/io_actor.h>
#include <cnet/cnet.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Caller-owned receive slot used by the CNet session adapter.
 *
 * CNet receive callback bytes are borrowed only for callback duration, so the
 * adapter copies at most capacity bytes into this caller-owned buffer before
 * completing the matching CFlow IO Actor request. No fallback allocation is
 * performed. size and kind are output fields.
 */
typedef struct cflow_io_cnet_receive_operation {
    void *buffer;
    size_t capacity;
    size_t size;
    cnet_message_kind kind;
} cflow_io_cnet_receive_operation;

/**
 * Thin single-connection bridge from one caller-owned CNet client/session to
 * the existing CFlow IO Actor/Publisher protocol.
 *
 * The adapter does not own the CNet client, connection, poll loop, socket,
 * NativeIO request, CFlow Actor, Publisher, Scheduler, or Executor.
 */
typedef struct cflow_io_cnet_session_adapter {
    void *impl;
} cflow_io_cnet_session_adapter;

typedef struct cflow_io_cnet_session_adapter_config {
    /** Borrowed until adapter destroy. All backend drive runs on this client owner. */
    cnet_client *client;
    /** Fixed number of outstanding CFlow receive credits. */
    size_t bridge_capacity;
    /** Optional forwarded CNet state callback; receive remains adapter-owned. */
    cnet_state_fn on_state;
    /** Optional forwarded CNet send callback. */
    cnet_send_fn on_send;
    void *observer_user;
} cflow_io_cnet_session_adapter_config;

typedef struct cflow_io_cnet_session_adapter_stats {
    size_t bridge_capacity;
    size_t active_bridges;
    size_t pending_credits;
    size_t cancelled_tombstones;
    uint64_t admitted_credits;
    uint64_t received_values;
    uint64_t copied_bytes;
    uint64_t cancelled_requests;
    uint64_t discarded_cancelled_values;
    uint64_t terminal_drains;
    uint64_t stale_callbacks;
    cnet_connection connection;
    cnet_connection_state state;
    bool bound;
    bool state_known;
    bool terminal;
    bool closed;
} cflow_io_cnet_session_adapter_stats;

/**
 * Allocates only fixed bridge/order metadata. client remains caller-owned.
 *
 * Adapter APIs, CFlow backend callbacks, and the returned CNet observer are
 * single-owner and must execute on the same non-overlapping owner lane used to
 * call cnet_client_poll().
 */
int cflow_io_cnet_session_adapter_init(
    cflow_io_cnet_session_adapter *adapter,
    const cflow_io_cnet_session_adapter_config *config);

/**
 * Returns the observer that must be installed on the CNet connection.
 *
 * The adapter owns on_receive so it can map one CNet receive credit to one
 * CFlow request. Optional user on_state/on_send hooks configured at init are
 * invoked after adapter state accounting.
 */
cnet_observer cflow_io_cnet_session_adapter_observer(
    cflow_io_cnet_session_adapter *adapter);

/**
 * Binds exactly one generation-checked CNet connection.
 *
 * Call after cnet_connect()/accept returns and before polling that connection.
 * The adapter never closes, destroys, or polls the connection.
 */
int cflow_io_cnet_session_adapter_bind(
    cflow_io_cnet_session_adapter *adapter,
    cnet_connection connection);

/**
 * Backend for cflow_io_actor / cflow_publisher_from_io_actor().
 *
 * operation_user must point to cflow_io_cnet_receive_operation storage whose
 * lifetime is owned by the submitted cflow_io_operation token.
 *
 * submit maps one CFlow request to cnet_receive(client, connection, 1).
 * cancel does not call cnet_close(): CNet has no per-credit receive-cancel API.
 * Instead the CFlow request becomes CANCELLED and the already-admitted CNet
 * credit remains as one bounded tombstone. The next receive value or CNet
 * terminal state consumes that tombstone without publishing a CFlow value.
 */
cflow_io_backend_ops cflow_io_cnet_session_adapter_actor_ops(void);

/** Stops new CFlow receive-credit admission; does not close the CNet session. */
int cflow_io_cnet_session_adapter_close(
    cflow_io_cnet_session_adapter *adapter);

/** Owner-lane snapshot; does not poll CNet or advance CFlow. */
bool cflow_io_cnet_session_adapter_get_stats(
    const cflow_io_cnet_session_adapter *adapter,
    cflow_io_cnet_session_adapter_stats *out_stats);

/**
 * Releases fixed adapter metadata.
 *
 * Requires adapter close plus complete credit/tombstone drain. The caller must
 * separately close/poll/stop the CNet session as needed and close/drain the
 * CFlow Actor/Publisher before destroying this adapter.
 */
int cflow_io_cnet_session_adapter_destroy(
    cflow_io_cnet_session_adapter *adapter);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_IO_CNET_ADAPTER_H */
