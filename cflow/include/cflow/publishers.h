#ifndef CFLOW_PUBLISHERS_H
#define CFLOW_PUBLISHERS_H

#include <cflow/reactive.h>
#include <cmeta/range.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Every Publisher constructor requires a zero-initialized destination. A live
 * Publisher is never overwritten when construction fails. */

/* Finite contiguous array -> Publisher. The returned Publisher owns only
 * its small cursor state; the caller retains every array element until
 * Subscription
 * close. Trivial elements are copied as bytes. Managed elements must provide
 * COPY, MOVE, and DESTROY traits and are copy-constructed per resume. */
bool cflow_publisher_from_array(cflow_publisher *out,
                             const cmeta_type_desc *type,
                             const void *data,
                             size_t count);

/* Borrowed CMeta Range -> Publisher. The Publisher owns only cursor state;
 * the underlying Range object must remain valid until Publisher/Subscription
 * close.
 * A managed element type requires COPY, MOVE, and DESTROY traits plus
 * CMETA_RANGE_CONSTRUCTS_VALUES. Such a Range next() callback constructs a live
 * value only when returning VALUE or VALUE_AND_DONE. */
bool cflow_publisher_from_range(cflow_publisher *out, cmeta_range range);

/* Timer Publisher. Each requested output waits interval_ticks on the
 * Subscription Scheduler. count==0 is already complete. Cancel/destroy
 * prevents future wake
 * delivery and waits for an executing wake to return, except for safe
 * destruction reentered from that same wake callback. */
bool cflow_publisher_from_timer(cflow_publisher *out,
                             size_t count,
                             uint64_t interval_ticks);

/* Channel is a resource/control-plane object. The Graph sees only its Publisher.
 * Stored values must have TRIVIAL_COPY and TRIVIAL_DESTROY traits. */
typedef struct cflow_channel {
    void *impl;
} cflow_channel;

/** Exact result of a non-blocking Channel admission attempt. */
typedef enum cflow_channel_status {
    CFLOW_CHANNEL_OK = 0,
    CFLOW_CHANNEL_INVALID_ARGUMENT,
    CFLOW_CHANNEL_FULL,
    CFLOW_CHANNEL_CLOSED
} cflow_channel_status;

/** Mutex-consistent snapshot of bounded Channel state and admission outcomes. */
typedef struct cflow_channel_stats {
    size_t capacity;
    size_t pending;
    size_t peak_pending;
    uint64_t accepted;
    uint64_t received;
    uint64_t rejected_full;
    uint64_t rejected_closed;
} cflow_channel_stats;

/**
 * Initialize fixed-capacity trivial-copy storage.
 *
 * Example:
 * @code
 * cflow_channel channel = {0};
 * int value = 7;
 * if (cflow_channel_init(&channel, &cmeta_type_int, 16u)) {
 *     cflow_channel_status status =
 *         cflow_channel_try_push(&channel, &value);
 *     cflow_channel_destroy(&channel);
 * }
 * @endcode
 *
 * @param ch Zero-initialized destination; a live handle is rejected unchanged.
 * @param type Borrowed trivial-copy/trivial-destroy descriptor that outlives ch.
 * @param capacity Non-zero maximum pending value count.
 * @return true on success; false for invalid input, occupied output, overflow,
 *         or allocation failure.
 */
bool cflow_channel_init(cflow_channel *ch,
                        const cmeta_type_desc *type,
                        size_t capacity);
/**
 * Try one non-blocking admission.
 *
 * @param ch Borrowed initialized Channel.
 * @param value Borrowed value copied before this call returns.
 * @return `OK`, `FULL`, `CLOSED`, or `INVALID_ARGUMENT` exactly.
 */
cflow_channel_status cflow_channel_try_push(cflow_channel *ch,
                                            const void *value);
/** Compatibility wrapper returning true only for `CFLOW_CHANNEL_OK`. */
bool cflow_channel_push(cflow_channel *ch, const void *value);
/**
 * Read one mutex-consistent bounded-resource snapshot.
 *
 * @param ch Borrowed initialized Channel; destruction must not race.
 * @param out Required destination overwritten only on success.
 * @return true on success; false for invalid arguments.
 */
bool cflow_channel_get_stats(const cflow_channel *ch,
                             cflow_channel_stats *out);
void cflow_channel_close(cflow_channel *ch);
void cflow_channel_destroy(cflow_channel *ch);
bool cflow_publisher_from_channel(cflow_publisher *out, cflow_channel *ch);

/* Generic readiness/completion adapter for real IO/UI drivers. Its value type
 * must have TRIVIAL_COPY and TRIVIAL_DESTROY traits.
 * read() may return WOULD_BLOCK; arm() then registers the supplied waker with
 * epoll/kqueue/IOCP/libuv/Qt/etc. The Subscription never knows the driver kind.
 *
 * `cancel` is required, idempotent, and accepts no active registration. It is
 * a quiescent unwatch boundary: after it returns, the driver no longer retains
 * or invokes the previously armed waker. `arm` may invoke the waker inline.
 * Publisher destruction calls cancel before close. */
Enum(cflow_read_status,
    (CFLOW_READ_VALUE,          "value"),
    (CFLOW_READ_VALUE_AND_DONE, "value_and_done"),
    (CFLOW_READ_WOULD_BLOCK,    "would_block"),
    (CFLOW_READ_DONE,           "done"),
    (CFLOW_READ_ERROR,          "error")
);

typedef cflow_read_status (*cflow_read_fn)(void *user,
                                           void *out_value,
                                           const char **error);
typedef bool (*cflow_watch_fn)(void *user, cflow_waker waker);
typedef void (*cflow_unwatch_fn)(void *user);
typedef void (*cflow_resource_close_fn)(void *user);

bool cflow_publisher_from_readiness(cflow_publisher *out,
                                 const char *name,
                                 const cmeta_type_desc *type,
                                 cflow_read_fn read,
                                 cflow_watch_fn arm,
                                 cflow_unwatch_fn cancel,
                                 cflow_resource_close_fn close,
                                 void *user);

#ifdef __cplusplus
}
#endif
#endif
