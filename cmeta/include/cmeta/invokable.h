#ifndef CMETA_INVOKABLE_H
#define CMETA_INVOKABLE_H

#include <cmeta/cmeta.h>
#include <cmeta/data.h>
#include <cmeta/function.h>
#include <cmeta/method.h>
#include <cmeta/object.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cmeta_interface_method_desc cmeta_interface_method_desc;

typedef struct cmeta_function_data_desc {
    size_t size;
    const cmeta_function_desc *function;
    const cmeta_data_desc *return_data;
    const cmeta_data_desc *const *params;
    size_t param_count;
} cmeta_function_data_desc;

bool cmeta_function_data_desc_valid(
    const cmeta_function_data_desc *desc);

typedef struct cmeta_invokable {
    size_t size;
    const cmeta_function_desc *function;
    const cmeta_function_data_desc *data;
    cmeta_callable callable;
} cmeta_invokable;

#define CMETA_INVOKABLE_INIT \
    { sizeof(cmeta_invokable), NULL, NULL, {0} }

/**
 * Bind canonical function semantics to an executable CMeta callable.
 *
 * The callable is resolved/bound once. Parameter/return types and
 * effect/property contracts must exactly match the reflected function.
 */
cmeta_status cmeta_invokable_bind(
    const cmeta_function_desc *function, cmeta_callable callable,
    cmeta_invokable *out);

/** Bind an invokable with explicit semantic argument/return data metadata. */
cmeta_status cmeta_invokable_bind_data(
    const cmeta_function_data_desc *data, cmeta_callable callable,
    cmeta_invokable *out);

/**
 * Join one fully reflected interface method to the same canonical invokable
 * contract. The callable provider owns self/capture binding; no vtable ABI is
 * interpreted by language bindings.
 */
cmeta_status cmeta_interface_method_invokable_bind(
    const cmeta_interface_method_desc *method,
    const cmeta_function_data_desc *data,
    cmeta_callable callable, cmeta_invokable *out);

/**
 * Join a reflected receiver method to an already receiver-bound exact callable.
 *
 * data->function must be the receiver-elided projection validated by
 * cmeta_receiver_method_projection_valid(). The callable provider is
 * responsible for binding the concrete receiver through a type-correct thunk
 * or capture; CMeta never casts/interprets the original receiver ABI.
 */
cmeta_status cmeta_receiver_method_invokable_bind(
    const cmeta_receiver_method *method,
    const cmeta_function_data_desc *data,
    cmeta_callable callable, cmeta_invokable *out);

/**
 * Ask the bound object's canonical method provider for the exact
 * receiver-bound callable/FunctionData pair, then validate that pair through
 * cmeta_receiver_method_invokable_bind().
 *
 * method must be the exact entry resolved from object->methods. This pointer is
 * a provider capability token, not a replacement for semantic type identity.
 */
cmeta_status cmeta_object_method_invokable_bind(
    const cmeta_object_ref *object,
    const cmeta_receiver_method *method,
    cmeta_invokable *out);

bool cmeta_invokable_valid(const cmeta_invokable *invokable);

/**
 * Invoke only through the validated callable adapter.
 *
 * Non-void functions require a non-NULL output slot. All argument storage is
 * borrowed for the call and must match the reflected/native signature.
 */
cmeta_status cmeta_invokable_invoke(
    const cmeta_invokable *invokable, void *out,
    const void *const *args);

#ifdef __cplusplus
}
#endif

#endif /* CMETA_INVOKABLE_H */
