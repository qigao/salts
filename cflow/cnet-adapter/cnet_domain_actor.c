#include <cflow/cnet_domain_actor.h>

#include <salts/error_codes.h>
#include <salts/thread.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const cmeta_type_traits domain_delivery_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY
};

const cmeta_type_desc cflow_cnet_domain_delivery_type = {
    .name = "cflow_cnet_domain_delivery",
    .size = sizeof(cflow_cnet_domain_delivery),
    .align = _Alignof(cflow_cnet_domain_delivery),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &domain_delivery_traits,
    .identity = NULL
};

typedef enum domain_phase {
    DOMAIN_FREE = 0,
    DOMAIN_RECEIVE_CREDIT,
    DOMAIN_STAGED,
    DOMAIN_DELIVERED
} domain_phase;

typedef struct domain_slot {
    domain_phase phase;
    uint64_t generation;
    size_t size;
    cnet_message_kind kind;
} domain_slot;

typedef struct domain_impl {
    cflow_actor_ref actor; /* One independent retained producer ref. */
    const void *owner_thread;
    cnet_connection connection;
    cflow_event_id event_id;
    uint64_t incarnation;
    domain_slot *slots;
    unsigned char *storage;
    size_t slot_capacity;
    size_t max_receive_bytes;
    size_t active_slots;
    size_t awaiting_ack;
    size_t retained_bytes;
    size_t peak_retained_bytes;
    uint32_t pending_credit;
    uint32_t pending_stage;
    uint64_t reserved_credits;
    uint64_t rolled_back_credits;
    uint64_t received_views;
    uint64_t mailbox_accepted;
    uint64_t mailbox_full;
    uint64_t acknowledged;
    uint64_t abandoned;
    int fatal_status;
    bool credit_live;
    bool stage_live;
    bool sealed;
    bool terminal;
} domain_impl;

static _Atomic uint64_t next_incarnation = 1u;

static bool domain_alloc_incarnation(uint64_t *out) {
    uint64_t current = atomic_load_explicit(&next_incarnation,
                                             memory_order_relaxed);
    while (current != UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(
                &next_incarnation, &current, current + 1u,
                memory_order_acq_rel, memory_order_relaxed)) {
            *out = current;
            return true;
        }
    }
    return false;
}

static domain_impl *domain_get(cflow_cnet_domain_bridge *bridge) {
    return bridge ? (domain_impl *)bridge->impl : NULL;
}

static bool domain_owner(const domain_impl *s) {
    return s && s->owner_thread == cmeta_thread_current_token();
}

static bool domain_connection_eq(cnet_connection a, cnet_connection b) {
    return a.slot == b.slot && a.generation == b.generation;
}

static unsigned char *domain_bytes(domain_impl *s, size_t index) {
    return s->storage + index * s->max_receive_bytes;
}

static void domain_release(domain_impl *s, size_t index) {
    domain_slot *slot = &s->slots[index];
    if (slot->phase == DOMAIN_STAGED || slot->phase == DOMAIN_DELIVERED) {
        if (s->retained_bytes < slot->size) abort();
        s->retained_bytes -= slot->size;
        if (slot->size != 0u) memset(domain_bytes(s, index), 0, slot->size);
    }
    if (slot->phase == DOMAIN_DELIVERED) {
        if (s->awaiting_ack == 0u) abort();
        --s->awaiting_ack;
    }
    if (slot->phase == DOMAIN_FREE || s->active_slots == 0u) abort();
    --s->active_slots;
    slot->phase = DOMAIN_FREE;
    slot->size = 0u;
    slot->kind = (cnet_message_kind)0;
}

static bool domain_credit_match(
    const domain_impl *s, cflow_cnet_domain_credit credit) {
    return s && s->credit_live && s->incarnation == credit.incarnation &&
           credit.slot == s->pending_credit &&
           credit.slot < s->slot_capacity &&
           s->slots[credit.slot].generation == credit.generation &&
           s->slots[credit.slot].phase == DOMAIN_RECEIVE_CREDIT;
}

static int domain_fatal(domain_impl *s, int reason) {
    if (s->fatal_status == SALTS_OK) s->fatal_status = reason;
    s->sealed = true; /* The host must handle connection close explicitly. */
    return s->fatal_status;
}

