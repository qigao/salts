#ifndef CFLOW_SOURCES_H
#define CFLOW_SOURCES_H

#include <cflow/runtime.h>
#include <cmeta/range.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Finite contiguous array -> resumable source. The returned source owns only
 * its small cursor state; the caller retains the array bytes until run close. */
bool cflow_source_from_array(cflow_source *out,
                             const cmeta_type_desc *type,
                             const void *data,
                             size_t count);

/* Borrowed CMeta Range -> resumable source. The source owns only cursor state;
 * the underlying range object must remain valid until the source/run closes. */
bool cflow_source_from_range(cflow_source *out, cmeta_range range);

/* Timer source. Each requested output waits interval_ticks on the run scheduler.
 * count==0 is an already-complete source. */
bool cflow_source_from_timer(cflow_source *out,
                             size_t count,
                             uint64_t interval_ticks);

/* Channel is a resource/control-plane object. The graph sees only its source. */
typedef struct cflow_channel {
    void *impl;
} cflow_channel;

bool cflow_channel_init(cflow_channel *ch,
                        const cmeta_type_desc *type,
                        size_t capacity);
bool cflow_channel_push(cflow_channel *ch, const void *value);
void cflow_channel_close(cflow_channel *ch);
void cflow_channel_destroy(cflow_channel *ch);
bool cflow_source_from_channel(cflow_source *out, cflow_channel *ch);

/* Generic readiness/completion adapter for real IO/UI drivers.
 * read() may return WOULD_BLOCK; arm() then registers the supplied waker with
 * epoll/kqueue/IOCP/libuv/Qt/etc. Runtime itself never knows the driver kind. */
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

bool cflow_source_from_readiness(cflow_source *out,
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
