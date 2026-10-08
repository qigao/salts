#include <cmeta/component.h>

#include <stdint.h>

bool cmeta_component_desc_valid(const cmeta_component_desc *desc) {
    size_t i;
    size_t j;

    if (desc == NULL || desc->size != sizeof(*desc) ||
        desc->format_version != CMETA_COMPONENT_DECLARATION_VERSION ||
        desc->stable_id == NULL || desc->stable_id[0] == '\0')
        return false;

    /* A Component config is a concrete native value, not a kind-only
     * container contract. Preserve kind-only DataDesc for other consumers. */
    if (desc->config != NULL &&
        (!cmeta_data_desc_valid(desc->config) ||
         desc->config->storage_type == NULL ||
         !cmeta_type_desc_valid(desc->config->storage_type)))
        return false;

    if ((desc->capability_count != 0u && desc->capabilities == NULL) ||
        desc->capability_count > SIZE_MAX / sizeof(*desc->capabilities))
        return false;

    for (i = 0u; i < desc->capability_count; ++i) {
        const cmeta_component_capability *row = &desc->capabilities[i];

        if ((row->role != CMETA_COMPONENT_PROVIDES &&
             row->role != CMETA_COMPONENT_REQUIRES) ||
            !cmeta_interface_desc_valid(row->interface_desc))
            return false;

        for (j = 0u; j < i; ++j) {
            const cmeta_component_capability *previous = &desc->capabilities[j];
            if (previous->role == row->role &&
                cmeta_interface_desc_equal(
                    previous->interface_desc, row->interface_desc))
                return false;
        }
    }

    return true;
}
