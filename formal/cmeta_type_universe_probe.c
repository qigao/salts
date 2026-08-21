typedef struct known_only_type {
    int value;
} known_only_type;

#define CMETA_SIGNATURE_PROFILE_FULL
#define CMETA_KNOWN_TYPE_LIST \
    CMETA_BUILTIN_TYPE_LIST, \
    (K, known_only_type, cmeta_type_known_only, CMETA_T_OBJECT)
#define CMETA_CALLABLE_TYPE_LIST CMETA_BUILTIN_TYPE_LIST

#include <cmeta/cmeta.h>

_Static_assert(CMETA_PP_NARG(CMETA_KNOWN_TYPE_LIST) == 6,
               "known type list must include the extra reflected type");
_Static_assert(CMETA_PP_NARG(CMETA_CALLABLE_TYPE_LIST) == 5,
               "callable list must remain the five builtins");
_Static_assert(CMETA_SIG_COUNT == 176,
               "five callable types in full profile must remain 25+125+25 signatures plus invalid");

static const cmeta_type_desc *const known_probe = &cmeta_type_known_only;

const cmeta_type_desc cmeta_type_known_only = {
    .name = "known_only_type",
    .size = sizeof(known_only_type),
    .align = _Alignof(known_only_type),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .identity = NULL
};

int main(void) {
    return known_probe == &cmeta_type_known_only ? 0 : 1;
}
