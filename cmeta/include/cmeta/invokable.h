#ifndef CMETA_INVOKABLE_H
#define CMETA_INVOKABLE_H

#include <cmeta/cmeta.h>
#include <cmeta/data.h>
#include <cmeta/function.h>
#include <cmeta/operation.h>
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

/** Borrowed binding produced by a successful *_invokable_bind() or
 * cmeta_invokable_bind[_data](). This public C record is caller-trusted, not
 * an unforgeable security capability. Do not forge or mutate an admitted
 * binding. Canonical descriptors/providers remain authoritative; no descriptor,
 * capture dependency, provider or Plugin/module lease is implicitly retained.
 * Their authoritative outer lifetime must cover every call and cleanup. */
typedef struct cmeta_invokable {
    size_t size;
    const cmeta_function_desc *function;
    const cmeta_function_data_desc *data;
    cmeta_callable callable;
} cmeta_invokable;

#ifdef __cplusplus
#define CMETA_INVOKABLE_INIT { sizeof(cmeta_invokable), nullptr, nullptr, {} }
#else
#define CMETA_INVOKABLE_INIT { sizeof(cmeta_invokable), NULL, NULL, {0} }
#endif

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
 * Join a reflected receiver operation to an already receiver-bound exact callable.
 *
 * data->function must be the receiver-elided projection validated by
 * cmeta_function_receiver_projection_valid(). The callable provider is
 * responsible for binding the concrete receiver through a type-correct thunk
 * or capture; CMeta never casts/interprets the original receiver ABI.
 */
cmeta_status cmeta_receiver_operation_invokable_bind(
    const cmeta_receiver_operation *operation,
    const cmeta_function_data_desc *data,
    cmeta_callable callable, cmeta_invokable *out);

/**
 * Ask the bound object's canonical operation provider for the exact
 * receiver-bound callable/FunctionData pair, then validate that pair through
 * cmeta_receiver_operation_invokable_bind().
 *
 * operation must be the exact entry resolved from object->operations. This pointer is
 * a borrowed provider entry, not semantic type identity or a security token.
 */
cmeta_status cmeta_object_operation_invokable_bind(
    const cmeta_object_ref *object,
    const cmeta_receiver_operation *operation,
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

/** Admitted fast path for an unmodified successful bind result. Metadata,
 * callable, capture dependencies and provider/module leases remain immutable
 * and live. Checks call storage, without traversing descriptor graphs again.
 * Foreign/manually assembled values must use bind or the checked invoke above. */
cmeta_status cmeta_invokable_invoke_admitted(
    const cmeta_invokable *invokable, void *out, const void *const *args);

#ifdef __cplusplus
}
#endif

#endif /* CMETA_INVOKABLE_H */
