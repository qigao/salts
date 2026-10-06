#ifndef SALTS_PLUGIN_LINKER_H
#define SALTS_PLUGIN_LINKER_H

#include <salts/plugin_decl.h>

/* One explicit discovery set per DSO. Link fragments as objects (or explicitly
 * extract their archive members). The checked count makes omission fail closed.
 * Rows are immutable after DSO initialization; their order is linker-defined. */
#define SALTS_PLUGIN_HAS_LINKER_EXPORTS (CMETA_SECTION_BACKEND != CMETA_SECTION_NONE)

#if SALTS_PLUGIN_HAS_LINKER_EXPORTS
#define SALTS_PLUGIN_LINKER_ALIGNMENT 8
#ifdef __cplusplus
#define SALTS_PLUGIN_LINKER_EXTERN extern "C"
#else
#define SALTS_PLUGIN_LINKER_EXTERN extern
#endif

#if CMETA_SECTION_BACKEND == CMETA_SECTION_COFF
#define SALTS_PLUGIN_LINKER_STORAGE CMETA_ATTR_SECTION(".sltexp$m")
#define SALTS_PLUGIN_LINKER_KEEP(name) CMETA_COFF_RETAIN(CMETA_PP_STRINGIFY(name))
#define SALTS_PLUGIN_LINKER_LOCAL
/* Full rows keep sentinel size/alignment identical to every fragment. */
#define SALTS_PLUGIN_LINKER_BOUNDS \
    SALTS_PLUGIN_LINKER_EXTERN const salts_plugin_export salts_plugin_linker_begin; \
    SALTS_PLUGIN_LINKER_EXTERN const salts_plugin_export salts_plugin_linker_end; \
    CMETA_ATTR_SECTION(".sltexp$a") CMETA_ATTR_ALIGNED(SALTS_PLUGIN_LINKER_ALIGNMENT) \
    const salts_plugin_export salts_plugin_linker_begin = {0}; \
    CMETA_ATTR_SECTION(".sltexp$z") CMETA_ATTR_ALIGNED(SALTS_PLUGIN_LINKER_ALIGNMENT) \
    const salts_plugin_export salts_plugin_linker_end = {0}; \
    SALTS_PLUGIN_LINKER_KEEP(salts_plugin_linker_begin) \
    SALTS_PLUGIN_LINKER_KEEP(salts_plugin_linker_end)
#define SALTS_PLUGIN_LINKER_FIRST (&salts_plugin_linker_begin + 1)
#define SALTS_PLUGIN_LINKER_LAST (&salts_plugin_linker_end)
#elif CMETA_SECTION_BACKEND == CMETA_SECTION_ELF
#define SALTS_PLUGIN_LINKER_STORAGE CMETA_ATTR_SECTION("salts_exports") CMETA_ATTR_USED CMETA_ATTR_RETAIN
#define SALTS_PLUGIN_LINKER_KEEP(name)
#define SALTS_PLUGIN_LINKER_LOCAL CMETA_ATTR_HIDDEN
#define SALTS_PLUGIN_LINKER_BOUNDS \
    SALTS_PLUGIN_LINKER_EXTERN const salts_plugin_export __start_salts_exports[] CMETA_ATTR_HIDDEN; \
    SALTS_PLUGIN_LINKER_EXTERN const salts_plugin_export __stop_salts_exports[] CMETA_ATTR_HIDDEN;
#define SALTS_PLUGIN_LINKER_FIRST __start_salts_exports
#define SALTS_PLUGIN_LINKER_LAST __stop_salts_exports
#elif CMETA_SECTION_BACKEND == CMETA_SECTION_MACHO
#define SALTS_PLUGIN_LINKER_STORAGE CMETA_ATTR_SECTION("__DATA,salts_exports") CMETA_ATTR_USED
#define SALTS_PLUGIN_LINKER_KEEP(name)
#define SALTS_PLUGIN_LINKER_LOCAL CMETA_ATTR_HIDDEN
#define SALTS_PLUGIN_LINKER_BOUNDS \
    SALTS_PLUGIN_LINKER_EXTERN const salts_plugin_export salts_plugin_linker_begin[] \
        __asm__("section$start$__DATA$salts_exports"); \
    SALTS_PLUGIN_LINKER_EXTERN const salts_plugin_export salts_plugin_linker_end[] \
        __asm__("section$end$__DATA$salts_exports");
