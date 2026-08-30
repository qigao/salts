#ifndef CFLOW_SCXML_H
#define CFLOW_SCXML_H

#include <cflow/event.h>
#include <cflow/statechart.h>
#include <cflow/statechart_runtime.h>
#include <cmeta/data.h>
#include <xml_parser/xml_parser.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CFLOW_SCXML_DIAGNOSTIC_CAPACITY 256u
#define CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V1 1u
#define CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V2 2u
#define CFLOW_SCXML_INVOKE_ADAPTER_ABI_V1 1u
#define CFLOW_SCXML_INVOKE_ADAPTER_ABI_V2 2u
#define CFLOW_SCXML_SESSION_ADAPTERS_ABI_V2 2u
#define CFLOW_SCXML_CMETA_COMPILE_OPTIONS_ABI_V1 1u
#define CFLOW_SCXML_CMETA_SESSION_OPTIONS_ABI_V1 1u
#define CFLOW_SCXML_CMETA_DEFAULT_MAX_ITERATIONS 65536u

#ifndef CFLOW_SCXML_PAYLOAD_MAX_ENTRIES
#define CFLOW_SCXML_PAYLOAD_MAX_ENTRIES 64u
#endif

typedef enum cflow_scxml_status {
    CFLOW_SCXML_OK = 0,
    CFLOW_SCXML_INVALID_ARGUMENT,
    CFLOW_SCXML_LIMIT_EXCEEDED,
    CFLOW_SCXML_ALLOCATION_FAILED,
    CFLOW_SCXML_XML_ERROR,
    CFLOW_SCXML_INVALID_NAMESPACE,
    CFLOW_SCXML_INVALID_VERSION,
    CFLOW_SCXML_UNSUPPORTED_DATAMODEL,
    CFLOW_SCXML_DUPLICATE_ID,
    CFLOW_SCXML_UNKNOWN_TARGET,
    CFLOW_SCXML_INVALID_STRUCTURE,
    CFLOW_SCXML_UNSUPPORTED_FEATURE,
    CFLOW_SCXML_NATIVE_IR_REJECTED
} cflow_scxml_status;

typedef struct cflow_scxml_limits {
    turbo_xml_limits xml;
    size_t max_states;
    size_t max_events;
    size_t max_transitions;
    size_t max_name_bytes;
} cflow_scxml_limits;

typedef struct cflow_scxml_diagnostic {
    cflow_scxml_status status;
    turbo_xml_location location;
    char message[CFLOW_SCXML_DIAGNOSTIC_CAPACITY];
} cflow_scxml_diagnostic;

typedef struct cflow_scxml_program {
    void *impl;
} cflow_scxml_program;

/**
 * Versioned compile-time provider for the opt-in `datamodel="cmeta"` profile.
 *
 * `root` and every descriptor reachable from it remain borrowed and immutable
 * until program destruction. All limits are positive hard bounds; expression
 * programs are compiled once and owned by the resulting SCXML program.
 */
typedef struct cflow_scxml_cmeta_compile_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const cmeta_data_desc *root;
    size_t max_source_bytes;
    size_t max_instructions;
    size_t max_operands;
    size_t max_expression_depth;
    size_t max_path_depth;
    size_t max_literal_bytes;
    size_t max_string_bytes;
    /** Maximum items visited by one `<foreach>` invocation. */
    size_t max_iterations;
} cflow_scxml_cmeta_compile_options_v1;

/**
 * Versioned per-session state provider for a CMeta-compiled program.
 * `initial_state` is borrowed only until initialization returns; the native
 * Statechart copies it using the program root descriptor's storage type.
 */
typedef struct cflow_scxml_cmeta_session_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const void *initial_state;
} cflow_scxml_cmeta_session_options_v1;

