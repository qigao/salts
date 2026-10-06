#include <cmeta/enum.h>

bool cmeta_enum_domain_valid(const cmeta_enum_domain *domain) {
    size_t i;
    uint64_t mask = 0u;
    uint64_t width_mask;
    if (domain == NULL || domain->struct_size < offsetof(cmeta_enum_domain, declared_mask) + sizeof(domain->declared_mask) ||
        domain->abi_version != CMETA_ENUM_DOMAIN_ABI_VERSION ||
        !(domain->bits == 8u || domain->bits == 16u || domain->bits == 32u || domain->bits == 64u) ||
        (domain->signedness != CMETA_ENUM_SIGNED &&
         domain->signedness != CMETA_ENUM_UNSIGNED) ||
        (domain->kind != CMETA_ENUM_ORDINARY && domain->kind != CMETA_ENUM_FLAGS) ||
        (domain->count != 0u && domain->items == NULL))
        return false;
    width_mask = (domain->bits == 64u ? UINT64_MAX : (UINT64_C(1) << domain->bits) - 1u);
    for (i = 0u; i < domain->count; ++i) {
        const cmeta_enum_bits_item *item = &domain->items[i];
        if ((item->bits & ~width_mask) != 0u ||
            item->symbol == NULL || item->symbol[0] == '\0' ||
            item->text == NULL || item->text[0] == '\0')
            return false;
        mask |= item->bits;
    }
    return domain->declared_mask ==
           (domain->kind == CMETA_ENUM_FLAGS ? mask : 0u);
}