static int domain_try_actor(domain_impl *s) {
    domain_slot *slot;
    cflow_cnet_domain_delivery payload;
    cflow_event_view event;
    cflow_actor_send_status status;
    const size_t index = s->pending_stage;

    if (!s->stage_live) return SALTS_OK;
    slot = &s->slots[index];
    if (slot->phase != DOMAIN_STAGED) abort();
    payload = (cflow_cnet_domain_delivery){
        .incarnation = s->incarnation,
        .generation = slot->generation,
        .slot = (uint32_t)index,
        .connection = s->connection,
        .size = slot->size,
        .kind = slot->kind
    };
    event = (cflow_event_view){
        .id = s->event_id,
        .payload_type = &cflow_cnet_domain_delivery_type,
        .payload = &payload
    };
    status = cflow_actor_ref_try_send(&s->actor, &event);
    if (status == CFLOW_ACTOR_SEND_ACCEPTED) {
        slot->phase = DOMAIN_DELIVERED;
        s->stage_live = false;
        ++s->awaiting_ack;
        ++s->mailbox_accepted;
        return SALTS_OK;
    }
    if (status == CFLOW_ACTOR_SEND_FULL) {
        ++s->mailbox_full;
        return SALTS_ENOBUFS; /* Retained, never silently discarded. */
    }
    return domain_fatal(s,
        status == CFLOW_ACTOR_SEND_TYPE_MISMATCH ||
        status == CFLOW_ACTOR_SEND_INVALID_ARGUMENT
            ? SALTS_EPROTO : SALTS_ESHUTDOWN);
}

int cflow_cnet_domain_bridge_init(
    cflow_cnet_domain_bridge *bridge,
    const cflow_cnet_domain_config *config) {
    domain_impl *s;
    uint64_t incarnation = 0u;
    if (!bridge || bridge->impl || !config || !config->actor ||
        config->event_id == 0u || config->slot_capacity == 0u ||
        config->max_receive_bytes == 0u ||
        config->connection.slot == 0u || config->connection.generation == 0u)
        return SALTS_EINVAL;
    if (config->slot_capacity > UINT32_MAX ||
        config->slot_capacity > SIZE_MAX / sizeof(domain_slot) ||
        config->slot_capacity > SIZE_MAX / config->max_receive_bytes)
        return SALTS_ERANGE;
    if (!domain_alloc_incarnation(&incarnation)) return SALTS_ERANGE;
    s = (domain_impl *)calloc(1u, sizeof(*s));
    if (!s) return SALTS_ENOMEM;
    s->slots = (domain_slot *)calloc(config->slot_capacity, sizeof(*s->slots));
    s->storage = (unsigned char *)calloc(
        config->slot_capacity, config->max_receive_bytes);
    if (!s->slots || !s->storage) {
        free(s->storage);
        free(s->slots);
        free(s);
        return SALTS_ENOMEM;
    }
    if (!cflow_actor_ref_retain(config->actor, &s->actor)) {
        free(s->storage);
        free(s->slots);
        free(s);
        return SALTS_ESHUTDOWN; /* Invalid or already stale Actor producer. */
    }

    s->owner_thread = cmeta_thread_current_token();
    s->connection = config->connection;
    s->event_id = config->event_id;
    s->incarnation = incarnation;
    s->slot_capacity = config->slot_capacity;
    s->max_receive_bytes = config->max_receive_bytes;
    *bridge = (cflow_cnet_domain_bridge){s};
    return SALTS_OK;
}

