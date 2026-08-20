#ifndef C11_STREAM_CONTAINER_H
#define C11_STREAM_CONTAINER_H

#include "stream.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STREAM_MAX_CURSOR_SIZE 128

typedef struct stream_container_ops stream_container_ops_t;

/*
 * A container becomes streamable by providing a cursor protocol.
 * The cursor is owned by each stream instance, not by the container.
 */
struct stream_container_ops {
    size_t cursor_size;

    size_t (*element_size)(const void *container);

    stream_result_t (*cursor_init)(
        const void *container,
        void *cursor);

    stream_result_t (*cursor_next)(
        const void *container,
        void *cursor,
        stream_item_t *out);

    stream_result_t (*cursor_reset)(
        const void *container,
        void *cursor);
};

stream_result_t stream_from_container(
    stream_t *stream,
    const void *container,
    const stream_container_ops_t *ops);

/* ------------------------- Array view ------------------------- */

typedef struct {
    const unsigned char *data;
    size_t count;
    size_t element_size;
} stream_array_view_t;

stream_array_view_t stream_array_view(
    const void *data,
    size_t count,
    size_t element_size);

extern const stream_container_ops_t stream_array_container_ops;

stream_result_t stream_from_array_view(
    stream_t *stream,
    const stream_array_view_t *array);

#define STREAM_FROM_ARRAY(stream_ptr, array) \
    stream_from_array_view(                    \
        (stream_ptr),                          \
        &(stream_array_view_t){                \
            (const unsigned char *)(array),    \
            sizeof(array) / sizeof((array)[0]),\
            sizeof((array)[0])                 \
        })

/* --------------------------- Vector --------------------------- */

/*
 * A small zero-allocation vector facade over caller-owned storage.
 * Structural modification increments version and invalidates active cursors.
 */
typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
    size_t element_size;
    uint64_t version;
} stream_vector_t;

void stream_vector_init(
    stream_vector_t *vector,
    void *storage,
    size_t capacity,
    size_t element_size);

stream_result_t stream_vector_push_back(
    stream_vector_t *vector,
    const void *value);

stream_result_t stream_vector_erase(
    stream_vector_t *vector,
    size_t index);

void stream_vector_clear(stream_vector_t *vector);

extern const stream_container_ops_t stream_vector_container_ops;

stream_result_t stream_from_vector(
    stream_t *stream,
    const stream_vector_t *vector);

/* ---------------------------- List ---------------------------- */

/*
 * Intrusive list adapter. Nodes and values are caller-owned, so no allocation
 * is required by the stream/container layer.
 */
typedef struct stream_list_node {
    struct stream_list_node *next;
    const void *value;
} stream_list_node_t;

typedef struct {
    stream_list_node_t *head;
    stream_list_node_t *tail;
    size_t size;
    size_t element_size;
    uint64_t version;
} stream_list_t;

void stream_list_init(stream_list_t *list, size_t element_size);
void stream_list_node_init(stream_list_node_t *node, const void *value);
stream_result_t stream_list_push_back(stream_list_t *list, stream_list_node_t *node);
void stream_list_clear(stream_list_t *list);

extern const stream_container_ops_t stream_list_container_ops;

stream_result_t stream_from_list(
    stream_t *stream,
    const stream_list_t *list);

#ifdef __cplusplus
}
#endif

#endif
