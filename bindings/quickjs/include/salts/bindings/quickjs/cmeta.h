#ifndef SALTS_BINDINGS_QUICKJS_CMETA_H
#define SALTS_BINDINGS_QUICKJS_CMETA_H

#include <cmeta/data.h>
#include <cmeta/invokable.h>
#include <cmeta/object.h>
#include <quickjs.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct salts_quickjs_limits {
  size_t max_depth;
  size_t max_items;
  size_t max_bytes;
} salts_quickjs_limits;

/**
 * Project one borrowed native value into an independently owned QuickJS value.
 * On success the caller owns out_value and releases it with JS_FreeValue().
 * On failure out_value remains JS_UNDEFINED.
 */
cmeta_status salts_quickjs_push_cmeta(
    JSContext *context, const cmeta_data_desc *data, const void *object,
    salts_quickjs_limits limits, JSValue *out_value);

/**
 * Read a QuickJS value into caller-provided canonical semantic-zero storage.
 * Aggregate/container/map conversion is transactional: failure leaves the
 * destination unchanged. Native ownership is governed only by CMeta providers.
 */
cmeta_status salts_quickjs_read_cmeta(
    JSContext *context, JSValueConst value, const cmeta_data_desc *data,
    void *object, salts_quickjs_limits limits);

/**
 * Convert JavaScript arguments through FunctionData and invoke a validated
 * CMeta callable. A reflected interface method uses the same entry point after
 * cmeta_interface_method_invokable_bind() joins it to an invokable.
 *
 * On success out_result is caller-owned. Void calls return JS_UNDEFINED and
 * set out_has_result to false. Failures leave both outputs cleared.
 */
cmeta_status salts_quickjs_call_invokable(
    JSContext *context, const cmeta_invokable *invokable,
    int argument_count, JSValueConst *arguments, salts_quickjs_limits limits,
    JSValue *out_result, bool *out_has_result);

/**
 * Move one canonical native object handle into a live QuickJS proxy.
 *
 * The returned JS object reads reflected fields from the current native
 * instance and invokes receiver methods only through the canonical CMeta
 * object-method provider/invokable path. The VM owns the moved object-handle
 * lifetime and releases it exactly once when the last native closure retained
 * by the proxy becomes unreachable.
 *
 * Reflected fields are writable only when the object carries explicit
 * cmeta_object_field_provider authority; writes route through canonical
 * semantic temporaries and cmeta_object_field_assign(). Reflection alone never
 * grants writability. No QuickJS-private semantic type/method registry is
 * created.
 */
cmeta_status salts_quickjs_push_object(
    JSContext *context, cmeta_object_ref *object,
    salts_quickjs_limits limits, JSValue *out_value);

#ifdef __cplusplus
}
#endif

#endif
