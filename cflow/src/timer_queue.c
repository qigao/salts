#include "timer_queue.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool ensure_capacity(cflow_timer_queue *queue, size_t need) {
    size_t capacity;
    cflow_timer_task *items;

    if (need <= queue->capacity) return true;
    capacity = queue->capacity ? queue->capacity : 16u;
    while (capacity < need) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = need;
            break;
        }
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(*queue->items)) return false;
    items = (cflow_timer_task *)realloc(queue->items,
                                        capacity * sizeof(*queue->items));
    if (!items) return false;
    queue->items = items;
    queue->capacity = capacity;
    return true;
}

static size_t earliest_index(const cflow_timer_queue *queue) {
    size_t best = SIZE_MAX;
    for (size_t i = 0; i < queue->count; ++i) {
        const cflow_timer_task *candidate = &queue->items[i];
        if (best == SIZE_MAX ||
            candidate->deadline.ns < queue->items[best].deadline.ns ||
            (candidate->deadline.ns == queue->items[best].deadline.ns &&
             candidate->order < queue->items[best].order)) {
            best = i;
        }
    }
    return best;
}

static void remove_at(cflow_timer_queue *queue, size_t index) {
    if (index + 1u < queue->count) {
        memmove(&queue->items[index], &queue->items[index + 1u],
                (queue->count - index - 1u) * sizeof(queue->items[0]));
    }
    --queue->count;
}

bool cflow_timer_queue_init(cflow_timer_queue *queue) {
    if (!queue) return false;
    memset(queue, 0, sizeof(*queue));
    queue->next_id = 1u;
    return true;
}

void cflow_timer_queue_destroy(cflow_timer_queue *queue) {
    if (!queue) return;
    free(queue->items);
    memset(queue, 0, sizeof(*queue));
}

cflow_timer_id cflow_timer_queue_schedule(cflow_timer_queue *queue,
                                          cflow_deadline deadline,
                                          cflow_task_fn fn,
                                          void *user) {
    cflow_timer_id id;
    if (!queue || !fn || queue->count == SIZE_MAX ||
        queue->next_id == 0u || queue->next_order == UINT64_MAX ||
        !ensure_capacity(queue, queue->count + 1u))
        return 0u;

    id = queue->next_id++;
    queue->items[queue->count++] = (cflow_timer_task){
        .id = id,
        .deadline = deadline,
        .order = queue->next_order++,
        .fn = fn,
        .user = user
    };
    return id;
}

bool cflow_timer_queue_cancel(cflow_timer_queue *queue, cflow_timer_id id) {
    if (!queue || id == 0u) return false;
    for (size_t i = 0; i < queue->count; ++i) {
        if (queue->items[i].id == id) {
            remove_at(queue, i);
            return true;
        }
    }
    return false;
}

bool cflow_timer_queue_next_deadline(const cflow_timer_queue *queue,
                                     cflow_deadline *out) {
    size_t index;
    if (!queue || queue->count == 0u || !out) return false;
    index = earliest_index(queue);
    if (index == SIZE_MAX) return false;
    *out = queue->items[index].deadline;
    return true;
}

bool cflow_timer_queue_take_ready(cflow_timer_queue *queue,
                                  cflow_instant now,
                                  cflow_timer_task *out) {
    size_t index;
    if (!queue || queue->count == 0u || !out) return false;
    index = earliest_index(queue);
    if (index == SIZE_MAX || queue->items[index].deadline.ns > now.ns) return false;
    *out = queue->items[index];
    remove_at(queue, index);
    return true;
}

size_t cflow_timer_queue_pending(const cflow_timer_queue *queue) {
    return queue ? queue->count : 0u;
}
