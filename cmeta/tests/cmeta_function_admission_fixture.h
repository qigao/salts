#ifndef CMETA_FUNCTION_ADMISSION_FIXTURE_H
#define CMETA_FUNCTION_ADMISSION_FIXTURE_H

typedef struct cmeta_admission_box { int value; } cmeta_admission_box;
typedef int *cmeta_admission_pointer;
extern const struct cmeta_type_desc cmeta_admission_box_type;

/* Register reflection-only types without changing the callable ABI. */
#define CMETA_CALLABLE_TYPE_LIST CMETA_BUILTIN_TYPE_LIST
#define CMETA_KNOWN_TYPE_LIST CMETA_BUILTIN_TYPE_LIST, \
    (AdmissionPointer, cmeta_admission_pointer, cmeta_type_int_ptr, CMETA_T_POINTER, cmeta_traits_int), \
    (AdmissionBox, cmeta_admission_box, cmeta_admission_box_type, CMETA_T_OBJECT, cmeta_traits_int)

#include <cmeta/function.h>

const cmeta_type_desc cmeta_admission_box_type = {
    .name = "cmeta_admission_box",
    .size = sizeof(cmeta_admission_box),
    .align = _Alignof(cmeta_admission_box),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = NULL
};

#endif
