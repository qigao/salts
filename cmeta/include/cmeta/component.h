#ifndef CMETA_COMPONENT_H
#define CMETA_COMPONENT_H

#include <cmeta/manifest.h>
#include <cmeta/interface.h>

#define CMETA_COMPONENT_DECLARATION_VERSION UINT32_C(1)

typedef enum cmeta_component_role {
    CMETA_COMPONENT_PROVIDES = 1,
    CMETA_COMPONENT_REQUIRES = 2
} cmeta_component_role;

/* Membership only: canonical interface rows remain the semantic authority.
 * No handle, callback, module state or lease is stored in this declaration. */
typedef struct cmeta_component_capability {
    cmeta_component_role role;
    const cmeta_interface_desc *interface_desc;
} cmeta_component_capability;

/* Format 1 is exact-size; metadata is borrowed under the provider's negotiated
 * Reflection ABI and lifetime. name is diagnostic, not runtime Plugin identity.
 * Manual empty tables and repeated/ordered role rows are preserved. */
typedef struct cmeta_component_desc {
    size_t size;
    uint32_t format_version;
    const char *stable_id;
    const cmeta_component_capability *capabilities;
    size_t count;
} cmeta_component_desc;

/* Flat nonempty 1..16 role rows, using the existing PP schema machinery. */
#define CMETA_COMPONENT_ROW_(role_, interface_) \
    {(role_), CMETA_MANIFEST_TYPED_POINTER_(cmeta_interface_desc, interface_)}
#define CMETA_COMPONENT_ROW_APPLY_(row_, ignored_) CMETA_COMPONENT_ROW_ row_
#define CMETA_COMPONENT_DESC_(name_, capabilities_, count_) \
    CMETA_LOCAL const cmeta_component_desc CMETA_PP_CAT(name_,__component_meta) = { \
        sizeof(cmeta_component_desc), CMETA_COMPONENT_DECLARATION_VERSION, \
        CMETA_PP_STRINGIFY(name_), (capabilities_), (count_) \
    }; \
    typedef char CMETA_PP_CAT(name_,__component_declaration_complete)[1]
#define CMETA_COMPONENT_DECLARE_(name_, sentinel_, ...) \
    CMETA_LOCAL const cmeta_component_capability CMETA_PP_CAT(name_,__component_capabilities)[] = { \
        CMETA_PP_MAP_COMMA(CMETA_COMPONENT_ROW_APPLY_, ~, __VA_ARGS__) \
    }; \
    CMETA_COMPONENT_DESC_(name_, CMETA_PP_CAT(name_,__component_capabilities), \
        sizeof(CMETA_PP_CAT(name_,__component_capabilities)) / \
        sizeof(CMETA_PP_CAT(name_,__component_capabilities)[0]))
#define CMETA_COMPONENT_EXPAND_(...) CMETA_COMPONENT_DECLARE_(__VA_ARGS__)
#define cmeta_component(name_, ...) CMETA_COMPONENT_EXPAND_(name_, ~ __VA_ARGS__)
/* Empty membership still emits an ordinary immutable descriptor, not a live
 * Salts::Component instance or a discovery entry. Publication remains explicit. */
#define cmeta_component_empty(name_) CMETA_COMPONENT_DESC_(name_, NULL, 0u)
#define cmeta_provides(interface_) \
    , (CMETA_COMPONENT_PROVIDES, &CMETA_PP_CAT(interface_, _interface_meta))
#define cmeta_requires(interface_) \
    , (CMETA_COMPONENT_REQUIRES, &CMETA_PP_CAT(interface_, _interface_meta))
#define cmeta_component_meta(name_) (&CMETA_PP_CAT(name_, __component_meta))

#endif
