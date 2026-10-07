#ifndef CMETA_PATTERN_FIXTURE_H
#define CMETA_PATTERN_FIXTURE_H

#include <cmeta/function.h>
#include <cmeta/object.h>
#include <cmeta/object_interface.h>

#define PATTERN_COUNTER_METHODS(X, I) \
    X(I, R0, int, get, _)

#define PATTERN_RESET_METHODS(X, I) \
    X(I, V0, void, reset, _)

#define PATTERN_OWNING_METHODS(X, I) \
    X(I, D0, void, destroy, _)

CMETA_INTERFACE(pattern_counter, PATTERN_COUNTER_METHODS);
CMETA_INTERFACE(pattern_reset, PATTERN_RESET_METHODS);
CMETA_INTERFACE(pattern_owning, PATTERN_OWNING_METHODS);

static int pattern_counter_get_impl(void *self) {
    return *(int *)self;
}

static void pattern_reset_reset_impl(void *self) {
    *(int *)self = 0;
}

CMETA_IMPLEMENTS(pattern_counter, pattern_counter_impl, 0u,
    .get = pattern_counter_get_impl);

CMETA_IMPLEMENTS(pattern_reset, pattern_reset_impl, 0u,
    .reset = pattern_reset_reset_impl);

/*
 * Factory is a semantic composition, not a separate runtime:
 * exact Function metadata + explicit owned result.
 * The function is intentionally not invoked by this fixture.
 */
Function0DeclAsAbiResult(
    value,
    void *,
    &cmeta_type_void_ptr,
    CMETA_ABI_OBJECT_POINTER,
    CMETA_RESULT_OWNED,
    pattern_owned_factory);

static cmeta_status pattern_extension_project(
    void *context,
    const cmeta_object_ref *object,
    const cmeta_interface_desc *expected,
    cmeta_interface_projection *out) {
    (void)context;

    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;

    if (cmeta_interface_desc_equal(expected, pattern_counter_interface())) {
        out->size = sizeof(*out);
        out->interface = pattern_counter_interface();
        out->self = object->object;
        out->dispatch = &pattern_counter_impl_vtable;
        return CMETA_OK;
    }

    if (cmeta_interface_desc_equal(expected, pattern_reset_interface())) {
        out->size = sizeof(*out);
        out->interface = pattern_reset_interface();
        out->self = object->object;
        out->dispatch = &pattern_reset_impl_vtable;
        return CMETA_OK;
    }

    return CMETA_TRAIT_MISSING;
}

CMETA_OBJECT_INTERFACE_ADAPTER(pattern_counter);
CMETA_OBJECT_INTERFACE_ADAPTER(pattern_reset);
CMETA_OBJECT_INTERFACE_ADAPTER(pattern_owning);

static const cmeta_object_interface_provider pattern_extension_provider = {
    sizeof(cmeta_object_interface_provider),
    NULL,
    pattern_extension_project
};

#endif /* CMETA_PATTERN_FIXTURE_H */
