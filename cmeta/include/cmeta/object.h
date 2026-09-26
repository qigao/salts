#ifndef CMETA_OBJECT_H
#define CMETA_OBJECT_H

#include <cmeta/data.h>
#include <cmeta/method.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Native object lifetime mode.
 *
 * Phase 1 intentionally admits only BORROWED. The caller owns the native
 * instance and every descriptor/provider referenced by the object handle and
 * must keep them alive for the complete handle lifetime. Releasing a borrowed
 * handle never destroys the native instance.
 *
 * Future shared/owned modes may extend this enum together with an explicit
 * object-lifetime provider. They are not inferred from cmeta_data_desc value
 * lifecycle and are not implemented by this contract.
 */
typedef enum cmeta_object_lifetime {
    CMETA_OBJECT_LIFETIME_NONE = 0,
    CMETA_OBJECT_LIFETIME_BORROWED = 1
} cmeta_object_lifetime;

/**
 * Provider-neutral reference to one native object instance.
 *
 * object preserves native identity; it is never a copied value projection.
 * data describes the canonical native semantic/storage type. methods is
 * optional and, when present, must describe the same receiver type.
 *
 * The size prefix reserves ABI growth for explicit lifetime/provider/module
 * lease state without placing any language-runtime handle in CMeta.
 */
typedef struct cmeta_object_ref {
    size_t size;
    void *object;
    const cmeta_data_desc *data;
    const cmeta_receiver_method_set *methods;
    cmeta_object_lifetime lifetime;
} cmeta_object_ref;

#define CMETA_OBJECT_REF_INIT \
    { sizeof(cmeta_object_ref), NULL, NULL, NULL, CMETA_OBJECT_LIFETIME_NONE }

/** Validate one currently live canonical object reference. */
bool cmeta_object_ref_valid(const cmeta_object_ref *ref);

/**
 * Publish a borrowed native object through the canonical dynamic C contract.
 *
 * The native instance, data descriptor, optional method set, and any provider
 * or module that owns them are borrowed. This call does not retain, copy, move
 * or otherwise alter the native object.
 */
cmeta_status cmeta_object_borrow(
    cmeta_object_ref *out, void *object, const cmeta_data_desc *data,
    const cmeta_receiver_method_set *methods);

/**
 * Clear one object reference.
 *
 * Phase-1 BORROWED release performs no native destruction and no provider or
 * module release. The referenced native object remains owned by its caller.
 */
void cmeta_object_release(cmeta_object_ref *ref);

/**
 * Resolve one reflected struct field and borrow its current native storage.
 *
 * No value copy or ownership transfer occurs. The returned pointer remains
 * valid only while the native object remains alive and the field is not
 * otherwise invalidated by caller-owned mutation.
 *
 * This phase is intentionally read-only. Reflected field presence alone does
 * not imply write permission; a later mutation API must carry an explicit
 * mutability contract instead of inferring writability from layout metadata.
 */
cmeta_status cmeta_object_field_read(
    const cmeta_object_ref *ref, const char *name,
    const cmeta_data_desc **out_data, const void **out_value);

/**
 * Resolve one receiver method in the context of this exact native object type.
 *
 * Resolution remains descriptive. Execution uses the canonical
 * cmeta_receiver_method_invokable_bind() join only after a provider has
 * produced an exact receiver-bound callable and receiver-elided FunctionData.
 */
cmeta_receiver_resolve_status cmeta_object_method_resolve(
    const cmeta_object_ref *ref, const char *owner_name,
    const char *method_name,
    const cmeta_type_desc *const *argument_types, size_t argument_count,
    cmeta_receiver_resolution *out);

#ifdef __cplusplus
}
#endif

#endif /* CMETA_OBJECT_H */
