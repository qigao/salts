#ifndef CMETA_PLUGIN_H
#define CMETA_PLUGIN_H

#include <cmeta/manifest.h>
#include <cmeta/interface.h>

#define CMETA_PLUGIN_DECLARATION_VERSION UINT32_C(1)

typedef enum cmeta_plugin_role {
    CMETA_PLUGIN_PROVIDES = 1,
    CMETA_PLUGIN_REQUIRES = 2
} cmeta_plugin_role;

/* Membership only: canonical interface rows remain the semantic authority.
 * No handle, callback, module state or lease is stored in this declaration. */
typedef struct cmeta_plugin_capability {
    cmeta_plugin_role role;
    const cmeta_interface_desc *interface_desc;
} cmeta_plugin_capability;

/* Format 1 is exact-size; metadata is borrowed under the provider's negotiated
 * Reflection ABI and lifetime. name is diagnostic, not runtime Plugin identity.
 * Manual empty tables and repeated/ordered role rows are preserved. */
typedef struct cmeta_plugin_desc {
    size_t size;
    uint32_t format_version;
    const char *name;
    const cmeta_plugin_capability *capabilities;
    size_t count;
} cmeta_plugin_desc;

/* Flat nonempty 1..16 role rows, using the existing PP schema machinery. */
#define CMETA_PLUGIN_ROW_(role_, interface_) \
    {(role_), CMETA_MANIFEST_TYPED_POINTER_(cmeta_interface_desc, interface_)}
#define CMETA_PLUGIN_ROW_APPLY_(row_, ignored_) CMETA_PLUGIN_ROW_ row_
#define CMETA_PLUGIN_DESC_(name_, capabilities_, count_) \
    CMETA_LOCAL const cmeta_plugin_desc CMETA_PP_CAT(name_,__plugin_meta) = { \
        sizeof(cmeta_plugin_desc), CMETA_PLUGIN_DECLARATION_VERSION, \
        CMETA_PP_STRINGIFY(name_), (capabilities_), (count_) \
    }; \
    typedef char CMETA_PP_CAT(name_,__plugin_declaration_complete)[1]
#define CMETA_PLUGIN_DECLARE_(name_, sentinel_, ...) \
    CMETA_LOCAL const cmeta_plugin_capability CMETA_PP_CAT(name_,__plugin_capabilities)[] = { \
        CMETA_PP_MAP_COMMA(CMETA_PLUGIN_ROW_APPLY_, ~, __VA_ARGS__) \
    }; \
    CMETA_PLUGIN_DESC_(name_, CMETA_PP_CAT(name_,__plugin_capabilities), \
        sizeof(CMETA_PP_CAT(name_,__plugin_capabilities)) / \
        sizeof(CMETA_PP_CAT(name_,__plugin_capabilities)[0]))
#define CMETA_PLUGIN_EXPAND_(...) CMETA_PLUGIN_DECLARE_(__VA_ARGS__)
#define cmeta_plugin(name_, ...) CMETA_PLUGIN_EXPAND_(name_, ~ __VA_ARGS__)
/* Empty membership still emits an ordinary immutable descriptor, not a live
 * Salts::Plugin instance or a discovery entry. Publication remains explicit. */
#define cmeta_plugin_empty(name_) CMETA_PLUGIN_DESC_(name_, NULL, 0u)
#define cmeta_provides(interface_) \
    , (CMETA_PLUGIN_PROVIDES, &CMETA_PP_CAT(interface_, _interface_meta))
#define cmeta_requires(interface_) \
    , (CMETA_PLUGIN_REQUIRES, &CMETA_PP_CAT(interface_, _interface_meta))
#define cmeta_plugin_meta(name_) (&CMETA_PP_CAT(name_, __plugin_meta))

#endif
