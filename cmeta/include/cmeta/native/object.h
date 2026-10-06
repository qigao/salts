#ifndef CMETA_NATIVE_OBJECT_H
#define CMETA_NATIVE_OBJECT_H
#include <cmeta/invokable.h>
#include <cmeta/native/thunk.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Optional execution authority attached to an existing object provider.
 * bind borrows the canonical provider context, object and operation. It must
 * return a CONTEXT_I32 binding with the same semantics as reference->bind;
 * no allocation, retain or ownership transfer is permitted here. */
typedef cmeta_status (*cmeta_native_object_bind_fn)(void *context, void *object,
    const cmeta_receiver_operation *operation, cmeta_native_binding *out);
typedef struct cmeta_native_object_provider {
    const cmeta_object_operation_provider *reference;
    cmeta_native_object_bind_fn bind;
} cmeta_native_object_provider;

/* Admit one exact native specialization after canonical ObjectRef admission.
 * reference must be object->operation_provider, and operation must be its
 * exact row. Both reference and native callbacks run once on successful
 * admission; invocation performs no provider lookup. Failure clears out and
 * propagates provider errors, or reports INVALID_ARGUMENT/TYPE_MISMATCH.
 * No owner/lease is retained. Destroy all derived thunks before object release
 * and provider/module teardown. Admission needs exclusive object access. */
cmeta_status cmeta_native_object_i32_admit(const cmeta_object_ref *object,
    const cmeta_receiver_operation *operation, const cmeta_native_object_provider *provider,
    cmeta_native_binding *out);

#ifdef __cplusplus
}
#endif
#endif
