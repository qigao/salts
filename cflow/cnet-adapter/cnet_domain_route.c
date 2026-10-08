#include <cflow/cnet_domain_route.h>

#include <salts/error_codes.h>
#include <salts/thread.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const cmeta_type_traits route_delivery_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY
};

const cmeta_type_desc cflow_cnet_domain_route_delivery_type = {
    .name = "cflow_cnet_domain_route_delivery",
    .size = sizeof(cflow_cnet_domain_route_delivery),
    .align = _Alignof(cflow_cnet_domain_route_delivery),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &route_delivery_traits,
    .identity = NULL
};

typedef enum route_slot_phase {
    ROUTE_SLOT_FREE = 0,
    ROUTE_SLOT_CREDIT,
    ROUTE_SLOT_STAGED,
    ROUTE_SLOT_DELIVERED
} route_slot_phase;

typedef struct route_slot {
    uint64_t generation;
    size_t size;
    cnet_message_kind kind;
    route_slot_phase phase;
} route_slot;

typedef struct route_impl {
    cmeta_mutex_t mutex;
    cflow_actor_ref actor;
    route_slot *slots;
    unsigned char *payload;
    const void *target_thread;
    const void *source_thread;
    cnet_connection connection;
    uint64_t incarnation;
    cflow_event_id event_id;
    uint32_t source_owner;
    uint32_t credit_index;
    uint32_t stage_index;
    size_t slot_capacity;
    size_t max_receive_bytes;
    size_t active_slots;
    size_t awaiting_ack;
    size_t retained_bytes;
    size_t peak_retained_bytes;
    uint64_t reserved_credits;
    uint64_t rollback_credits;
    uint64_t received_values;
    uint64_t actor_accepted;
    uint64_t actor_full;
    uint64_t acknowledged;
    uint64_t abandoned;
    int fatal_status;
    bool source_bound;
    bool credit_live;
    bool stage_live;
    bool sealed;
    bool source_terminal;
} route_impl;

static _Atomic uint64_t next_route_incarnation = 1u;

static bool route_new_incarnation(uint64_t *out) {
    uint64_t id = atomic_load_explicit(&next_route_incarnation,
                                        memory_order_relaxed);
    while (id != UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(
                &next_route_incarnation, &id, id + 1u,
                memory_order_acq_rel, memory_order_relaxed)) {
            *out = id;
            return true;
        }
    }
    return false;
}

static route_impl *route_get(cflow_cnet_domain_route *route) {
    return route ? (route_impl *)route->impl : NULL;
}

static bool route_on_target(const route_impl *s) {
    return s && s->target_thread == cmeta_thread_current_token();
}

static bool route_on_source(const route_impl *s) {
    return s && s->source_bound &&
           s->source_thread == cmeta_thread_current_token();
}

static bool route_same_connection(cnet_connection a, cnet_connection b) {
    return a.slot == b.slot && a.generation == b.generation;
}

static unsigned char *route_payload(route_impl *s, size_t i) {
    return s->payload + i * s->max_receive_bytes;
}

static void route_return_slot_locked(route_impl *s, size_t i) {
    route_slot *slot = &s->slots[i];
    if (slot->phase == ROUTE_SLOT_FREE || s->active_slots == 0u)
        abort(); /* No duplicate settlement or forged release. */
    if (slot->phase == ROUTE_SLOT_STAGED ||
        slot->phase == ROUTE_SLOT_DELIVERED) {
        if (s->retained_bytes < slot->size) abort();
        s->retained_bytes -= slot->size;
        if (slot->size != 0u)
            memset(route_payload(s, i), 0, slot->size);
    }
    if (slot->phase == ROUTE_SLOT_DELIVERED) {
        if (s->awaiting_ack == 0u) abort();
        --s->awaiting_ack;
    }
    --s->active_slots;
    slot->phase = ROUTE_SLOT_FREE;
    slot->size = 0u;
    slot->kind = (cnet_message_kind)0;
}

