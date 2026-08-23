#ifndef CMETA_TYPE_IDENTITY_H
#define CMETA_TYPE_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cmeta_type_form {
    CMETA_TYPE_ATOM,
    CMETA_TYPE_POINTER,
    CMETA_TYPE_CONST,
    CMETA_TYPE_APPLY
} cmeta_type_form;

typedef enum cmeta_generic_category {
    CMETA_GENERIC_VALUE,
    CMETA_GENERIC_CONTAINER,
    CMETA_GENERIC_HANDLE
} cmeta_generic_category;

typedef struct cmeta_generic_desc {
    const char *stable_id;
    const char *display_name;
    uint8_t min_arity;
    uint8_t max_arity;
    cmeta_generic_category category;
} cmeta_generic_desc;

typedef struct cmeta_type_identity cmeta_type_identity;
struct cmeta_type_identity {
    cmeta_type_form form;
    const char *stable_atom_id;
    const cmeta_generic_desc *constructor;
    const cmeta_type_identity *base;
    const cmeta_type_identity *const *args;
    size_t arity;
};

#ifdef __cplusplus
#define CMETA_GENERIC_ARITY(value) static_cast<uint8_t>(value)
#else
#define CMETA_GENERIC_ARITY(value) ((uint8_t)(value))
#endif

#define CMETA_GENERIC_DESC_INIT(id_, display_, min_, max_, category_) \
    { (id_), (display_), CMETA_GENERIC_ARITY(min_), \
      CMETA_GENERIC_ARITY(max_), (category_) }
#define CMETA_TYPE_ID_ATOM_INIT(id_) \
    { CMETA_TYPE_ATOM, (id_), NULL, NULL, NULL, 0u }
#define CMETA_TYPE_ID_POINTER_INIT(base_) \
    { CMETA_TYPE_POINTER, NULL, NULL, (base_), NULL, 0u }
#define CMETA_TYPE_ID_CONST_INIT(base_) \
    { CMETA_TYPE_CONST, NULL, NULL, (base_), NULL, 0u }
#define CMETA_TYPE_ID_APPLY_INIT(constructor_, args_) \
    { CMETA_TYPE_APPLY, NULL, (constructor_), NULL, (args_), \
      sizeof(args_) / sizeof((args_)[0]) }

bool cmeta_generic_desc_valid(const cmeta_generic_desc *desc);
bool cmeta_generic_accepts_arity(const cmeta_generic_desc *desc, size_t arity);
bool cmeta_type_application_valid(
    const cmeta_generic_desc *constructor,
    const cmeta_type_identity *const *args,
    size_t arity);
bool cmeta_type_identity_valid(const cmeta_type_identity *identity);
bool cmeta_type_identity_equal(const cmeta_type_identity *a,
                               const cmeta_type_identity *b);
bool cmeta_type_identity_is_application(const cmeta_type_identity *identity);
const cmeta_generic_desc *
cmeta_type_identity_constructor(const cmeta_type_identity *identity);
size_t cmeta_type_identity_arity(const cmeta_type_identity *identity);
const cmeta_type_identity *
cmeta_type_identity_argument(const cmeta_type_identity *identity, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* CMETA_TYPE_IDENTITY_H */
