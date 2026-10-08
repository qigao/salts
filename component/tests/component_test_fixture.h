#ifndef SALTS_COMPONENT_TEST_FIXTURE_H
#define SALTS_COMPONENT_TEST_FIXTURE_H

#include <salts/component.h>

#define TEST_LOG_METHODS(X, I) \
    X(I, R0, int, get, _)

#define TEST_APP_METHODS(X, I) \
    X(I, R0, int, value, _)

#define TEST_A_METHODS(X, I) \
    X(I, R0, int, a, _)

#define TEST_B_METHODS(X, I) \
    X(I, R0, int, b, _)

CMETA_INTERFACE(test_log, TEST_LOG_METHODS);
CMETA_INTERFACE(test_app, TEST_APP_METHODS);
CMETA_INTERFACE(test_a, TEST_A_METHODS);
CMETA_INTERFACE(test_b, TEST_B_METHODS);

cmeta_component(TestLogger,
    cmeta_provides(test_log));

cmeta_component_configured(TestApp, &cmeta_data_int,
    cmeta_provides(test_app)
    cmeta_requires(test_log));

cmeta_component(TestLoggerAlt,
    cmeta_provides(test_log));

cmeta_component(TestBrokenProvider,
    cmeta_provides(test_a));

cmeta_component(TestCycleA,
    cmeta_provides(test_a)
    cmeta_requires(test_b));

cmeta_component(TestCycleB,
    cmeta_provides(test_b)
    cmeta_requires(test_a));

typedef struct test_provider_state {
    int value;
    unsigned creates;
    unsigned activates;
    unsigned deactivates;
    unsigned destroys;
    unsigned sequence;
    unsigned *clock;
    bool fail_activate;
    cmeta_object_lifecycle lifecycle;
} test_provider_state;

static int test_log_get_impl(void *self) {
    return *(int *)self;
}

static int test_app_value_impl(void *self) {
    return *(int *)self;
}

CMETA_IMPLEMENTS(test_log, test_log_impl, 0u,
    .get = test_log_get_impl);

CMETA_IMPLEMENTS(test_app, test_app_impl, 0u,
    .value = test_app_value_impl);

static cmeta_status test_interface_project(
    void *context,
    const cmeta_object_ref *object,
    const cmeta_interface_desc *expected,
    cmeta_interface_projection *out) {
    (void)context;
    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;

    if (cmeta_interface_desc_equal(expected, test_log_interface())) {
        out->size = sizeof(*out);
        out->interface = test_log_interface();
        out->self = object->object;
        out->dispatch = &test_log_impl_vtable;
        return CMETA_OK;
    }
    if (cmeta_interface_desc_equal(expected, test_app_interface())) {
        out->size = sizeof(*out);
        out->interface = test_app_interface();
        out->self = object->object;
        out->dispatch = &test_app_impl_vtable;
        return CMETA_OK;
    }
    return CMETA_TRAIT_MISSING;
}

static const cmeta_object_interface_provider test_interfaces = {
    sizeof(cmeta_object_interface_provider),
    NULL,
    test_interface_project
};

CMETA_OBJECT_INTERFACE_ADAPTER(test_log);
CMETA_OBJECT_INTERFACE_ADAPTER(test_app);

static void test_destroy(void *context, void *object) {
    test_provider_state *state = (test_provider_state *)context;
    (void)object;
    ++state->destroys;
}

static void test_provider_state_init(
    test_provider_state *state,
    int value,
    unsigned *clock) {
    *state = (test_provider_state){0};
    state->value = value;
    state->clock = clock;
    state->lifecycle.size = sizeof(state->lifecycle);
    state->lifecycle.context = state;
    state->lifecycle.destroy = test_destroy;
}

static cmeta_status test_publish_owned_int(
    test_provider_state *state,
    cmeta_object_ref *out) {
    cmeta_status status =
        cmeta_object_borrow(out, &state->value, &cmeta_data_int, NULL);
    if (status != CMETA_OK)
        return status;
    return cmeta_object_take(out, &state->lifecycle);
}

static cmeta_status test_logger_create(
    void *provider_context,
    const cmeta_data_desc *config_data,
    const void *config_value,
    const salts_component_dependency *dependencies,
    size_t dependency_count,
    cmeta_object_ref *out_instance) {
    test_provider_state *state = (test_provider_state *)provider_context;
    (void)config_data;
    (void)config_value;
    (void)dependencies;
    if (dependency_count != 0u)
        return CMETA_CALLBACK_ERROR;
    ++state->creates;
    return test_publish_owned_int(state, out_instance);
}

static cmeta_status test_app_create(
    void *provider_context,
    const cmeta_data_desc *config_data,
    const void *config_value,
    const salts_component_dependency *dependencies,
    size_t dependency_count,
    cmeta_object_ref *out_instance) {
    test_provider_state *state = (test_provider_state *)provider_context;
    const salts_component_dependency *dependency = NULL;
    test_log logger = test_log_bind(NULL, NULL);
    cmeta_status status;
    salts_component_status component_status;

    ++state->creates;
    if (!cmeta_data_desc_equal(config_data, &cmeta_data_int) ||
        config_value == NULL)
        return CMETA_TYPE_MISMATCH;

    component_status = salts_component_dependency_find(
        dependencies, dependency_count, test_log_interface(), &dependency);
    if (component_status != SALTS_COMPONENT_OK)
        return CMETA_CALLBACK_ERROR;

    status = test_log_borrow_from_object(
        dependency->provider_instance,
        dependency->provider_interfaces,
        &logger);
    if (status != CMETA_OK)
        return status;

    state->value = test_log_get(&logger) + *(const int *)config_value;
    return test_publish_owned_int(state, out_instance);
}

static cmeta_status test_activate(
    void *provider_context,
    const cmeta_object_ref *instance) {
    test_provider_state *state = (test_provider_state *)provider_context;
    if (!cmeta_object_ref_valid(instance))
        return CMETA_INVALID_ARGUMENT;
    ++state->activates;
    if (state->clock != NULL)
        state->sequence = ++*state->clock;
    return state->fail_activate ? CMETA_CALLBACK_ERROR : CMETA_OK;
}

static void test_deactivate(
    void *provider_context,
    const cmeta_object_ref *instance) {
    test_provider_state *state = (test_provider_state *)provider_context;
    (void)instance;
    ++state->deactivates;
}

#define TEST_PROVIDER_BINDING(component_, state_, create_) \
    ((salts_component_provider_binding){ \
        sizeof(salts_component_provider_binding), \
        SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION, \
        cmeta_component_meta(component_), (state_), &test_interfaces, \
        (create_), test_activate, test_deactivate })

#define TEST_DEPLOYMENT(provider_, config_data_, config_value_) \
    ((salts_component_deployment){ \
        (provider_), (config_data_), (config_value_) })

static cmeta_status test_cycle_create(
    void *provider_context,
    const cmeta_data_desc *config_data,
    const void *config_value,
    const salts_component_dependency *dependencies,
    size_t dependency_count,
    cmeta_object_ref *out_instance) {
    (void)config_data;
    (void)config_value;
    (void)dependencies;
    (void)dependency_count;
    return test_publish_owned_int(
        (test_provider_state *)provider_context, out_instance);
}

#endif