static bool route_credit_valid_locked(
    const route_impl *s, cflow_cnet_domain_route_credit credit) {
    return s->credit_live && credit.incarnation == s->incarnation &&
           credit.slot == s->credit_index &&
           credit.slot < s->slot_capacity &&
           credit.source_owner == s->source_owner &&
           route_same_connection(credit.connection, s->connection) &&
           s->slots[credit.slot].phase == ROUTE_SLOT_CREDIT &&
           s->slots[credit.slot].generation == credit.generation;
}

static int route_set_fatal_locked(route_impl *s, int reason) {
    if (s->fatal_status == SALTS_OK) s->fatal_status = reason;
    s->sealed = true; /* Only this source route is sealed, not its peers. */
    return s->fatal_status;
}

/* Called with the ROUTE mutex held. Actor's MPMC Mailbox is the sole event
 * FIFO; the route retains at most one unsent payload lease, not another queue.
 * Neither Actor send nor Scheduler wake may synchronously drive callbacks
 * into this route (the published Actor/Executor protocol requires this). */
static int route_try_actor_locked(route_impl *s) {
    route_slot *slot;
    cflow_cnet_domain_route_delivery delivery;
    cflow_event_view event;
    cflow_actor_send_status status;
    const size_t index = s->stage_index;

    if (!s->stage_live) return SALTS_OK;
    slot = &s->slots[index];
    if (slot->phase != ROUTE_SLOT_STAGED) abort();
    delivery = (cflow_cnet_domain_route_delivery){
        .incarnation = s->incarnation,
        .generation = slot->generation,
        .slot = (uint32_t)index,
        .source_owner = s->source_owner,
        .connection = s->connection,
        .size = slot->size,
        .kind = slot->kind
    };
    event = (cflow_event_view){
        .id = s->event_id,
        .payload_type = &cflow_cnet_domain_route_delivery_type,
        .payload = &delivery
    };
    status = cflow_actor_ref_try_send(&s->actor, &event);
    if (status == CFLOW_ACTOR_SEND_ACCEPTED) {
        slot->phase = ROUTE_SLOT_DELIVERED;
        s->stage_live = false;
        ++s->awaiting_ack;
        ++s->actor_accepted;
        return SALTS_OK;
    }
    if (status == CFLOW_ACTOR_SEND_FULL) {
        ++s->actor_full;
        return SALTS_ENOBUFS;
    }
    return route_set_fatal_locked(s,
        status == CFLOW_ACTOR_SEND_TYPE_MISMATCH ||
        status == CFLOW_ACTOR_SEND_INVALID_ARGUMENT
            ? SALTS_EPROTO : SALTS_ESHUTDOWN);
}

int cflow_cnet_domain_route_init(
    cflow_cnet_domain_route *route,
    const cflow_cnet_domain_route_config *config) {
    route_impl *s;
    uint64_t incarnation;
    if (!route || route->impl || !config || !config->actor ||
        config->event_id == 0u || config->source_owner == 0u ||
        config->connection.slot == 0u || config->connection.generation == 0u ||
        config->slot_capacity == 0u || config->max_receive_bytes == 0u)
        return SALTS_EINVAL;
    if (config->slot_capacity > UINT32_MAX ||
        config->slot_capacity > SIZE_MAX / sizeof(route_slot) ||
        config->slot_capacity > SIZE_MAX / config->max_receive_bytes)
        return SALTS_ERANGE;
    if (!route_new_incarnation(&incarnation)) return SALTS_ERANGE;

    s = (route_impl *)calloc(1u, sizeof(*s));
    if (!s) return SALTS_ENOMEM;
    s->slots = (route_slot *)calloc(config->slot_capacity, sizeof(*s->slots));
    s->payload = (unsigned char *)calloc(
        config->slot_capacity, config->max_receive_bytes);
    cmeta_mutex_init(&s->mutex);
    if (!s->slots || !s->payload || !s->mutex) {
        cmeta_mutex_destroy(&s->mutex);
        free(s->payload);
        free(s->slots);
        free(s);
        return SALTS_ENOMEM;
    }
    if (!cflow_actor_ref_retain(config->actor, &s->actor)) {
        cmeta_mutex_destroy(&s->mutex);
        free(s->payload);
        free(s->slots);
        free(s);
        return SALTS_ESHUTDOWN;
    }
    s->target_thread = cmeta_thread_current_token();
    s->source_owner = config->source_owner;
    s->connection = config->connection;
    s->event_id = config->event_id;
    s->incarnation = incarnation;
    s->slot_capacity = config->slot_capacity;
    s->max_receive_bytes = config->max_receive_bytes;
    route->impl = s;
    return SALTS_OK;
}