typedef enum cflow_scxml_program_requirement {
    CFLOW_SCXML_REQUIREMENT_NONE = 0u,
    CFLOW_SCXML_REQUIREMENT_EVENT_IO = 1u << 0u,
    CFLOW_SCXML_REQUIREMENT_DELAYED_SEND = 1u << 1u,
    CFLOW_SCXML_REQUIREMENT_CANCEL = 1u << 2u,
    CFLOW_SCXML_REQUIREMENT_INVOKE = 1u << 3u,
    CFLOW_SCXML_REQUIREMENT_PAYLOAD = 1u << 4u,
    CFLOW_SCXML_REQUIREMENT_INVOKE_PAYLOAD = 1u << 5u,
    CFLOW_SCXML_REQUIREMENT_INVOKE_IDLOCATION = 1u << 6u
} cflow_scxml_program_requirement;

typedef enum cflow_scxml_event_io_capability {
    CFLOW_SCXML_EVENT_IO_CAP_SEND = UINT64_C(1) << 0u,
    CFLOW_SCXML_EVENT_IO_CAP_DELAYED_SEND = UINT64_C(1) << 1u,
    CFLOW_SCXML_EVENT_IO_CAP_CANCEL = UINT64_C(1) << 2u,
    CFLOW_SCXML_EVENT_IO_CAP_PAYLOAD = UINT64_C(1) << 3u
} cflow_scxml_event_io_capability;

typedef enum cflow_scxml_adapter_status {
    CFLOW_SCXML_ADAPTER_ACCEPTED = 0,
    CFLOW_SCXML_ADAPTER_ERROR_EXECUTION,
    CFLOW_SCXML_ADAPTER_ERROR_COMMUNICATION,
    CFLOW_SCXML_ADAPTER_FULL,
    CFLOW_SCXML_ADAPTER_CLOSED,
    CFLOW_SCXML_ADAPTER_INVALID_CONTRACT
} cflow_scxml_adapter_status;

typedef enum cflow_scxml_adapter_error_kind {
    CFLOW_SCXML_ADAPTER_ERROR_KIND_EXECUTION = 1,
    CFLOW_SCXML_ADAPTER_ERROR_KIND_COMMUNICATION
} cflow_scxml_adapter_error_kind;

#ifndef CFLOW_SCXML_EVENT_METADATA_CAPACITY
#define CFLOW_SCXML_EVENT_METADATA_CAPACITY 256u
#endif

/** Borrowed external Event metadata copied by v2 session admission. */
typedef struct cflow_scxml_event_metadata {
    const char *send_id;
    size_t send_id_size;
    const char *origin;
    size_t origin_size;
    const char *origin_type;
    size_t origin_type_size;
    const char *invoke_id;
    size_t invoke_id_size;
    /** UTF-8 scalar data exposed as `_event.data`. */
    const char *data;
    size_t data_size;
} cflow_scxml_event_metadata;

/** Borrowed literal request fields valid only during one prepare callback. */
typedef struct cflow_scxml_send_request {
    const char *event;
    size_t event_size;
    const char *target;
    size_t target_size;
    const char *type;
    size_t type_size;
    const char *id;
    size_t id_size;
    uint64_t delay_ms;
} cflow_scxml_send_request;

typedef enum cflow_scxml_payload_value_kind {
    CFLOW_SCXML_PAYLOAD_VALUE_INVALID = 0,
    CFLOW_SCXML_PAYLOAD_VALUE_BOOL,
    CFLOW_SCXML_PAYLOAD_VALUE_SINT,
    CFLOW_SCXML_PAYLOAD_VALUE_UINT,
    CFLOW_SCXML_PAYLOAD_VALUE_FLOAT,
    CFLOW_SCXML_PAYLOAD_VALUE_STRING
} cflow_scxml_payload_value_kind;

/** Format-neutral scalar copied or borrowed only for one prepare callback. */
typedef struct cflow_scxml_payload_value {
    cflow_scxml_payload_value_kind kind;
    union {
        bool boolean;
        int64_t sint;
        uint64_t uint;
        double number;
        struct {
            const char *data;
            size_t size;
        } string;
    } data;
} cflow_scxml_payload_value;

typedef struct cflow_scxml_payload_entry {
    const char *name;
    size_t name_size;
    cflow_scxml_payload_value value;
} cflow_scxml_payload_entry;

typedef enum cflow_scxml_payload_kind {
    CFLOW_SCXML_PAYLOAD_NONE = 0,
    CFLOW_SCXML_PAYLOAD_CONTENT,
    CFLOW_SCXML_PAYLOAD_NAMED
} cflow_scxml_payload_kind;