int cflow_cnet_domain_reserve_credit(
    cflow_cnet_domain_bridge *bridge,
    cflow_cnet_domain_credit *out) {
    domain_impl *s = domain_get(bridge);
    size_t i;
    if (out) *out = (cflow_cnet_domain_credit){0};
    if (!s || !out) return SALTS_EINVAL;
    if (!domain_owner(s)) return SALTS_EPERM;
    if (s->sealed || s->terminal || s->fatal_status != SALTS_OK)
        return SALTS_ESHUTDOWN;
    if (s->credit_live || s->stage_live) return SALTS_EBUSY;
    if (s->active_slots == s->slot_capacity) return SALTS_ENOBUFS;

    for (i = 0u; i < s->slot_capacity; ++i) {
        domain_slot *slot = &s->slots[i];
        if (slot->phase != DOMAIN_FREE || slot->generation == UINT64_MAX)
            continue;
        ++slot->generation;
        slot->phase = DOMAIN_RECEIVE_CREDIT;
        slot->size = 0u;
        s->pending_credit = (uint32_t)i;
        s->credit_live = true;
        ++s->active_slots;
        ++s->reserved_credits;
        *out = (cflow_cnet_domain_credit){
            .incarnation = s->incarnation,
            .generation = slot->generation,
            .slot = (uint32_t)i
        };
        return SALTS_OK;
    }
    return SALTS_ERANGE; /* Generation exhausted, never wrap. */
}

int cflow_cnet_domain_cancel_credit(
    cflow_cnet_domain_bridge *bridge,
    cflow_cnet_domain_credit credit) {
    domain_impl *s = domain_get(bridge);
    if (!s) return SALTS_EINVAL;
    if (!domain_owner(s)) return SALTS_EPERM;
    if (!domain_credit_match(s, credit)) return SALTS_ENOENT;
    s->credit_live = false;
    domain_release(s, credit.slot);
    ++s->rolled_back_credits;
    return SALTS_OK;
}

int cflow_cnet_domain_receive(
    cflow_cnet_domain_bridge *bridge,
    cnet_connection connection,
    const cnet_receive_view *view) {
    domain_impl *s = domain_get(bridge);
    domain_slot *slot;
    uint32_t index;
    if (!s || !view) return SALTS_EINVAL;
    if (!domain_owner(s)) return SALTS_EPERM;
    if (!domain_connection_eq(s->connection, connection) || !s->credit_live)
        return SALTS_ENOENT;
    index = s->pending_credit;
    slot = &s->slots[index];
    if (slot->phase != DOMAIN_RECEIVE_CREDIT || s->stage_live)
        abort();
    s->credit_live = false;
    slot->phase = DOMAIN_STAGED;
    slot->kind = view->kind;
    s->pending_stage = index;
    s->stage_live = true;
    ++s->received_views;

    if (view->kind != CNET_MESSAGE_BYTES ||
        (view->size != 0u && !view->data))
        return domain_fatal(s, SALTS_EPROTO);
    if (view->size > s->max_receive_bytes)
        return domain_fatal(s, SALTS_EMSGSIZE);
    slot->size = view->size;
    if (view->size != 0u)
        memcpy(domain_bytes(s, index), view->data, view->size);
    s->retained_bytes += view->size;
    if (s->retained_bytes > s->peak_retained_bytes)
        s->peak_retained_bytes = s->retained_bytes;
    return domain_try_actor(s);
}

int cflow_cnet_domain_retry_actor(
    cflow_cnet_domain_bridge *bridge, size_t *out_work) {
    domain_impl *s = domain_get(bridge);
    int status;
    if (out_work) *out_work = 0u;
    if (!s || !out_work) return SALTS_EINVAL;
    if (!domain_owner(s)) return SALTS_EPERM;
    if (s->fatal_status != SALTS_OK) return s->fatal_status;
    if (!s->stage_live) return SALTS_OK;
    status = domain_try_actor(s);
    if (status == SALTS_OK) *out_work = 1u;
    return status;
}

static domain_slot *domain_delivery_lookup(
    domain_impl *s, const cflow_cnet_domain_delivery *value) {
    domain_slot *slot;
    if (!s || !value || value->incarnation != s->incarnation ||
        value->slot >= s->slot_capacity ||
        !domain_connection_eq(s->connection, value->connection))
        return NULL;
    slot = &s->slots[value->slot];
    return slot->phase == DOMAIN_DELIVERED &&
                   slot->generation == value->generation &&
                   slot->size == value->size && slot->kind == value->kind
               ? slot : NULL;
}

