#include <cflow/executor.h>
#include <cflow/scxml.h>

#include "tinytest.h"

#include <stdio.h>
#include <string.h>
#include <turbo/thread.h>

#define HOST_ENDPOINT_CAPACITY 4u
#define HOST_MESSAGE_CAPACITY 4u
#define HOST_LOCATION_CAPACITY 64u
#define HOST_EVENT_CAPACITY 64u
#define HOST_SEND_ID_CAPACITY 64u
#define HOST_TARGET_CAPACITY 64u

static const char HOST_SCXML_PROCESSOR[] =
    "http://www.w3.org/TR/scxml/#SCXMLEventProcessor";
static const char HOST_ORIGIN_TYPE[] = "scxml";

typedef enum host_message_state {
    HOST_MESSAGE_FREE = 0,
    HOST_MESSAGE_RESERVED,
    HOST_MESSAGE_READY,
    HOST_MESSAGE_INFLIGHT
} host_message_state;

typedef struct host_router host_router;
typedef struct host_adapter_context host_adapter_context;

typedef struct host_endpoint {
    bool in_use;
    bool accessible;
    cflow_scxml_session *session;
    const cflow_scxml_program *program;
    host_adapter_context *adapter;
    size_t parent;
    size_t invoke_target;
    char invoke_alias[HOST_TARGET_CAPACITY];
    char location[HOST_LOCATION_CAPACITY];
} host_endpoint;

typedef struct host_message {
    host_router *router;
    host_adapter_context *source_adapter;
    host_message_state state;
    size_t source;
    size_t target;
    uint64_t sequence;
    char event[HOST_EVENT_CAPACITY];
    char target_text[HOST_TARGET_CAPACITY];
    char send_id[HOST_SEND_ID_CAPACITY];
} host_message;

typedef struct host_delivery {
    size_t count;
    char event[HOST_EVENT_CAPACITY];
    char target[HOST_TARGET_CAPACITY];
    char send_id[HOST_SEND_ID_CAPACITY];
    char origin[HOST_LOCATION_CAPACITY];
    char origin_type[sizeof(HOST_ORIGIN_TYPE)];
} host_delivery;

struct host_router {
    turbo_mutex_t lock;
    size_t message_capacity;
    uint64_t next_sequence;
    host_endpoint endpoints[HOST_ENDPOINT_CAPACITY];
    host_message messages[HOST_MESSAGE_CAPACITY];
    host_delivery last_delivery;
};

struct host_adapter_context {
    host_router *router;
    size_t endpoint;
    size_t outstanding;
    bool closed;
};

typedef enum host_pump_status {
    HOST_PUMP_EMPTY = 0,
    HOST_PUMP_DELIVERED,
    HOST_PUMP_WOULD_BLOCK,
    HOST_PUMP_DROPPED
} host_pump_status;

static bool host_copy(char *destination, size_t capacity,
                      const char *source, size_t source_size) {
    if (destination == NULL || capacity == 0u || source_size >= capacity ||
        (source == NULL && source_size != 0u))
        return false;
    if (source_size != 0u) memcpy(destination, source, source_size);
    destination[source_size] = '\0';
    return true;
}

static bool host_text_equal(const char *left, size_t left_size,
                            const char *right) {
    const size_t right_size = strlen(right);
    return left_size == right_size &&
        (left_size == 0u || memcmp(left, right, left_size) == 0);
}

static bool host_router_init(host_router *router, size_t message_capacity) {
    size_t index;
    if (router == NULL || message_capacity == 0u ||
        message_capacity > HOST_MESSAGE_CAPACITY)
        return false;
    memset(router, 0, sizeof(*router));
    for (index = 0u; index < HOST_ENDPOINT_CAPACITY; ++index) {
        router->endpoints[index].parent = SIZE_MAX;
        router->endpoints[index].invoke_target = SIZE_MAX;
    }
    router->message_capacity = message_capacity;
    router->next_sequence = UINT64_C(1);
    turbo_mutex_init(&router->lock);
    return router->lock != NULL;
}