/**
 * Callback-scoped payload view. Named entries preserve SCXML order and
 * duplicates. Every pointer is invalid after the prepare callback returns.
 */
typedef struct cflow_scxml_payload_view {
    cflow_scxml_payload_kind kind;
    cflow_scxml_payload_value content;
    const cflow_scxml_payload_entry *entries;
    size_t entry_count;
} cflow_scxml_payload_view;

typedef struct cflow_scxml_send_request_v2 {
    cflow_scxml_send_request base;
    cflow_scxml_payload_view payload;
} cflow_scxml_send_request_v2;

typedef struct cflow_scxml_cancel_request {
    const char *send_id;
    size_t send_id_size;
} cflow_scxml_cancel_request;

/**
 * Versioned Event I/O reservation table copied by session initialization.
 *
 * Prepare callbacks run on the session SerialExecutor. They must reserve all
 * capacity without publishing the effect and copy every request field needed
 * after return. ACCEPTED transfers one valid move-only ticket to the session;
 * its commit or discard callback is invoked exactly once and must be
 * nonblocking and infallible. Non-ACCEPTED results transfer no ticket.
 *
 * `close` is nonblocking and called exactly once after adapter attachment,
 * including initialization failures. Once `is_quiescent` returns true after
 * close, no adapter-owned callback may reach the borrowed session or user.
 */