#define SALTS_PLUGIN_LINKER_FIRST salts_plugin_linker_begin
#define SALTS_PLUGIN_LINKER_LAST salts_plugin_linker_end
#endif

/* The section alignment must divide every row size. Fixed 8-byte alignment
 * supports the current 32/64-bit ABI; other layouts are rejected at compile time. */
CMETA_STATIC_ASSERT(CMETA_ALIGNOF(salts_plugin_export) <= SALTS_PLUGIN_LINKER_ALIGNMENT &&
    sizeof(salts_plugin_export) % SALTS_PLUGIN_LINKER_ALIGNMENT == 0,
    "Plugin linker row alignment unsupported");

#define SALTS_PLUGIN_EXPORT_FRAGMENT(name,exports) \
    SALTS_PLUGIN_EXPORT_FRAGMENT_I(name,exports)
#define SALTS_PLUGIN_EXPORT_FRAGMENT_I(name,exports) \
    exports(SALTS_PLUGIN_DECL_CHECK) \
    exports(SALTS_PLUGIN_DECL_BRIDGE) \
    SALTS_PLUGIN_LINKER_EXTERN const salts_plugin_export name[] SALTS_PLUGIN_LINKER_LOCAL; \
    SALTS_PLUGIN_LINKER_STORAGE CMETA_ATTR_ALIGNED(SALTS_PLUGIN_LINKER_ALIGNMENT) \
    const salts_plugin_export name[] = { exports(SALTS_PLUGIN_DECL_ROW) }; \
    SALTS_PLUGIN_LINKER_KEEP(name) \
    CMETA_STATIC_ASSERT(sizeof(name)/sizeof(name[0]) > 0 && \
        sizeof(name)/sizeof(name[0]) <= SALTS_PLUGIN_MAX_EXPORTS, \
        "Plugin fragment count must be within capacity")

#define SALTS_PLUGIN_DECLARE_LINKER(name,id,version,count,lifecycle) \
    SALTS_PLUGIN_DECLARE_LINKER_I(name,id,version,count,lifecycle)
#define SALTS_PLUGIN_DECLARE_LINKER_I(name,id,version,count,lifecycle) \
    CMETA_STATIC_ASSERT((count) > 0 && (count) <= SALTS_PLUGIN_MAX_EXPORTS && \
        (((count) & UINT32_MAX) == (count)), \
        "Plugin linker count must be an integer within capacity"); \
    SALTS_PLUGIN_LINKER_BOUNDS \
    CMETA_PP_TUPLE_APPLY(SALTS_PLUGIN_DECL_VERSION,version) \
    CMETA_PP_TUPLE_APPLY(SALTS_PLUGIN_DECL_LIFE_CHECK_I,lifecycle) \
    static const salts_plugin_manifest name##__manifest = { \
        SALTS_PLUGIN_MANIFEST_SIZE,SALTS_PLUGIN_ABI_VERSION,(id), \
        {CMETA_PP_UNPAREN version},SALTS_PLUGIN_LINKER_FIRST,(count), \
        CMETA_PP_TUPLE_APPLY(SALTS_PLUGIN_DECL_LIFE_I,lifecycle) \
    }; \
    SALTS_PLUGIN_QUERY_EXPORT const salts_plugin_manifest *SALTS_PLUGIN_CALL \
    salts_plugin_query(uint32_t host_abi) { \
        const uintptr_t first = (uintptr_t)(const void *)SALTS_PLUGIN_LINKER_FIRST; \
        const uintptr_t last = (uintptr_t)(const void *)SALTS_PLUGIN_LINKER_LAST; \
        if (host_abi != SALTS_PLUGIN_ABI_VERSION || last < first || \
            last - first != (count) * sizeof(salts_plugin_export)) return NULL; \
        return &name##__manifest; \
    } \
    typedef char name##__plugin_linker_declaration_complete[1]
#endif

#endif
