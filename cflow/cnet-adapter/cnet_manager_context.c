#include <cflow/cnet_manager_context.h>

#include <salts/error_codes.h>
#include <salts/thread.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct manager_context_impl {
    cnet_manager *manager; /* Borrowed: source owner must outlive guard. */
    cnet_managed_connection managed;
    cflow_cnet_domain_route **routes; /* Fixed copied pointer array. */
    const void *source_thread;
    cflow_cnet_manager_context_wake_fn wake;
    void *wake_user;
    size_t route_count;
    uint64_t release_checks; /* Source owner only. */
    atomic_uint_fast64_t notifications;
    atomic_uint_fast64_t coalesced_wakes;
    atomic_bool notification_pending;
    bool released; /* Written/read only by source Owner. */
} manager_context_impl;

static manager_context_impl *context_get(cflow_cnet_manager_context *context) {
    return context ? (manager_context_impl *)context->impl : NULL;
}

static bool on_source(const manager_context_impl *s) {
    return s && s->source_thread == cmeta_thread_current_token();
}

static bool same_connection(cnet_connection a, cnet_connection b) {
    return a.slot == b.slot && a.generation == b.generation;
}

int cflow_cnet_manager_context_init(
    cflow_cnet_manager_context *context,
    const cflow_cnet_manager_context_config *config) {
    cnet_manager_entry entry = {0};
    manager_context_impl *s;
    uint32_t source_owner = 0u;
    int status;

    if (!context || context->impl || !config || !config->manager ||
        !config->routes || config->route_count == 0u ||
        config->route_count > SIZE_MAX / sizeof(cflow_cnet_domain_route *))
        return SALTS_EINVAL;

    /* This owner-local lookup authenticates manager identity AND current
     * manager incarnation; there is no global manager registry. */
    status = cnet_manager_lookup(
        config->manager, config->managed, &entry);
    if (status != SALTS_OK) return status;
    if (entry.state != CNET_MANAGER_BOUND || !entry.context_held)
        return SALTS_EBUSY;
    if (entry.connection.slot == 0u ||
        entry.connection.generation == 0u)
        return SALTS_EINVAL;

    /* All routes must be bound to THIS calling CNet source owner, reference
     * the SAME exact connection generation, and have the same declared source
     * identity. Reject duplicate route entries (would double-count leases). */
    for (size_t i = 0u; i < config->route_count; ++i) {
        cnet_connection connection = {0};
        uint32_t owner_id = 0u;
        if (!config->routes[i]) return SALTS_EINVAL;
        for (size_t j = 0u; j < i; ++j)
            if (config->routes[i] == config->routes[j])
                return SALTS_EINVAL;
        status = cflow_cnet_domain_route_get_source_binding(
            config->routes[i], &connection, &owner_id);
        if (status != SALTS_OK) return status;
        if (!same_connection(connection, entry.connection) ||
            owner_id == 0u || (source_owner && owner_id != source_owner))
            return SALTS_EINVAL;
        source_owner = owner_id;
    }

    s = (manager_context_impl *)calloc(1u, sizeof(*s));
    if (!s) return SALTS_ENOMEM;
    s->routes = (cflow_cnet_domain_route **)calloc(
        config->route_count, sizeof(*s->routes));
    if (!s->routes) {
        free(s);
        return SALTS_ENOMEM;
    }
    for (size_t i = 0u; i < config->route_count; ++i)
        s->routes[i] = config->routes[i];
    s->manager = config->manager;
    s->managed = config->managed;
    s->source_thread = cmeta_thread_current_token();
    s->route_count = config->route_count;
    s->wake = config->wake;
    s->wake_user = config->wake_user;
    atomic_init(&s->notifications, 0u);
    atomic_init(&s->coalesced_wakes, 0u);
    atomic_init(&s->notification_pending, false);
    context->impl = s;
    return SALTS_OK;
}

