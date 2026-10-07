#include <salts/component.h>

cmeta_component_empty(InstalledEmpty);

int main(void) {
    salts_component_context context;

    if (!cmeta_component_desc_valid(cmeta_component_meta(InstalledEmpty)))
        return 1;

    if (salts_component_context_init(
            &context,
            NULL, 0u,
            NULL, 0u,
            NULL, 0u,
            NULL, 0u,
            NULL, 0u) != SALTS_COMPONENT_OK)
        return 2;

    if (salts_component_context_resolve(&context) != SALTS_COMPONENT_OK)
        return 3;
    if (salts_component_context_start(&context) != SALTS_COMPONENT_OK)
        return 4;
    if (salts_component_context_stop(&context) != SALTS_COMPONENT_OK)
        return 5;

    return 0;
}
