#ifndef C11_STREAM_STREAMABLE_H
#define C11_STREAM_STREAMABLE_H

#include "stream_container.h"

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Declare a low-boilerplate adapter for an application container.
 *
 * Contract:
 *   - CursorType must be valid when zero-initialized.
 *   - typed_next_fn has the signature:
 *
 *       stream_result_t typed_next_fn(
 *           const ContainerType *container,
 *           CursorType *cursor,
 *           ElementType *out);
 *
 *   - The typed next function returns STREAM_OK and writes *out for one item,
 *     STREAM_END when traversal is complete, STREAM_MODIFIED for fail-fast
 *     invalidation, or another stream_result_t on failure.
 *   - reset() is provided automatically by zeroing the per-stream cursor.
 *
 * The macro generates a stream_container_ops_t plus a typed stream factory:
 *
 *       stream_result_t name##_stream(
 *           stream_t *stream,
 *           const ContainerType *container);
 *
 * Example:
 *
 *   STREAMABLE(user_ring, user_ring_t, user_ring_cursor_t, User,
 *              user_ring_next)
 *
 *   user_ring_stream(&stream, &ring);
 */
#define STREAMABLE(name, ContainerType, CursorType, ElementType, typed_next_fn)       \
    typedef struct name##_stream_cursor {                                              \
        CursorType user;                                                               \
        uint64_t sequence;                                                             \
    } name##_stream_cursor_t;                                                          \
                                                                                       \
    static size_t name##_stream_element_size(const void *container)                    \
    {                                                                                  \
        (void)container;                                                               \
        return sizeof(ElementType);                                                     \
    }                                                                                  \
                                                                                       \
    static stream_result_t name##_stream_cursor_init(                                  \
        const void *container,                                                         \
        void *cursor)                                                                  \
    {                                                                                  \
        name##_stream_cursor_t *c = (name##_stream_cursor_t *)cursor;                  \
        (void)container;                                                               \
        if (!c) {                                                                      \
            return STREAM_ERROR;                                                       \
        }                                                                              \
        memset(c, 0, sizeof(*c));                                                       \
        return STREAM_OK;                                                              \
    }                                                                                  \
                                                                                       \
    static stream_result_t name##_stream_cursor_next(                                  \
        const void *container,                                                         \
        void *cursor,                                                                  \
        stream_item_t *out)                                                            \
    {                                                                                  \
        name##_stream_cursor_t *c = (name##_stream_cursor_t *)cursor;                  \
        ElementType value;                                                             \
        stream_result_t r;                                                             \
                                                                                       \
        if (!container || !c || !out || !out->data) {                                 \
            return STREAM_ERROR;                                                       \
        }                                                                              \
                                                                                       \
        r = typed_next_fn(                                                              \
            (const ContainerType *)container,                                          \
            &c->user,                                                                  \
            &value);                                                                   \
                                                                                       \
        if (r != STREAM_OK) {                                                          \
            return r;                                                                  \
        }                                                                              \
                                                                                       \
        memcpy(out->data, &value, sizeof(value));                                      \
        out->size = sizeof(value);                                                      \
        out->timestamp_ns = 0;                                                         \
        out->sequence = c->sequence++;                                                  \
        return STREAM_OK;                                                              \
    }                                                                                  \
                                                                                       \
    static const stream_container_ops_t name##_stream_ops = {                          \
        sizeof(name##_stream_cursor_t),                                                 \
        name##_stream_element_size,                                                     \
        name##_stream_cursor_init,                                                      \
        name##_stream_cursor_next,                                                      \
        name##_stream_cursor_init                                                       \
    };                                                                                 \
                                                                                       \
    static stream_result_t name##_stream(                                              \
        stream_t *stream,                                                              \
        const ContainerType *container)                                                \
    {                                                                                  \
        return stream_from_container(stream, container, &name##_stream_ops);            \
    }


/*
 * Reference/borrowed variant. typed_next_ref_fn has the signature:
 *
 *   stream_result_t typed_next_ref_fn(
 *       const ContainerType *container,
 *       CursorType *cursor,
 *       const ElementType **out);
 *
 * No element copy is performed by the adapter. The returned pointer must
 * remain valid until the next traversal step or structural container change.
 */
#define STREAMABLE_REF(name, ContainerType, CursorType, ElementType, typed_next_ref_fn) \
    typedef struct name##_stream_cursor {                                               \
        CursorType user;                                                                \
        uint64_t sequence;                                                              \
    } name##_stream_cursor_t;                                                           \
                                                                                        \
    static size_t name##_stream_element_size(const void *container)                     \
    {                                                                                   \
        (void)container;                                                                \
        return sizeof(ElementType);                                                      \
    }                                                                                   \
                                                                                        \
    static stream_result_t name##_stream_cursor_init(                                   \
        const void *container,                                                          \
        void *cursor)                                                                   \
    {                                                                                   \
        name##_stream_cursor_t *c = (name##_stream_cursor_t *)cursor;                   \
        (void)container;                                                                \
        if (!c) {                                                                       \
            return STREAM_ERROR;                                                        \
        }                                                                               \
        memset(c, 0, sizeof(*c));                                                        \
        return STREAM_OK;                                                               \
    }                                                                                   \
                                                                                        \
    static stream_result_t name##_stream_cursor_next(                                   \
        const void *container,                                                          \
        void *cursor,                                                                   \
        stream_item_t *out)                                                             \
    {                                                                                   \
        name##_stream_cursor_t *c = (name##_stream_cursor_t *)cursor;                   \
        const ElementType *value = NULL;                                                \
        stream_result_t r;                                                              \
                                                                                        \
        if (!container || !c || !out) {                                                 \
            return STREAM_ERROR;                                                        \
        }                                                                               \
                                                                                        \
        r = typed_next_ref_fn(                                                          \
            (const ContainerType *)container,                                           \
            &c->user,                                                                   \
            &value);                                                                    \
                                                                                        \
        if (r != STREAM_OK) {                                                           \
            return r;                                                                   \
        }                                                                               \
        if (!value) {                                                                   \
            return STREAM_ERROR;                                                        \
        }                                                                               \
                                                                                        \
        out->data = (void *)value;                                                      \
        out->size = sizeof(*value);                                                      \
        out->timestamp_ns = 0;                                                          \
        out->sequence = c->sequence++;                                                   \
        return STREAM_OK;                                                               \
    }                                                                                   \
                                                                                        \
    static const stream_container_ops_t name##_stream_ops = {                           \
        sizeof(name##_stream_cursor_t),                                                  \
        name##_stream_element_size,                                                      \
        name##_stream_cursor_init,                                                       \
        name##_stream_cursor_next,                                                       \
        name##_stream_cursor_init                                                        \
    };                                                                                  \
                                                                                        \
    static stream_result_t name##_stream(                                               \
        stream_t *stream,                                                               \
        const ContainerType *container)                                                 \
    {                                                                                   \
        return stream_from_container(stream, container, &name##_stream_ops);             \
    }

#ifdef __cplusplus
}
#endif

#endif