int cflow_cnet_domain_borrow(
    cflow_cnet_domain_bridge *bridge,
    const cflow_cnet_domain_delivery *delivery,
    cnet_receive_view *out) {
    domain_impl *s = domain_get(bridge);
    domain_slot *slot;
    if (out) *out = (cnet_receive_view){0};
    if (!s || !out || !delivery) return SALTS_EINVAL;
    if (!domain_owner(s)) return SALTS_EPERM;
    slot = domain_delivery_lookup(s, delivery);
    if (!slot) return SALTS_ENOENT;
    *out = (cnet_receive_view){
        .data = domain_bytes(s, delivery->slot),
        .size = slot->size,
        .kind = slot->kind
    };
    return SALTS_OK;
}

int cflow_cnet_domain_acknowledge(
    cflow_cnet_domain_bridge *bridge,
    const cflow_cnet_domain_delivery *delivery) {
    domain_impl *s = domain_get(bridge);
    domain_slot *slot;
    if (!s || !delivery) return SALTS_EINVAL;
    if (!domain_owner(s)) return SALTS_EPERM;
    slot = domain_delivery_lookup(s, delivery);
    if (!slot) return SALTS_ENOENT;
    domain_release(s, delivery->slot);
    ++s->acknowledged;
    return SALTS_OK;
}

int cflow_cnet_domain_seal(cflow_cnet_domain_bridge *bridge) {
    domain_impl *s = domain_get(bridge);
    if (!s) return SALTS_EINVAL;
    if (!domain_owner(s)) return SALTS_EPERM;
    s->sealed = true;
    return SALTS_OK;
}

int cflow_cnet_domain_transport_terminal(cflow_cnet_domain_bridge *bridge) {
    domain_impl *s = domain_get(bridge);
    if (!s) return SALTS_EINVAL;
    if (!domain_owner(s)) return SALTS_EPERM;
    s->terminal = true;
    s->sealed = true;
    if (s->credit_live) {
        s->credit_live = false;
        domain_release(s, s->pending_credit);
    }
    return SALTS_OK;
}

int cflow_cnet_domain_abort_after_quiescence(
    cflow_cnet_domain_bridge *bridge) {
    domain_impl *s = domain_get(bridge);
    if (!s) return SALTS_EINVAL;
    if (!domain_owner(s)) return SALTS_EPERM;
    if (!s->sealed || s->credit_live) return SALTS_EBUSY;
    for (size_t i = 0u; i < s->slot_capacity; ++i) {
        if (s->slots[i].phase != DOMAIN_FREE) {
            domain_release(s, i);
            ++s->abandoned;
        }
    }
    s->stage_live = false;
    return SALTS_OK;
}

int cflow_cnet_domain_get_stats(
    cflow_cnet_domain_bridge *bridge,
    cflow_cnet_domain_stats *out) {
    domain_impl *s = domain_get(bridge);
    if (!s || !out) return SALTS_EINVAL;
    if (!domain_owner(s)) return SALTS_EPERM;
    *out = (cflow_cnet_domain_stats){
        .slot_capacity = s->slot_capacity,
        .max_receive_bytes = s->max_receive_bytes,
        .active_slots = s->active_slots,
        .awaiting_ack = s->awaiting_ack,
        .pending_actor_admission = s->stage_live ? 1u : 0u,
        .retained_bytes = s->retained_bytes,
        .peak_retained_bytes = s->peak_retained_bytes,
        .reserved_credits = s->reserved_credits,
        .rolled_back_credits = s->rolled_back_credits,
        .received_views = s->received_views,
        .mailbox_accepted = s->mailbox_accepted,
        .mailbox_full = s->mailbox_full,
        .acknowledged = s->acknowledged,
        .abandoned = s->abandoned,
        .fatal_status = s->fatal_status,
        .credit_outstanding = s->credit_live,
        .sealed = s->sealed,
        .transport_terminal = s->terminal
    };
    return SALTS_OK;
}

int cflow_cnet_domain_bridge_destroy(cflow_cnet_domain_bridge *bridge) {
    domain_impl *s = domain_get(bridge);
    if (!s) return SALTS_EINVAL;
    if (!domain_owner(s)) return SALTS_EPERM;
    if (!s->sealed || s->active_slots != 0u ||
        s->credit_live || s->stage_live)
        return SALTS_EBUSY;
    cflow_actor_ref_release(&s->actor);
    free(s->storage);
    free(s->slots);
    free(s);
    bridge->impl = NULL;
    return SALTS_OK;
}
