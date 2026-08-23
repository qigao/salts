#ifndef CMETA_DECLARED_TYPE_H
#define CMETA_DECLARED_TYPE_H

#include <cmeta/cmeta.h>
#include <cmeta/pp.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cmeta_container_construct_ops;

typedef struct cmeta_declared_type {
    const cmeta_type_desc *storage_type;
    const cmeta_generic_desc *constructor;
    const cmeta_type_desc *const *arguments;
    size_t arity;
    const struct cmeta_container_construct_ops *construction;
} cmeta_declared_type;

bool cmeta_declared_type_valid(const cmeta_declared_type *declared);
bool cmeta_declared_type_constructible(const cmeta_declared_type *declared);
const cmeta_type_desc *cmeta_declared_type_argument(
    const cmeta_declared_type *declared, size_t index);

#ifdef __cplusplus
}
#endif

/* TYPE(...) is a schema/type-position token. It is lowered by Struct through
 * provider macros; it is not a generated C typedef and allocates nothing. */
#define CMETA_TYPE_SPEC_MARKER_PROBE_CMETA_TYPE_SPEC_MARKER CMETA_PP_PROBE()
#define CMETA_TYPE_SPEC_TAG_VALID(tag) \
    CMETA_PP_IS_PROBE(CMETA_PP_CAT(CMETA_TYPE_SPEC_MARKER_PROBE_, tag))
#define CMETA_TYPE_SPEC_IS_PAREN_I(tag, ...) CMETA_TYPE_SPEC_TAG_VALID(tag)
#define CMETA_TYPE_SPEC_IS_PAREN(spec) CMETA_TYPE_SPEC_IS_PAREN_I spec
#define CMETA_TYPE_SPEC_IS_0(spec) 0
#define CMETA_TYPE_SPEC_IS_1(spec) CMETA_TYPE_SPEC_IS_PAREN(spec)
#define CMETA_TYPE_SPEC_IS_I(is_paren, spec) \
    CMETA_PP_CAT(CMETA_TYPE_SPEC_IS_, is_paren)(spec)
#define CMETA_TYPE_SPEC_IS(spec) \
    CMETA_TYPE_SPEC_IS_I(CMETA_PP_IS_PAREN(spec), spec)

#define CMETA_TYPE_SPEC(kind, ...) \
    (CMETA_TYPE_SPEC_MARKER, kind, __VA_ARGS__)
#ifndef TYPE
#define TYPE(kind, ...) CMETA_TYPE_SPEC(kind, __VA_ARGS__)
#endif

#define CMETA_TYPE_SPEC_STORAGE_I(tag, kind, ...) \
    CMETA_PP_CAT(CMETA_DECLARED_STORAGE_, kind)
#define CMETA_TYPE_SPEC_STORAGE(spec) CMETA_TYPE_SPEC_STORAGE_I spec
#define CMETA_TYPE_SPEC_STORAGE_DESC_I(tag, kind, ...) \
    CMETA_PP_CAT(CMETA_DECLARED_STORAGE_DESC_, kind)
#define CMETA_TYPE_SPEC_STORAGE_DESC(spec) CMETA_TYPE_SPEC_STORAGE_DESC_I spec
#define CMETA_TYPE_SPEC_CONSTRUCTOR_I(tag, kind, ...) \
    CMETA_PP_CAT(CMETA_DECLARED_CONSTRUCTOR_, kind)
#define CMETA_TYPE_SPEC_CONSTRUCTOR(spec) CMETA_TYPE_SPEC_CONSTRUCTOR_I spec
#define CMETA_TYPE_SPEC_CONSTRUCTION_I(tag, kind, ...) \
    CMETA_PP_CAT(CMETA_DECLARED_CONSTRUCTION_, kind)
#define CMETA_TYPE_SPEC_CONSTRUCTION(spec) CMETA_TYPE_SPEC_CONSTRUCTION_I spec

#endif /* CMETA_DECLARED_TYPE_H */