int cflow_cnet_domain_route_bind_source(cflow_cnet_domain_route *route) {
    route_impl *s = route_get(route);
    if (!s) return SALTS_EINVAL;
    if (route_on_target(s)) return SALTS_EPERM;
    cmeta_mutex_lock(&s->mutex);
    if (s->source_bound || s->sealed) {
        cmeta_mutex_unlock(&s->mutex);
        return SALTS_EALREADY;
    }
    s->source_thread = cmeta_thread_current_token();
    s->source_bound = true;
    cmeta_mutex_unlock(&s->mutex);
    return SALTS_OK;
}

int cflow_cnet_domain_route_reserve(
    cflow_cnet_domain_route *route,
    cflow_cnet_domain_route_credit *out) {
    route_impl *s = route_get(route);
    int result = SALTS_ENOBUFS;
    if (out) *out = (cflow_cnet_domain_route_credit){0};
    if (!s || !out) return SALTS_EINVAL;
    cmeta_mutex_lock(&s->mutex);
    if (!route_on_source(s)) result = SALTS_EPERM;
    else if (s->sealed || s->source_terminal || s->fatal_status != SALTS_OK)
        result = SALTS_ESHUTDOWN;
    else if (s->credit_live || s->stage_live)
        result = SALTS_EBUSY;
    else if (s->active_slots == s->slot_capacity)
        result = SALTS_ENOBUFS;
    else {
        for (size_t i = 0u; i < s->slot_capacity; ++i) {
            route_slot *slot = &s->slots[i];
            if (slot->phase != ROUTE_SLOT_FREE ||
                slot->generation == UINT64_MAX)
                continue;
            ++slot->generation;
            slot->phase = ROUTE_SLOT_CREDIT;
            s->credit_index = (uint32_t)i;
            s->credit_live = true;
            ++s->active_slots;
            ++s->reserved_credits;
            *out = (cflow_cnet_domain_route_credit){
                s->incarnation, slot->generation, (uint32_t)i,
                s->source_owner, s->connection
            };
            result = SALTS_OK;
            break;
        }
        if (result == SALTS_ENOBUFS) result = SALTS_ERANGE;
    }
    cmeta_mutex_unlock(&s->mutex);
    return result;
}

int cflow_cnet_domain_route_cancel_credit(
    cflow_cnet_domain_route *route,
    cflow_cnet_domain_route_credit credit) {
    route_impl *s = route_get(route);
    int result = SALTS_OK;
    if (!s) return SALTS_EINVAL;
    cmeta_mutex_lock(&s->mutex);
    if (!route_on_source(s)) result = SALTS_EPERM;
    else if (!route_credit_valid_locked(s, credit)) result = SALTS_ENOENT;
    else {
        s->credit_live = false;
        route_return_slot_locked(s, credit.slot);
        ++s->rollback_credits;
    }
    cmeta_mutex_unlock(&s->mutex);
    return result;
}

