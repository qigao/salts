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
 * Object ownership is separate from cmeta_data_desc value lifecycle and from
 * any Plugin/module lease:
 *
 * BORROWED: caller owns the object; release only clears the handle.
 * SHARED:   lifecycle retain() succeeds once on admission; release() runs once.
 * OWNED:    ownership is transferred; destroy() runs exactly once on release.
 *
 * Descriptor/provider/module lifetime is still externally borrowed. In
 * particular a Plugin host must keep its DSO lease alive through object
 * teardown; these callbacks do not retain a code module implicitly.
 */
typedef enum cmeta_object_lifetime {
    CMETA_OBJECT_LIFETIME_NONE = 0,
    CMETA_OBJECT_LIFETIME_BORROWED = 1,
    CMETA_OBJECT_LIFETIME_SHARED = 2,
    CMETA_OBJECT_LIFETIME_OWNED = 3
} cmeta_object_lifetime;

typedef cmeta_status (*cmeta_object_retain_fn)(
    void *context, void *object);
typedef void (*cmeta_object_release_fn)(
    void *context, void *object);
typedef void (*cmeta_object_destroy_fn)(
    void *context, void *object);

typedef struct cmeta_object_lifecycle {
    size_t size;
    void *context;
    cmeta_object_retain_fn retain;
    cmeta_object_release_fn release;
    cmeta_object_destroy_fn destroy;
} cmeta_object_lifecycle;

bool cmeta_object_lifecycle_valid(
    const cmeta_object_lifecycle *lifecycle);

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
struct cmeta_function_data_desc;

typedef struct cmeta_object_method_binding {
    size_t size;
    const struct cmeta_function_data_desc *data;
    cmeta_callable callable;
} cmeta_object_method_binding;

#define CMETA_OBJECT_METHOD_BINDING_INIT \
    { sizeof(cmeta_object_method_binding), NULL, {0} }

typedef cmeta_status (*cmeta_object_method_bind_fn)(
    void *context, void *object, const cmeta_receiver_method *method,
    cmeta_object_method_binding *out);

typedef struct cmeta_object_method_provider {
    size_t size;
    const cmeta_receiver_method_set *methods;
    void *context;
    cmeta_object_method_bind_fn bind;
} cmeta_object_method_provider;

bool cmeta_object_method_provider_valid(
    const cmeta_object_method_provider *provider);

typedef struct cmeta_object_ref {
    size_t size;
    void *object;
    const cmeta_data_desc *data;
    const cmeta_receiver_method_set *methods;
    const cmeta_object_method_provider *method_provider;
    cmeta_object_lifetime lifetime;
    const cmeta_object_lifecycle *lifecycle;
} cmeta_object_ref;

#define CMETA_OBJECT_REF_INIT \
    { sizeof(cmeta_object_ref), NULL, NULL, NULL, NULL, \
      CMETA_OBJECT_LIFETIME_NONE, NULL }

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
 * Publish a borrowed native object with canonical executable receiver methods.
 *
 * provider->methods is both the reflected method authority and the executable
 * capability set. The provider is borrowed with the object and must remain
 * alive, together with any module/code it references, through final release.
 */
cmeta_status cmeta_object_borrow_with_provider(
    cmeta_object_ref *out, void *object, const cmeta_data_desc *data,
    const cmeta_object_method_provider *provider);

/**
 * Upgrade a BORROWED object handle to SHARED ownership.
 *
 * retain() is called exactly once. On failure the handle remains BORROWED.
 * lifecycle must stay alive until cmeta_object_release().
 */
cmeta_status cmeta_object_share(
    cmeta_object_ref *ref, const cmeta_object_lifecycle *lifecycle);

/**
 * Transfer ownership of a BORROWED object handle to CMeta.
 *
 * No callback runs at admission. destroy() runs exactly once on release.
 * lifecycle must stay alive until cmeta_object_release().
 */
cmeta_status cmeta_object_take(
    cmeta_object_ref *ref, const cmeta_object_lifecycle *lifecycle);

/**
 * Clear one object reference.
 *
 * BORROWED performs no object callback. SHARED calls lifecycle->release once.
 * OWNED calls lifecycle->destroy once. No mode releases descriptor/provider/
 * module leases; those remain an explicit outer-layer responsibility.
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