static void host_router_destroy(host_router *router) {
    if (router == NULL) return;
    if (router->lock != NULL) turbo_mutex_destroy(&router->lock);
    memset(router, 0, sizeof(*router));
}

static void host_adapter_init(host_adapter_context *adapter,
                              host_router *router) {
    if (adapter == NULL) return;
    *adapter = (host_adapter_context){
        .router = router, .endpoint = SIZE_MAX};
}

static bool host_router_register(
    host_router *router, cflow_scxml_session *session,
    const cflow_scxml_program *program, bool accessible,
    host_adapter_context *adapter, size_t *out_endpoint) {
    char location[HOST_LOCATION_CAPACITY];
    size_t required = 0u;
    size_t index;
    if (router == NULL || session == NULL || program == NULL ||
        out_endpoint == NULL ||
        cflow_scxml_session_copy_location(
            session, location, sizeof(location), &required) !=
            CFLOW_SCXML_LOCATION_OK)
        return false;
    turbo_mutex_lock(&router->lock);
    for (index = 0u; index < HOST_ENDPOINT_CAPACITY; ++index) {
        if (router->endpoints[index].in_use &&
            strcmp(router->endpoints[index].location, location) == 0) {
            turbo_mutex_unlock(&router->lock);
            return false;
        }
    }
    for (index = 0u; index < HOST_ENDPOINT_CAPACITY; ++index) {
        host_endpoint *endpoint = &router->endpoints[index];
        if (endpoint->in_use) continue;
        endpoint->in_use = true;
        endpoint->accessible = accessible;
        endpoint->session = session;
        endpoint->program = program;
        endpoint->adapter = adapter;
        endpoint->parent = SIZE_MAX;
        endpoint->invoke_target = SIZE_MAX;
        memcpy(endpoint->location, location, required);
        if (adapter != NULL) adapter->endpoint = index;
        *out_endpoint = index;
        turbo_mutex_unlock(&router->lock);
        return true;
    }
    turbo_mutex_unlock(&router->lock);
    return false;
}

static bool host_router_unregister(host_router *router, size_t endpoint) {
    size_t index;
    if (router == NULL || endpoint >= HOST_ENDPOINT_CAPACITY) return false;
    turbo_mutex_lock(&router->lock);
    if (!router->endpoints[endpoint].in_use) {
        turbo_mutex_unlock(&router->lock);
        return false;
    }
    for (index = 0u; index < router->message_capacity; ++index) {
        const host_message *message = &router->messages[index];
        if (message->state != HOST_MESSAGE_FREE &&
            (message->source == endpoint || message->target == endpoint)) {
            turbo_mutex_unlock(&router->lock);
            return false;
        }
    }
    if (router->endpoints[endpoint].adapter != NULL)
        router->endpoints[endpoint].adapter->endpoint = SIZE_MAX;
    router->endpoints[endpoint] = (host_endpoint){
        .parent = SIZE_MAX, .invoke_target = SIZE_MAX};
    turbo_mutex_unlock(&router->lock);
    return true;
}

static bool host_router_set_parent(host_router *router, size_t child,
                                   size_t parent) {
    bool valid;
    if (router == NULL || child >= HOST_ENDPOINT_CAPACITY ||
        parent >= HOST_ENDPOINT_CAPACITY)
        return false;
    turbo_mutex_lock(&router->lock);
    valid = router->endpoints[child].in_use &&
        router->endpoints[parent].in_use;
    if (valid) router->endpoints[child].parent = parent;
    turbo_mutex_unlock(&router->lock);
    return valid;
}

