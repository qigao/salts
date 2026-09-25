#ifndef CMETA_INVOKABLE_H
#define CMETA_INVOKABLE_H

#include <cmeta/cmeta.h>
#include <cmeta/function.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cmeta_invokable {
    size_t size;
    const cmeta_function_desc *function;
    cmeta_callable callable;
} cmeta_invokable;

#define CMETA_INVOKABLE_INIT     { sizeof(cmeta_invokable), NULL, {0} }

/**
 * Bind canonical function semantics to an executable CMeta callable.
 *
 * The callable is resolved/bound once. Parameter/return types and
 * effect/property contracts must exactly match the reflected function.
 */
cmeta_status cmeta_invokable_bind(
    const cmeta_function_desc *function, cmeta_callable callable,
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
