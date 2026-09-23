#ifndef TINYMOCK_REFLECTED_DEPENDENCY_H
#define TINYMOCK_REFLECTED_DEPENDENCY_H

#include <cmeta/function.h>

typedef struct tinymock_reflected_box {
  int value;
} tinymock_reflected_box;

static const cmeta_type_desc tinymock_reflected_box_type = {
    .name = "tinymock_reflected_box",
    .size = sizeof(tinymock_reflected_box),
    .align = _Alignof(tinymock_reflected_box),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_type_desc tinymock_reflected_box_ptr_type = {
    .name = "tinymock_reflected_box *",
    .size = sizeof(tinymock_reflected_box *),
    .align = _Alignof(tinymock_reflected_box *),
    .kind = CMETA_T_POINTER,
    .pointee = &tinymock_reflected_box_type,
    .traits = NULL,
    .identity = NULL
};

FunctionDecl(value, int, tinymock_reflected_add,
    (int, left, CMETA_PARAM_IN),
    (int, right, CMETA_PARAM_IN));

Function0Decl(value, int, tinymock_reflected_status);

FunctionDeclAs(value, int, &cmeta_type_int, tinymock_reflected_read_box,
    (tinymock_reflected_box *, box,
     CMETA_PARAM_IN | CMETA_PARAM_BORROWED,
     &tinymock_reflected_box_ptr_type));

Function0DeclAs(value, int, &cmeta_type_int,
                tinymock_reflected_explicit_status);

#endif /* TINYMOCK_REFLECTED_DEPENDENCY_H */