static bool host_router_set_invoke_alias(
    host_router *router, size_t owner, const char *alias, size_t target) {
    bool valid;
    if (router == NULL || owner >= HOST_ENDPOINT_CAPACITY ||
        target >= HOST_ENDPOINT_CAPACITY || alias == NULL)
        return false;
    turbo_mutex_lock(&router->lock);
    valid = router->endpoints[owner].in_use &&
        router->endpoints[target].in_use &&
        host_copy(router->endpoints[owner].invoke_alias,
                  sizeof(router->endpoints[owner].invoke_alias),
                  alias, strlen(alias));
    if (valid) router->endpoints[owner].invoke_target = target;
    turbo_mutex_unlock(&router->lock);
    return valid;
}

static size_t host_resolve_target_locked(
    const host_router *router, size_t source,
    const char *target, size_t target_size) {
    const host_endpoint *owner;
    size_t index;
    if (source >= HOST_ENDPOINT_CAPACITY ||
        !router->endpoints[source].in_use)
        return SIZE_MAX;
    owner = &router->endpoints[source];
    if (target_size == 0u) return source;
    if (host_text_equal(target, target_size, "#_parent"))
        return owner->parent;
    if (owner->invoke_target != SIZE_MAX &&
        host_text_equal(target, target_size, owner->invoke_alias))
        return owner->invoke_target;
    for (index = 0u; index < HOST_ENDPOINT_CAPACITY; ++index) {
        const host_endpoint *candidate = &router->endpoints[index];
        if (candidate->in_use && candidate->accessible &&
            host_text_equal(target, target_size, candidate->location))
            return index;
    }
    return SIZE_MAX;
}

static void host_release_message_locked(host_message *message) {
    host_adapter_context *adapter = message->source_adapter;
    if (adapter != NULL && adapter->outstanding != 0u)
        --adapter->outstanding;
    memset(message, 0, sizeof(*message));
}

static void host_ticket_commit(void *user) {
    host_message *message = (host_message *)user;
    host_router *router = message != NULL ? message->router : NULL;
    if (router == NULL) return;
    turbo_mutex_lock(&router->lock);
    if (message->state == HOST_MESSAGE_RESERVED) {
        message->state = HOST_MESSAGE_READY;
        message->sequence = router->next_sequence++;
    }
    turbo_mutex_unlock(&router->lock);
}

static void host_ticket_discard(void *user) {
    host_message *message = (host_message *)user;
    host_router *router = message != NULL ? message->router : NULL;
    if (router == NULL) return;
    turbo_mutex_lock(&router->lock);
    if (message->state == HOST_MESSAGE_RESERVED)
        host_release_message_locked(message);
    turbo_mutex_unlock(&router->lock);
}