typedef struct cflow_scxml_event_io_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t capabilities;
    cflow_scxml_adapter_status (*prepare_send)(
        void *user, const cflow_scxml_send_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    cflow_scxml_adapter_status (*prepare_cancel)(
        void *user, const cflow_scxml_cancel_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    void (*close)(void *user);
    bool (*is_quiescent)(void *user);
} cflow_scxml_event_io_adapter_v1;

/** Payload-aware Event I/O adapter; ownership otherwise matches v1. */
typedef struct cflow_scxml_event_io_adapter_v2 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t capabilities;
    cflow_scxml_adapter_status (*prepare_send)(
        void *user, const cflow_scxml_send_request_v2 *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    cflow_scxml_adapter_status (*prepare_cancel)(
        void *user, const cflow_scxml_cancel_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    void (*close)(void *user);
    bool (*is_quiescent)(void *user);
} cflow_scxml_event_io_adapter_v2;

typedef enum cflow_scxml_invoke_capability {
    CFLOW_SCXML_INVOKE_CAP_START = UINT64_C(1) << 0u,
    CFLOW_SCXML_INVOKE_CAP_CANCEL = UINT64_C(1) << 1u,
    CFLOW_SCXML_INVOKE_CAP_FORWARD = UINT64_C(1) << 2u,
    CFLOW_SCXML_INVOKE_CAP_PAYLOAD = UINT64_C(1) << 3u
} cflow_scxml_invoke_capability;

/** Borrowed invocation fields valid only during one prepare callback. */
typedef struct cflow_scxml_invoke_start_request {
    uint64_t token;
    const char *id;
    size_t id_size;
    const char *type;
    size_t type_size;
    const char *src;
    size_t src_size;
    bool autoforward;
} cflow_scxml_invoke_start_request;

typedef struct cflow_scxml_invoke_start_request_v2 {
    cflow_scxml_invoke_start_request base;
    cflow_scxml_payload_view payload;
} cflow_scxml_invoke_start_request_v2;

typedef struct cflow_scxml_invoke_cancel_request {
    uint64_t token;
    const char *id;
    size_t id_size;
} cflow_scxml_invoke_cancel_request;

typedef struct cflow_scxml_invoke_forward_request {
    uint64_t token;
    const char *id;
    size_t id_size;
    /** Borrowed Event view valid only for the callback duration. */
    const cflow_event_view *event;
} cflow_scxml_invoke_forward_request;

/**
 * Versioned invocation adapter copied by session initialization.
 *
 * Prepare callbacks run on the session SerialExecutor without the session
 * registry mutex held. ACCEPTED transfers one valid move-only effect ticket;
 * the session invokes exactly one of commit and discard. The adapter must copy
 * every borrowed request field retained after return. `close` and
 * `is_quiescent` follow the Event I/O adapter ownership contract above.
 */
typedef struct cflow_scxml_invoke_adapter_v1 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t capabilities;
    cflow_scxml_adapter_status (*prepare_start)(
        void *user, const cflow_scxml_invoke_start_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    cflow_scxml_adapter_status (*prepare_cancel)(
        void *user, const cflow_scxml_invoke_cancel_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    cflow_scxml_adapter_status (*prepare_forward)(
        void *user, const cflow_scxml_invoke_forward_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    void (*close)(void *user);
    bool (*is_quiescent)(void *user);
} cflow_scxml_invoke_adapter_v1;

/** Payload-aware invocation adapter; ownership otherwise matches v1. */
typedef struct cflow_scxml_invoke_adapter_v2 {
    uint32_t abi_version;
    size_t struct_size;
    uint64_t capabilities;
    cflow_scxml_adapter_status (*prepare_start)(
        void *user, const cflow_scxml_invoke_start_request_v2 *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    cflow_scxml_adapter_status (*prepare_cancel)(
        void *user, const cflow_scxml_invoke_cancel_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    cflow_scxml_adapter_status (*prepare_forward)(
        void *user, const cflow_scxml_invoke_forward_request *request,
        cflow_statechart_effect_ticket *out_ticket,
        const char **out_error);
    void (*close)(void *user);
    bool (*is_quiescent)(void *user);
} cflow_scxml_invoke_adapter_v2;

/**
 * Optional v2 adapter injection used with the unchanged base session config.
 * A non-NULL v2 adapter requires the corresponding v1 config field to be
 * NULL. Tables are copied; user pointers remain borrowed through destruction.
 */
typedef struct cflow_scxml_session_adapters_v2 {
    uint32_t abi_version;
    size_t struct_size;
    const cflow_scxml_event_io_adapter_v2 *event_io;
    void *event_io_user;
    const cflow_scxml_invoke_adapter_v2 *invoke;
    void *invoke_user;
} cflow_scxml_session_adapters_v2;

typedef struct cflow_scxml_invoke_stats {
    uint64_t started;
    uint64_t start_failed;
    uint64_t cancelled;
    /** Committed exits whose adapter cancellation could not be published. */
    uint64_t cancel_failed;
    uint64_t completed;
    uint64_t returned_accepted;
    uint64_t returned_rejected;
    uint64_t forwarded;
    uint64_t forward_failed;
    /** Recoverable adapter errors rejected by the bounded internal ingress. */
    uint64_t adapter_error_rejected;
    size_t active;
} cflow_scxml_invoke_stats;

typedef struct cflow_scxml_session_config {
    /** Borrowed immutable program; it must outlive session destruction. */
    const cflow_scxml_program *program;
    cflow_executor *executor;
    size_t external_event_capacity;
    size_t internal_event_capacity;
    size_t completion_capacity;
    size_t microstep_limit;
    size_t max_storage_bytes;
    cflow_clock *clock;
    size_t timer_capacity;
    /** Maximum effect tickets staged by one rollback-capable microstep. */
    size_t effect_capacity;
    /** Bounded MPSC ingress for asynchronous adapter error Events. */
    size_t adapter_internal_event_capacity;
    /** Maximum retained delayed sends in this session. */
    size_t delayed_send_capacity;
    /** Ops are copied; adapter_user remains borrowed through destruction. */
    const cflow_scxml_event_io_adapter_v1 *event_io;
    void *adapter_user;
    /** Fixed invocation registry rows; must cover the compiled descriptors. */
    size_t invocation_capacity;
    /** Ops are copied; invoke_user remains borrowed through destruction. */
    const cflow_scxml_invoke_adapter_v1 *invoke;
    void *invoke_user;
} cflow_scxml_session_config;

typedef struct cflow_scxml_session {
    void *impl;
} cflow_scxml_session;

cflow_scxml_limits cflow_scxml_default_limits(void);

/** Return v1 bounded defaults with `root` installed as a borrowed schema. */
cflow_scxml_cmeta_compile_options_v1
cflow_scxml_cmeta_default_compile_options(const cmeta_data_desc *root);

/**
 * Validate and compile an SCXML Core document into one owning program.
 * `out` must be zero-initialized. Input and temporary XML/IR rows are copied;
 * failure leaves `out` empty and returns the first diagnostic detected by the
 * deterministic admission pipeline. Within a validation phase, document order
 * is preserved.
 */
cflow_scxml_status cflow_scxml_compile(
    cflow_scxml_program *out,
    const char *input,
    size_t input_size,
    const cflow_scxml_limits *limits,
    cflow_scxml_diagnostic *diagnostic);

/**
 * Compile an exact `datamodel="cmeta"` document with an explicit provider.
 * There is no implicit provider fallback. Ownership and failure guarantees
 * otherwise match `cflow_scxml_compile()`.
 */
cflow_scxml_status cflow_scxml_compile_cmeta(
    cflow_scxml_program *out,
    const char *input,
    size_t input_size,
    const cflow_scxml_limits *limits,
    const cflow_scxml_cmeta_compile_options_v1 *options,
    cflow_scxml_diagnostic *diagnostic);

/** Destroy a quiescent program and its native Statechart/name mappings. */
void cflow_scxml_program_destroy(cflow_scxml_program *program);

/** Borrowed Statechart; invalid after program destruction. */
const cflow_statechart *cflow_scxml_program_statechart(
    const cflow_scxml_program *program);

bool cflow_scxml_program_state_id(const cflow_scxml_program *program,
                                  const char *name,
                                  size_t name_size,
                                  cflow_machine_state_id *out_id);
bool cflow_scxml_program_event_id(const cflow_scxml_program *program,
                                  const char *name,
                                  size_t name_size,
                                  cflow_event_id *out_id);

/** Borrowed inert `false` value matching the program's null data-model type. */
const void *cflow_scxml_program_initial_state(
    const cflow_scxml_program *program);

/**
 * Construct a borrowed null-data-model Event view by name. The CFlow runtime
 * copies the payload during successful mailbox admission.
 */
bool cflow_scxml_program_event(const cflow_scxml_program *program,
                               const char *name,
                               size_t name_size,
                               cflow_event_view *out_event);

/** Copy the program's immutable execution requirements bitmask. */
bool cflow_scxml_program_requirements(
    const cflow_scxml_program *program, uint32_t *out_requirements);

/**
 * Borrow the native executable bindings compiled for this program.
 *
 * A structural program succeeds with a NULL view and zero count. The returned
 * rows and their callback user pointers are invalidated by program destruction,
 * so the program must outlive every Statechart instance configured with them.
 * CMeta `_event.name` is a read-only borrowed view while a native contextual
 * callback carries an Event; it is unavailable during initial or later
 * eventless work. The remaining SCXML `_event` fields are not admitted.
 * CMeta expressions that read `_sessionid` require the owning session adapters
 * installed by `cflow_scxml_session_init_cmeta()` and fail through these
 * program-level rows.
 * Invalid arguments return false without modifying either output.
 */
bool cflow_scxml_program_runtime_bindings(
    const cflow_scxml_program *program,
    const cflow_statechart_executable_binding **out_bindings,
    size_t *out_count);

/**
 * Borrow the native guard bindings compiled for transition conditions.
 *
 * A program without conditioned transitions succeeds with a NULL view and
 * zero count. The returned rows and callback user pointers are invalidated by
 * program destruction, so the program must outlive every configured
 * Statechart instance. Invalid arguments return false without modifying either
 * output.
 * CMeta `_event.name` is resolved from the current Event for event-triggered
 * guards. Eventless guards have no `_event` binding, and other `_event` fields
 * are not admitted.
 * CMeta guards that read `_sessionid` require the owning session adapters
 * installed by `cflow_scxml_session_init_cmeta()`.
 */
bool cflow_scxml_program_guard_bindings(
    const cflow_scxml_program *program,
    const cflow_statechart_guard_binding **out_bindings,
    size_t *out_count);

/**
 * Initialize one owning mutable SCXML session over an immutable program.
 * Required capacities and adapter capabilities are checked against the
 * program requirements before attachment. The program, executor, and adapter
 * user remain borrowed until successful session destruction.
 */
cflow_statechart_runtime_status cflow_scxml_session_init(
    cflow_scxml_session *session,
    const cflow_scxml_session_config *config);

/**
 * Initialize a CMeta program session from one call-scoped initial object.
 * The session copies the document name and generates an immutable UUID string
 * for `_sessionid` before attaching the native runtime.
 */
cflow_statechart_runtime_status cflow_scxml_session_init_cmeta(
    cflow_scxml_session *session,
    const cflow_scxml_session_config *config,
    const cflow_scxml_cmeta_session_options_v1 *options);

/** Initialize a null-data-model session with opt-in payload-aware adapters. */
cflow_statechart_runtime_status cflow_scxml_session_init_v2(
    cflow_scxml_session *session,
    const cflow_scxml_session_config *config,
    const cflow_scxml_session_adapters_v2 *adapters);

/** Initialize a CMeta session with opt-in payload-aware adapters. */
cflow_statechart_runtime_status cflow_scxml_session_init_cmeta_v2(
    cflow_scxml_session *session,
    const cflow_scxml_session_config *config,
    const cflow_scxml_cmeta_session_options_v1 *options,
    const cflow_scxml_session_adapters_v2 *adapters);

cflow_mailbox_status cflow_scxml_session_try_send(
    cflow_scxml_session *session, const cflow_event_view *event);

/**
 * Copy one external Event and its bounded metadata atomically. Metadata rows
 * are capacity-coupled to the configured external mailbox and released before
 * transition selection. Oversized or partially NULL fields fail admission.
 */
cflow_mailbox_status cflow_scxml_session_try_send_v2(
    cflow_scxml_session *session, const cflow_event_view *event,
    const cflow_scxml_event_metadata *metadata);
/**
 * Copy one returned invocation Event into the external FIFO with its live
 * session token. Admission validates the token once; external preprocessing
 * revalidates it to close the admission/cancellation race. Stale tokens return
 * `INVALID_ARGUMENT` before admission or are dropped after dequeue.
 */
cflow_mailbox_status cflow_scxml_session_report_invoke_event(
    cflow_scxml_session *session, uint64_t token,
    const cflow_event_view *event);
/**
 * Admit the compiled done Event for one live invocation token. Dynamic
 * `idlocation` identity is exposed as `done.invoke.<active-id>` and through
 * `_event.invokeid`; the finite compiled Event ID remains the routing key.
 * Zero, stale, completed, or cancelled tokens return `INVALID_ARGUMENT`.
 */
cflow_mailbox_status cflow_scxml_session_report_invoke_done(
    cflow_scxml_session *session, uint64_t token);
/**
 * Concurrently admit one asynchronous adapter failure to the prioritized
 * bounded internal ingress. The exact mailbox result is returned; there is no
 * retry or external-queue fallback.
 */
cflow_mailbox_status cflow_scxml_session_report_adapter_error(
    cflow_scxml_session *session,
    cflow_scxml_adapter_error_kind kind);
/**
 * Release one committed delayed-send registry row. Returns true only when the
 * named row was active in this session and this call won the completion race.
 */
bool cflow_scxml_session_report_send_done(
    cflow_scxml_session *session, const char *send_id, size_t send_id_size);
void cflow_scxml_session_close(cflow_scxml_session *session);
void cflow_scxml_session_cancel(cflow_scxml_session *session);
bool cflow_scxml_session_get_stats(
    const cflow_scxml_session *session,
    cflow_statechart_instance_stats *out);
/** Copy the fixed invocation registry counters under the session mutex. */
bool cflow_scxml_session_get_invoke_stats(
    const cflow_scxml_session *session, cflow_scxml_invoke_stats *out);
const char *cflow_scxml_session_error(
    const cflow_scxml_session *session);

/**
 * Stop admission and close the adapter exactly once. Destruction returns
 * `WOULD_BLOCK` while the adapter reports non-quiescent and preserves the
 * owning handle for a later retry.
 */
cflow_statechart_runtime_status cflow_scxml_session_destroy(
    cflow_scxml_session *session);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_SCXML_H */
