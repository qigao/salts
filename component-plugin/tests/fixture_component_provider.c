#include "component_plugin_fixture.h"
#include <salts/plugin_decl.h>

cmeta_component_empty(ComponentPluginFixture);

static cmeta_status SALTS_COMPONENT_CALL fixture_create(
    void *provider_context,
    const cmeta_data_desc *config_data,
    const void *config_value,
    const salts_component_dependency *dependencies,
    size_t dependency_count,
    cmeta_object_ref *out_instance) {
    (void)provider_context;
    (void)config_data;
    (void)config_value;
    (void)dependencies;
    (void)dependency_count;
    (void)out_instance;
    return CMETA_CALLBACK_ERROR;
}

static const salts_component_provider_binding fixture_binding = {
    sizeof(salts_component_provider_binding),
    SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION,
    cmeta_component_meta(ComponentPluginFixture),
    NULL,
    NULL,
    fixture_create,
    NULL,
    NULL
};

static const salts_component_provider_binding *fixture_get_binding(void *self) {
    (void)self;
    return &fixture_binding;
}

CMETA_IMPLEMENTS(
    salts_component_provider,
    fixture_component_provider_impl,
    0u,
    .get_binding = fixture_get_binding);

static salts_component_provider fixture_component_provider = {
    (void *)&fixture_binding,
    &fixture_component_provider_impl_vtable
};

#define COMPONENT_PROVIDER_EXPORTS(X) \
    X(interface, (salts_component_provider, &fixture_component_provider), \
      COMPONENT_PROVIDER_EXPORT_ID, SALTS_COMPONENT_PROVIDER_CONTRACT_ID, \
      SALTS_COMPONENT_PROVIDER_CONTRACT_VERSION, 0)

CMETA_PLUGIN_DECLARE(
    component_provider_fixture,
    "test.component.provider",
    (1,0,0),
    COMPONENT_PROVIDER_EXPORTS,
    CMETA_PLUGIN_PASSIVE());