static cflow_scxml_adapter_status host_prepare_send(
    void *user, const cflow_scxml_send_request_v3 *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    host_adapter_context *adapter = (host_adapter_context *)user;
    host_router *router = adapter != NULL ? adapter->router : NULL;
    host_message *message = NULL;
    bool source_relative_target;
    size_t target;
    size_t index;
    if (router == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || adapter->endpoint == SIZE_MAX)
        return CFLOW_SCXML_ADAPTER_INVALID_CONTRACT;
    *out_error = NULL;
    if (request->payload.kind != CFLOW_SCXML_PAYLOAD_NONE)
        return CFLOW_SCXML_ADAPTER_INVALID_CONTRACT;
    if (request->base.type_size != 0u &&
        !host_text_equal(request->base.type, request->base.type_size,
                         HOST_SCXML_PROCESSOR)) {
        *out_error = "unsupported Event I/O processor type";
        return CFLOW_SCXML_ADAPTER_ERROR_EXECUTION;
    }
    turbo_mutex_lock(&router->lock);
    if (adapter->closed) {
        turbo_mutex_unlock(&router->lock);
        return CFLOW_SCXML_ADAPTER_CLOSED;
    }
    source_relative_target = request->base.target_size == 0u ||
        host_text_equal(request->base.target, request->base.target_size,
                        "#_parent") ||
        (router->endpoints[adapter->endpoint].invoke_target != SIZE_MAX &&
         host_text_equal(
             request->base.target, request->base.target_size,
             router->endpoints[adapter->endpoint].invoke_alias));
    target = host_resolve_target_locked(
        router, adapter->endpoint, request->base.target,
        request->base.target_size);
    if (target == SIZE_MAX || !router->endpoints[target].in_use ||
        (!source_relative_target &&
         !router->endpoints[target].accessible)) {
        turbo_mutex_unlock(&router->lock);
        *out_error = "target session is missing or inaccessible";
        return CFLOW_SCXML_ADAPTER_ERROR_COMMUNICATION;
    }
    for (index = 0u; index < router->message_capacity; ++index) {
        if (router->messages[index].state == HOST_MESSAGE_FREE) {
            message = &router->messages[index];
            break;
        }
    }
    if (message == NULL) {
        turbo_mutex_unlock(&router->lock);
        return CFLOW_SCXML_ADAPTER_FULL;
    }
    if (!host_copy(message->event, sizeof(message->event),
                   request->base.event, request->base.event_size) ||
        !host_copy(message->target_text, sizeof(message->target_text),
                   request->base.target, request->base.target_size) ||
        !host_copy(message->send_id, sizeof(message->send_id),
                   request->base.id, request->base.id_size)) {
        memset(message, 0, sizeof(*message));
        turbo_mutex_unlock(&router->lock);
        return CFLOW_SCXML_ADAPTER_FULL;
    }
    message->router = router;
    message->source_adapter = adapter;
    message->state = HOST_MESSAGE_RESERVED;
    message->source = adapter->endpoint;
    message->target = target;
    ++adapter->outstanding;
    *out_ticket = (cflow_statechart_effect_ticket){
        host_ticket_commit, host_ticket_discard, message};
    turbo_mutex_unlock(&router->lock);
    return CFLOW_SCXML_ADAPTER_ACCEPTED;
}

static cflow_scxml_adapter_status host_prepare_cancel(
    void *user, const cflow_scxml_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    (void)user;
    (void)request;
    (void)out_ticket;
    if (out_error != NULL) *out_error = "cancel capability is not advertised";
    return CFLOW_SCXML_ADAPTER_INVALID_CONTRACT;
}

static void host_adapter_close(void *user) {
    host_adapter_context *adapter = (host_adapter_context *)user;
    if (adapter == NULL || adapter->router == NULL) return;
    turbo_mutex_lock(&adapter->router->lock);
    adapter->closed = true;
    turbo_mutex_unlock(&adapter->router->lock);
}

static bool host_adapter_is_quiescent(void *user) {
    host_adapter_context *adapter = (host_adapter_context *)user;
    bool quiescent;
    if (adapter == NULL || adapter->router == NULL) return false;
    turbo_mutex_lock(&adapter->router->lock);
    quiescent = adapter->closed && adapter->outstanding == 0u;
    turbo_mutex_unlock(&adapter->router->lock);
    return quiescent;
}

static const cflow_scxml_event_io_adapter_v3 HOST_ADAPTER = {
    .abi_version = CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V3,
    .struct_size = sizeof(cflow_scxml_event_io_adapter_v3),
    .capabilities = CFLOW_SCXML_EVENT_IO_CAP_SEND,
    .prepare_send = host_prepare_send,
    .prepare_cancel = host_prepare_cancel,
    .close = host_adapter_close,
    .is_quiescent = host_adapter_is_quiescent};

static size_t host_router_ready_count(host_router *router) {
    size_t count = 0u;
    size_t index;
    turbo_mutex_lock(&router->lock);
    for (index = 0u; index < router->message_capacity; ++index) {
        if (router->messages[index].state == HOST_MESSAGE_READY) ++count;
    }
    turbo_mutex_unlock(&router->lock);
    return count;
}