int cflow_cnet_domain_route_receive(
    cflow_cnet_domain_route *route,
    cflow_cnet_domain_route_credit credit,
    const cnet_receive_view *view) {
    route_impl *s = route_get(route);
    route_slot *slot;
    int result;
    if (!s || !view) return SALTS_EINVAL;
    cmeta_mutex_lock(&s->mutex);
    if (!route_on_source(s)) {
        result = SALTS_EPERM;
    } else if (!route_credit_valid_locked(s, credit)) {
        result = SALTS_ENOENT;
    } else {
        slot = &s->slots[credit.slot];
        s->credit_live = false;
        slot->phase = ROUTE_SLOT_STAGED;
        slot->kind = view->kind;
        s->stage_index = credit.slot;
        s->stage_live = true;
        ++s->received_values;

        if (view->kind != CNET_MESSAGE_BYTES ||
            (view->size != 0u && !view->data)) {
            result = route_set_fatal_locked(s, SALTS_EPROTO);
        } else if (view->size > s->max_receive_bytes) {
            result = route_set_fatal_locked(s, SALTS_EMSGSIZE);
        } else {
            slot->size = view->size;
            if (view->size != 0u)
                memcpy(route_payload(s, credit.slot),
                       view->data, view->size);
            s->retained_bytes += view->size;
            if (s->retained_bytes > s->peak_retained_bytes)
                s->peak_retained_bytes = s->retained_bytes;
            result = route_try_actor_locked(s);
        }
    }
    cmeta_mutex_unlock(&s->mutex);
    return result;
}

int cflow_cnet_domain_route_retry_staged(
    cflow_cnet_domain_route *route, size_t *out_work) {
    route_impl *s = route_get(route);
    int result;
    if (out_work) *out_work = 0u;
    if (!s || !out_work) return SALTS_EINVAL;
    if (!route_on_target(s)) return SALTS_EPERM;
    cmeta_mutex_lock(&s->mutex);
    if (s->fatal_status != SALTS_OK) result = s->fatal_status;
    else if (!s->stage_live) result = SALTS_OK;
    else {
        result = route_try_actor_locked(s);
        if (result == SALTS_OK) *out_work = 1u;
    }
    cmeta_mutex_unlock(&s->mutex);
    return result;
}

static route_slot *route_lookup_delivery_locked(
    route_impl *s, const cflow_cnet_domain_route_delivery *delivery) {
    route_slot *slot;
    if (!delivery || delivery->incarnation != s->incarnation ||
        delivery->source_owner != s->source_owner ||
        delivery->slot >= s->slot_capacity ||
        !route_same_connection(delivery->connection, s->connection))
        return NULL;
    slot = &s->slots[delivery->slot];
    if (slot->phase != ROUTE_SLOT_DELIVERED ||
        slot->generation != delivery->generation ||
        slot->size != delivery->size || slot->kind != delivery->kind)
        return NULL;
    return slot;
}

int cflow_cnet_domain_route_borrow(
    cflow_cnet_domain_route *route,
    const cflow_cnet_domain_route_delivery *delivery,
    cnet_receive_view *out) {
    route_impl *s = route_get(route);
    route_slot *slot;
    int status = SALTS_OK;
    if (out) *out = (cnet_receive_view){0};
    if (!s || !delivery || !out) return SALTS_EINVAL;
    if (!route_on_target(s)) return SALTS_EPERM;
    cmeta_mutex_lock(&s->mutex);
    slot = route_lookup_delivery_locked(s, delivery);
    if (!slot) status = SALTS_ENOENT;
    else {
        *out = (cnet_receive_view){
            route_payload(s, delivery->slot), slot->size, slot->kind
        };
    }
    cmeta_mutex_unlock(&s->mutex);
    return status;
}

int cflow_cnet_domain_route_acknowledge(
    cflow_cnet_domain_route *route,
    const cflow_cnet_domain_route_delivery *delivery) {
    route_impl *s = route_get(route);
    int result = SALTS_OK;
    if (!s || !delivery) return SALTS_EINVAL;
    if (!route_on_target(s)) return SALTS_EPERM;
    cmeta_mutex_lock(&s->mutex);
    if (!route_lookup_delivery_locked(s, delivery)) result = SALTS_ENOENT;
    else {
        route_return_slot_locked(s, delivery->slot);
        ++s->acknowledged;
    }
    cmeta_mutex_unlock(&s->mutex);
    return result;
}