int cflow_cnet_manager_context_notify_settled(
    cflow_cnet_manager_context *context) {
    manager_context_impl *s = context_get(context);
    bool already_pending;
    if (!s) return SALTS_EINVAL;
    /* Notification is only a coalesced hint. The SOURCE Owner alone
     * checks route/accounting truth and releases the manager context. */
    atomic_fetch_add_explicit(
        &s->notifications, 1u, memory_order_relaxed);
    already_pending = atomic_exchange_explicit(
        &s->notification_pending, true, memory_order_acq_rel);
    if (!already_pending) {
        atomic_fetch_add_explicit(
            &s->coalesced_wakes, 1u, memory_order_relaxed);
        if (s->wake) s->wake(s->wake_user);
    }
    return SALTS_OK;
}

int cflow_cnet_manager_context_poll_release(
    cflow_cnet_manager_context *context, bool *out_released) {
    manager_context_impl *s = context_get(context);
    cnet_manager_entry entry = {0};
    int status;

    if (out_released) *out_released = false;
    if (!s || !out_released) return SALTS_EINVAL;
    if (!on_source(s)) return SALTS_EPERM;
    if (s->released) return SALTS_EALREADY;
    ++s->release_checks;

    /* Clear BEFORE inspecting route state so an ACK arriving during the scan
     * can publish a fresh hint (no lost-wake read/clear race). The host also
     * must poll on CNet terminal and must not rely solely on this hint. */
    (void)atomic_exchange_explicit(
        &s->notification_pending, false, memory_order_acq_rel);

    status = cnet_manager_lookup(s->manager, s->managed, &entry);
    if (status != SALTS_OK) return status;
    if (!entry.context_held) return SALTS_EPROTO;
    if (entry.state != CNET_MANAGER_RETIRED)
        return SALTS_EBUSY;

    for (size_t i = 0u; i < s->route_count; ++i) {
        cflow_cnet_domain_route_stats stats = {0};
        status = cflow_cnet_domain_route_get_stats(s->routes[i], &stats);
        if (status != SALTS_OK) return status;
        if (!stats.source_bound || !stats.source_terminal ||
            !stats.sealed || stats.receive_credit_live ||
            stats.staged != 0u || stats.awaiting_ack != 0u ||
            stats.active_slots != 0u)
            return SALTS_EBUSY;
    }

    /* This is the ONLY manager context release call. It never executes on
     * a target Actor callback thread and cannot fabricate a CNet terminal. */
    status = cnet_manager_release_context(s->manager, s->managed);
    if (status != SALTS_OK) return status;
    s->released = true;
    *out_released = true;
    return SALTS_OK;
}

int cflow_cnet_manager_context_get_stats(
    cflow_cnet_manager_context *context,
    cflow_cnet_manager_context_stats *out) {
    manager_context_impl *s = context_get(context);
    if (!s || !out) return SALTS_EINVAL;
    if (!on_source(s)) return SALTS_EPERM;
    *out = (cflow_cnet_manager_context_stats){
        .route_count = s->route_count,
        .notifications = (uint64_t)atomic_load_explicit(
            &s->notifications, memory_order_relaxed),
        .coalesced_wakes = (uint64_t)atomic_load_explicit(
            &s->coalesced_wakes, memory_order_relaxed),
        .release_checks = s->release_checks,
        .notification_pending = atomic_load_explicit(
            &s->notification_pending, memory_order_acquire),
        .released = s->released
    };
    return SALTS_OK;
}

int cflow_cnet_manager_context_destroy(
    cflow_cnet_manager_context *context) {
    manager_context_impl *s = context_get(context);
    if (!s) return SALTS_EINVAL;
    if (!on_source(s)) return SALTS_EPERM;
    if (!s->released) return SALTS_EBUSY;
    /* The host must have stopped/joined all Actor ACK/wake publishers; the
     * route and manager themselves are separately owned and not destroyed. */
    free(s->routes);
    free(s);
    context->impl = NULL;
    return SALTS_OK;
}