static host_pump_status host_router_pump(host_router *router) {
    host_message snapshot = {0};
    host_message *selected = NULL;
    host_endpoint target = {0};
    host_endpoint source = {0};
    cflow_event_view event = {0};
    cflow_scxml_event_metadata metadata = {0};
    cflow_mailbox_status mailbox_status = CFLOW_MAILBOX_INVALID_ARGUMENT;
    size_t index;
    if (router == NULL) return HOST_PUMP_EMPTY;
    turbo_mutex_lock(&router->lock);
    for (index = 0u; index < router->message_capacity; ++index) {
        host_message *candidate = &router->messages[index];
        if (candidate->state == HOST_MESSAGE_READY &&
            (selected == NULL || candidate->sequence < selected->sequence))
            selected = candidate;
    }
    if (selected == NULL) {
        turbo_mutex_unlock(&router->lock);
        return HOST_PUMP_EMPTY;
    }
    selected->state = HOST_MESSAGE_INFLIGHT;
    snapshot = *selected;
    source = router->endpoints[snapshot.source];
    target = router->endpoints[snapshot.target];
    turbo_mutex_unlock(&router->lock);

    metadata = (cflow_scxml_event_metadata){
        .send_id = snapshot.send_id,
        .send_id_size = strlen(snapshot.send_id),
        .origin = source.location,
        .origin_size = strlen(source.location),
        .origin_type = HOST_ORIGIN_TYPE,
        .origin_type_size = sizeof(HOST_ORIGIN_TYPE) - 1u};
    if (source.in_use && target.in_use && target.accessible &&
        cflow_scxml_program_event(
            target.program, snapshot.event, strlen(snapshot.event), &event)) {
        mailbox_status = cflow_scxml_session_try_send_v2(
            target.session, &event, &metadata);
    }

    turbo_mutex_lock(&router->lock);
    if (mailbox_status == CFLOW_MAILBOX_FULL) {
        selected->state = HOST_MESSAGE_READY;
        turbo_mutex_unlock(&router->lock);
        return HOST_PUMP_WOULD_BLOCK;
    }
    if (mailbox_status == CFLOW_MAILBOX_OK) {
        ++router->last_delivery.count;
        (void)host_copy(router->last_delivery.event,
                        sizeof(router->last_delivery.event), snapshot.event,
                        strlen(snapshot.event));
        (void)host_copy(router->last_delivery.target,
                        sizeof(router->last_delivery.target),
                        snapshot.target_text, strlen(snapshot.target_text));
        (void)host_copy(router->last_delivery.send_id,
                        sizeof(router->last_delivery.send_id),
                        snapshot.send_id, strlen(snapshot.send_id));
        (void)host_copy(router->last_delivery.origin,
                        sizeof(router->last_delivery.origin), source.location,
                        strlen(source.location));
        (void)host_copy(router->last_delivery.origin_type,
                        sizeof(router->last_delivery.origin_type),
                        HOST_ORIGIN_TYPE, sizeof(HOST_ORIGIN_TYPE) - 1u);
    }
    host_release_message_locked(selected);
    turbo_mutex_unlock(&router->lock);
    if (mailbox_status == CFLOW_MAILBOX_OK) return HOST_PUMP_DELIVERED;
    if (target.in_use)
        (void)cflow_scxml_session_report_adapter_error(
            target.session, CFLOW_SCXML_ADAPTER_ERROR_KIND_COMMUNICATION);
    if (source.in_use)
        (void)cflow_scxml_session_report_adapter_error(
            source.session, CFLOW_SCXML_ADAPTER_ERROR_KIND_COMMUNICATION);
    return HOST_PUMP_DROPPED;
}

static cflow_scxml_status host_compile(
    const char *source, cflow_scxml_program *program,
    cflow_scxml_diagnostic *diagnostic) {
    return cflow_scxml_compile(
        program, source, strlen(source), NULL, diagnostic);
}