int cflow_cnet_domain_route_seal(cflow_cnet_domain_route *route) {
    route_impl *s = route_get(route);
    if (!s) return SALTS_EINVAL;
    cmeta_mutex_lock(&s->mutex);
    if (!route_on_source(s)) {
        cmeta_mutex_unlock(&s->mutex);
        return SALTS_EPERM;
    }
    s->sealed = true;
    cmeta_mutex_unlock(&s->mutex);
    return SALTS_OK;
}

int cflow_cnet_domain_route_source_terminal(cflow_cnet_domain_route *route) {
    route_impl *s = route_get(route);
    if (!s) return SALTS_EINVAL;
    cmeta_mutex_lock(&s->mutex);
    if (!route_on_source(s)) {
        cmeta_mutex_unlock(&s->mutex);
        return SALTS_EPERM;
    }
    s->sealed = true;
    s->source_terminal = true;
    if (s->credit_live) {
        s->credit_live = false;
        route_return_slot_locked(s, s->credit_index);
    }
    cmeta_mutex_unlock(&s->mutex);
    return SALTS_OK;
}

int cflow_cnet_domain_route_abort_after_quiescence(
    cflow_cnet_domain_route *route) {
    route_impl *s = route_get(route);
    if (!s) return SALTS_EINVAL;
    if (!route_on_target(s)) return SALTS_EPERM;
    cmeta_mutex_lock(&s->mutex);
    if (!s->sealed || s->credit_live) {
        cmeta_mutex_unlock(&s->mutex);
        return SALTS_EBUSY;
    }
    for (size_t i = 0u; i < s->slot_capacity; ++i) {
        if (s->slots[i].phase != ROUTE_SLOT_FREE) {
            route_return_slot_locked(s, i);
            ++s->abandoned;
        }
    }
    s->stage_live = false;
    cmeta_mutex_unlock(&s->mutex);
    return SALTS_OK;
}

int cflow_cnet_domain_route_get_stats(
    cflow_cnet_domain_route *route,
    cflow_cnet_domain_route_stats *out) {
    route_impl *s = route_get(route);
    if (!s || !out) return SALTS_EINVAL;
    cmeta_mutex_lock(&s->mutex);
    *out = (cflow_cnet_domain_route_stats){
        .slot_capacity = s->slot_capacity,
        .active_slots = s->active_slots,
        .awaiting_ack = s->awaiting_ack,
        .staged = s->stage_live ? 1u : 0u,
        .retained_bytes = s->retained_bytes,
        .peak_retained_bytes = s->peak_retained_bytes,
        .reserved_credits = s->reserved_credits,
        .rollback_credits = s->rollback_credits,
        .received_values = s->received_values,
        .actor_accepted = s->actor_accepted,
        .actor_full = s->actor_full,
        .acknowledged = s->acknowledged,
        .abandoned = s->abandoned,
        .fatal_status = s->fatal_status,
        .source_bound = s->source_bound,
        .receive_credit_live = s->credit_live,
        .sealed = s->sealed,
        .source_terminal = s->source_terminal
    };
    cmeta_mutex_unlock(&s->mutex);
    return SALTS_OK;
}

int cflow_cnet_domain_route_destroy(cflow_cnet_domain_route *route) {
    route_impl *s = route_get(route);
    if (!s) return SALTS_EINVAL;
    if (!route_on_target(s)) return SALTS_EPERM;
    cmeta_mutex_lock(&s->mutex);
    if (!s->sealed || s->active_slots != 0u ||
        s->credit_live || s->stage_live) {
        cmeta_mutex_unlock(&s->mutex);
        return SALTS_EBUSY;
    }
    cmeta_mutex_unlock(&s->mutex);
    /* Producers (including post/wake tails) and Actor callbacks have quiesced
     * under the host's explicit lifecycle contract. It is now safe to free. */
    cflow_actor_ref_release(&s->actor);
    cmeta_mutex_destroy(&s->mutex);
    free(s->payload);
    free(s->slots);
    free(s);
    route->impl = NULL;
    return SALTS_OK;
}
