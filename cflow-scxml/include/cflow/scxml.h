#ifndef CFLOW_SCXML_H
#define CFLOW_SCXML_H

#include <cflow/event.h>
#include <cflow/statechart.h>
#include <cflow/statechart_runtime.h>
#include <xml_parser/xml_parser.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CFLOW_SCXML_DIAGNOSTIC_CAPACITY 256u

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

cflow_scxml_limits cflow_scxml_default_limits(void);

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

/**
 * Borrow the native executable bindings compiled for this program.
 *
 * A structural program succeeds with a NULL view and zero count. The returned
 * rows and their callback user pointers are invalidated by program destruction,
 * so the program must outlive every Statechart instance configured with them.
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
 */
bool cflow_scxml_program_guard_bindings(
    const cflow_scxml_program *program,
    const cflow_statechart_guard_binding **out_bindings,
    size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_SCXML_H */