static cflow_scxml_session_config host_session_config(
    const cflow_scxml_program *program, cflow_executor *executor) {
    return (cflow_scxml_session_config){
        .program = program,
        .executor = executor,
        .external_event_capacity = 4u,
        .internal_event_capacity = 4u,
        .completion_capacity = 4u,
        .microstep_limit = 32u,
        .effect_capacity = 4u,
        .adapter_internal_event_capacity = 4u};
}

spec("SCXML host Event I/O adapter contract") {
    it("publishes committed cross-session sends with SCXML field mapping") {
        static const char receiver_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'><transition event='ping' target='done'/>"
            "</state><final id='done'/></scxml>";
        host_router router;
        host_adapter_context sender_adapter;
        cflow_scxml_program receiver_program = {0};
        cflow_scxml_program sender_program = {0};
        cflow_scxml_diagnostic diagnostic = {0};
        cflow_scxml_session receiver = {0};
        cflow_scxml_session sender = {0};
        cflow_executor receiver_executor = {0};
        cflow_executor sender_executor = {0};
        cflow_scxml_session_config receiver_config;
        cflow_scxml_session_config sender_config;
        cflow_scxml_session_adapters_v3 adapters;
        cflow_statechart_instance_stats stats = {0};
        cflow_event_view go = {0};
        char receiver_location[HOST_LOCATION_CAPACITY];
        char sender_location[HOST_LOCATION_CAPACITY];
        char sender_source[512];
        size_t receiver_required = 0u;
        size_t sender_required = 0u;
        size_t receiver_endpoint = SIZE_MAX;
        size_t sender_endpoint = SIZE_MAX;

        check_true(host_router_init(&router, HOST_MESSAGE_CAPACITY));
        host_adapter_init(&sender_adapter, &router);
        check_equal(host_compile(receiver_source, &receiver_program,
                                 &diagnostic), CFLOW_SCXML_OK);
        check_true(cflow_executor_serial_init(&receiver_executor));
        receiver_config = host_session_config(
            &receiver_program, &receiver_executor);
        check_equal(cflow_scxml_session_init(&receiver, &receiver_config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(cflow_scxml_session_copy_location(
                        &receiver, receiver_location,
                        sizeof(receiver_location), &receiver_required),
                    CFLOW_SCXML_LOCATION_OK);
        check_true(host_router_register(
            &router, &receiver, &receiver_program, true, NULL,
            &receiver_endpoint));

        check_true(snprintf(
            sender_source, sizeof(sender_source),
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'><transition event='go' target='sent'>"
            "<send event='ping' target='%s' id='send-1'/></transition>"
            "</state><state id='sent'/></scxml>",
            receiver_location) > 0);
        check_equal(host_compile(sender_source, &sender_program, &diagnostic),
                    CFLOW_SCXML_OK);
        check_true(cflow_executor_serial_init(&sender_executor));
        sender_config = host_session_config(&sender_program, &sender_executor);
        adapters = (cflow_scxml_session_adapters_v3){
            .abi_version = CFLOW_SCXML_SESSION_ADAPTERS_ABI_V3,
            .struct_size = sizeof(adapters),
            .event_io = &HOST_ADAPTER,
            .event_io_user = &sender_adapter};
        check_equal(cflow_scxml_session_init_v3(
                        &sender, &sender_config, &adapters),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_register(
            &router, &sender, &sender_program, true, &sender_adapter,
            &sender_endpoint));
        check_equal(cflow_scxml_session_copy_location(
                        &sender, sender_location, sizeof(sender_location),
                        &sender_required),
                    CFLOW_SCXML_LOCATION_OK);

        check_true(cflow_scxml_program_event(
            &sender_program, "go", 2u, &go));
        check_equal(cflow_scxml_session_try_send(&sender, &go),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&sender_executor));
        check_equal(host_router_ready_count(&router), (size_t)1u);
        check_true(cflow_scxml_session_get_stats(&receiver, &stats));
        check_false(stats.done);
        check_equal(host_router_pump(&router), HOST_PUMP_DELIVERED);
        check_true(cflow_executor_wait_idle(&receiver_executor));
        check_true(cflow_scxml_session_get_stats(&receiver, &stats));
        check_true(stats.done);
        check_equal(router.last_delivery.count, (size_t)1u);
        check_equal(router.last_delivery.event, "ping");
        check_equal(router.last_delivery.target, receiver_location);
        check_equal(router.last_delivery.send_id, "send-1");
        check_equal(router.last_delivery.origin, sender_location);
        check_equal(router.last_delivery.origin_type, HOST_ORIGIN_TYPE);

        check_equal(cflow_scxml_session_destroy(&sender),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_unregister(&router, sender_endpoint));
        check_true(host_router_unregister(&router, receiver_endpoint));
        check_equal(cflow_scxml_session_destroy(&receiver),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&sender_executor);
        cflow_executor_destroy(&receiver_executor);
        cflow_scxml_program_destroy(&sender_program);
        cflow_scxml_program_destroy(&receiver_program);
        host_router_destroy(&router);
    }

    it("maps inaccessible sessions to error.communication") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'><transition event='go'>"
            "<send event='ping' target='#_scxml_missing'/></transition>"
            "<transition event='error.communication' target='done'/>"
            "</state><final id='done'/></scxml>";
        host_router router;
        host_adapter_context adapter;
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};
        cflow_scxml_session session = {0};
        cflow_executor executor = {0};
        cflow_scxml_session_config config;
        cflow_scxml_session_adapters_v3 adapters;
        cflow_statechart_instance_stats stats = {0};
        cflow_event_view go = {0};
        size_t endpoint = SIZE_MAX;

        check_true(host_router_init(&router, 1u));
        host_adapter_init(&adapter, &router);
        check_equal(host_compile(source, &program, &diagnostic),
                    CFLOW_SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        config = host_session_config(&program, &executor);
        adapters = (cflow_scxml_session_adapters_v3){
            .abi_version = CFLOW_SCXML_SESSION_ADAPTERS_ABI_V3,
            .struct_size = sizeof(adapters),
            .event_io = &HOST_ADAPTER,
            .event_io_user = &adapter};
        check_equal(cflow_scxml_session_init_v3(
                        &session, &config, &adapters),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_register(
            &router, &session, &program, true, &adapter, &endpoint));
        check_true(cflow_scxml_program_event(&program, "go", 2u, &go));
        check_equal(cflow_scxml_session_try_send(&session, &go),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(host_router_ready_count(&router), (size_t)0u);
        check_true(cflow_scxml_session_get_stats(&session, &stats));
        check_true(stats.done);
        check_false(stats.errored);

        check_equal(cflow_scxml_session_destroy(&session),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_unregister(&router, endpoint));
        cflow_executor_destroy(&executor);
        cflow_scxml_program_destroy(&program);
        host_router_destroy(&router);
    }

    it("enforces bounded reservations special routes and close quiescence") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='waiting'><transition event='ping'/></state></scxml>";
        host_router router;
        host_adapter_context adapter;
        cflow_scxml_program owner_program = {0};
        cflow_scxml_program peer_program = {0};
        cflow_scxml_diagnostic diagnostic = {0};
        cflow_scxml_session owner = {0};
        cflow_scxml_session peer = {0};
        cflow_executor owner_executor = {0};
        cflow_executor peer_executor = {0};
        cflow_scxml_session_config owner_config;
        cflow_scxml_session_config peer_config;
        cflow_scxml_session_adapters_v3 adapters;
        cflow_scxml_send_request_v3 request = {0};
        cflow_statechart_effect_ticket first = {0};
        cflow_statechart_effect_ticket second = {0};
        const char *error = NULL;
        size_t owner_endpoint = SIZE_MAX;
        size_t peer_endpoint = SIZE_MAX;

        check_true(host_router_init(&router, 1u));
        host_adapter_init(&adapter, &router);
        check_equal(host_compile(source, &owner_program, &diagnostic),
                    CFLOW_SCXML_OK);
        check_equal(host_compile(source, &peer_program, &diagnostic),
                    CFLOW_SCXML_OK);
        check_true(cflow_executor_serial_init(&owner_executor));
        check_true(cflow_executor_serial_init(&peer_executor));
        owner_config = host_session_config(&owner_program, &owner_executor);
        peer_config = host_session_config(&peer_program, &peer_executor);
        adapters = (cflow_scxml_session_adapters_v3){
            .abi_version = CFLOW_SCXML_SESSION_ADAPTERS_ABI_V3,
            .struct_size = sizeof(adapters),
            .event_io = &HOST_ADAPTER,
            .event_io_user = &adapter};
        check_equal(cflow_scxml_session_init_v3(
                        &owner, &owner_config, &adapters),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_equal(cflow_scxml_session_init(&peer, &peer_config),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_register(
            &router, &owner, &owner_program, false, &adapter,
            &owner_endpoint));
        check_true(host_router_register(
            &router, &peer, &peer_program, true, NULL, &peer_endpoint));
        check_true(host_router_set_parent(
            &router, owner_endpoint, peer_endpoint));
        check_true(host_router_set_invoke_alias(
            &router, owner_endpoint, "#_child", peer_endpoint));
        request.base.event = "ping";
        request.base.event_size = 4u;
        request.base.type = HOST_SCXML_PROCESSOR;
        request.base.type_size = sizeof(HOST_SCXML_PROCESSOR) - 1u;

        request.base.target = "#_parent";
        request.base.target_size = 8u;
        check_equal(host_prepare_send(
                        &adapter, &request, &first, &error),
                    CFLOW_SCXML_ADAPTER_ACCEPTED);
        check_equal(host_prepare_send(
                        &adapter, &request, &second, &error),
                    CFLOW_SCXML_ADAPTER_FULL);
        first.discard(first.user);

        request.base.target = "#_child";
        request.base.target_size = 7u;
        check_equal(host_prepare_send(
                        &adapter, &request, &first, &error),
                    CFLOW_SCXML_ADAPTER_ACCEPTED);
        first.discard(first.user);
        request.base.type = "urn:unsupported";
        request.base.type_size = sizeof("urn:unsupported") - 1u;
        check_equal(host_prepare_send(
                        &adapter, &request, &second, &error),
                    CFLOW_SCXML_ADAPTER_ERROR_EXECUTION);
        check_not_null(error);
        request.base.type = NULL;
        request.base.type_size = 0u;
        request.base.target = NULL;
        request.base.target_size = 0u;
        check_equal(host_prepare_send(
                        &adapter, &request, &first, &error),
                    CFLOW_SCXML_ADAPTER_ACCEPTED);
        cflow_scxml_session_close(&owner);
        check_false(host_adapter_is_quiescent(&adapter));
        check_equal(host_prepare_send(
                        &adapter, &request, &second, &error),
                    CFLOW_SCXML_ADAPTER_CLOSED);
        first.discard(first.user);
        check_true(host_adapter_is_quiescent(&adapter));
        check_equal(host_router_ready_count(&router), (size_t)0u);

        check_equal(cflow_scxml_session_destroy(&owner),
                    CFLOW_STATECHART_INSTANCE_OK);
        check_true(host_router_unregister(&router, owner_endpoint));
        check_true(host_router_unregister(&router, peer_endpoint));
        check_equal(cflow_scxml_session_destroy(&peer),
                    CFLOW_STATECHART_INSTANCE_OK);
        cflow_executor_destroy(&owner_executor);
        cflow_executor_destroy(&peer_executor);
        cflow_scxml_program_destroy(&owner_program);
        cflow_scxml_program_destroy(&peer_program);
        host_router_destroy(&router);
    }
}
