#ifndef CFLOW_MINICORO_H
#define CFLOW_MINICORO_H

#include <cflow/reactive.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_minicoro cflow_minicoro;

typedef void (*cflow_minicoro_entry_fn)(cflow_minicoro *coroutine,
                                        void *user);
typedef void *(*cflow_minicoro_alloc_fn)(size_t size,
                                         void *allocator_data);
typedef void (*cflow_minicoro_dealloc_fn)(void *pointer,
                                          size_t size,
                                          void *allocator_data);

typedef struct cflow_minicoro_config {
    const char *name;
    const cmeta_type_desc *output_type;
    cflow_minicoro_entry_fn entry;
    void *user;
    size_t stack_size;
    cflow_minicoro_alloc_fn alloc;
    cflow_minicoro_dealloc_fn dealloc;
    void *allocator_data;
} cflow_minicoro_config;

/* Create a minicoro-backed Resumable. The returned Resumable owns its adapter
 * state and coroutine frame. name, output_type, entry user data, yielded error
 * text, waitable state, allocator callbacks, and allocator_data remain
 * borrowed through destroy or the narrower invalidation point documented by
 * the corresponding suspension operation. alloc and dealloc must be supplied
 * as a pair.
 *
 * The adapter is single-owner. resume, cancel, and destroy must not overlap.
 * Current retained-byte composition requires output_type to declare both
 * TRIVIAL_COPY and TRIVIAL_DESTROY. cancel permanently terminates the frame,
 * cancels an active waitable, and makes later resume calls return DONE without
 * executing code after the suspension. destroy performs cancel, frame
 * destruction, then adapter-state release. Construction failure leaves *out
 * unchanged. */
bool cflow_resumable_from_minicoro(cflow_resumable *out,
                                    const cflow_minicoro_config *config);

/* Suspend with VALUE. value is copied into the current resume call's
 * out_value before that resume returns and is not retained across the next
 * resume. The function returns when the owner explicitly resumes the frame. */
bool cflow_minicoro_yield_value(cflow_minicoro *coroutine,
                                const void *value);

/* Suspend with VALUE_AND_DONE. A conforming owner never resumes this frame
 * again; destroy releases the suspended frame. */
bool cflow_minicoro_return_value(cflow_minicoro *coroutine,
                                 const void *value);

/* Suspend with WAIT using an existing CFlow waitable. Its state is borrowed
 * until the next resume, cancel, or destroy. Waking only notifies the waitable
 * owner; it never resumes the coroutine frame directly. */
bool cflow_minicoro_wait(cflow_minicoro *coroutine,
                         cflow_waitable waitable);

/* Suspend with ERROR. error is borrowed until the returned step is consumed
 * or the Resumable is destroyed. */
bool cflow_minicoro_fail(cflow_minicoro *coroutine,
                         const char *error);

/* Borrow the exact CFlow resume context supplied to the active resume call.
 * The result is NULL outside the running callback and must not be retained
 * across a suspension point. No fallback scheduler is created. */
cflow_publish_context *cflow_minicoro_resume_context(
    cflow_minicoro *coroutine);

#ifdef __cplusplus
}
#endif

#endif
