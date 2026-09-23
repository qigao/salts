#ifndef TINYMOCK_FUNCTION_FIXTURE_H
#define TINYMOCK_FUNCTION_FIXTURE_H

#include <cmeta/function.h>

typedef struct tinymock_fixture_box {
  int value;
} tinymock_fixture_box;

static bool tinymock_fixture_box_equal(const void *left, const void *right) {
  const tinymock_fixture_box *a = (const tinymock_fixture_box *)left;
  const tinymock_fixture_box *b = (const tinymock_fixture_box *)right;
  return a && b && a->value == b->value;
}

static const cmeta_type_traits tinymock_fixture_box_traits = {
  .flags = CMETA_TRAIT_EQUAL |
           CMETA_TRAIT_TRIVIAL_COPY |
           CMETA_TRAIT_TRIVIAL_DESTROY,
  .equal = tinymock_fixture_box_equal
};

static const cmeta_type_desc tinymock_fixture_box_type = {
  .name = "tinymock_fixture_box",
  .size = sizeof(tinymock_fixture_box),
  .align = _Alignof(tinymock_fixture_box),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = &tinymock_fixture_box_traits,
  .identity = NULL
};

FunctionDecl(value, int, tinymock_fixture_add,
    (int, left, CMETA_PARAM_IN),
    (int, right, CMETA_PARAM_IN));

Function0Decl(value, int, tinymock_fixture_answer);

FunctionDecl(value, int, tinymock_fixture_pointer,
    (int *, value, CMETA_PARAM_IN, &cmeta_type_int_ptr, CMETA_ABI_OBJECT_POINTER));

FunctionDecl(value, int, tinymock_fixture_write_size,
    (int, input, CMETA_PARAM_IN),
    (size_t *, written, CMETA_PARAM_OUT, &cmeta_type_size_ptr, CMETA_ABI_OBJECT_POINTER));

FunctionDecl(value, int, tinymock_fixture_adjust_int,
    (int *, value, CMETA_PARAM_INOUT, &cmeta_type_int_ptr, CMETA_ABI_OBJECT_POINTER));

FunctionDecl(value, int, tinymock_fixture_unknown_ptr,
    (int *, value, CMETA_PARAM_UNKNOWN, &cmeta_type_int_ptr, CMETA_ABI_OBJECT_POINTER));

FunctionDecl(value, int, tinymock_fixture_nullable_out,
    (size_t *, written, CMETA_PARAM_OUT | CMETA_PARAM_NULLABLE,
     &cmeta_type_size_ptr, CMETA_ABI_OBJECT_POINTER));

FunctionDecl(value, void, tinymock_fixture_notify,
    (int, event, CMETA_PARAM_IN),
    (size_t *, written, CMETA_PARAM_OUT, &cmeta_type_size_ptr, CMETA_ABI_OBJECT_POINTER));

Function0Decl(value, void, tinymock_fixture_shutdown);

FunctionDeclAsAbi(value, tinymock_fixture_box,
                  &tinymock_fixture_box_type, CMETA_ABI_AGGREGATE,
                  tinymock_fixture_box_copy,
    (tinymock_fixture_box, input, CMETA_PARAM_IN,
     &tinymock_fixture_box_type, CMETA_ABI_AGGREGATE));

Function0DeclAsAbi(value, int *, &cmeta_type_int_ptr,
                   CMETA_ABI_OBJECT_POINTER,
                   tinymock_fixture_pointer_answer);

#endif /* TINYMOCK_FUNCTION_FIXTURE_H */
