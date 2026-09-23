#ifndef TINYMOCK_INSTALLED_API_H
#define TINYMOCK_INSTALLED_API_H

#include <cmeta/function.h>

typedef int (*tinymock_installed_callback)(int);

static bool tinymock_installed_callback_equal(
    const void *left, const void *right) {
  const tinymock_installed_callback *a =
      (const tinymock_installed_callback *)left;
  const tinymock_installed_callback *b =
      (const tinymock_installed_callback *)right;
  return a && b && *a == *b;
}

static const cmeta_type_traits tinymock_installed_callback_traits = {
  .flags = CMETA_TRAIT_EQUAL |
           CMETA_TRAIT_TRIVIAL_COPY |
           CMETA_TRAIT_TRIVIAL_DESTROY,
  .equal = tinymock_installed_callback_equal
};

static const cmeta_type_desc tinymock_installed_callback_type = {
  .name = "tinymock_installed_callback",
  .size = sizeof(tinymock_installed_callback),
  .align = _Alignof(tinymock_installed_callback),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = &tinymock_installed_callback_traits,
  .identity = NULL
};

typedef struct tinymock_installed_box {
  int value;
} tinymock_installed_box;

static bool tinymock_installed_box_equal(const void *left, const void *right) {
  const tinymock_installed_box *a = (const tinymock_installed_box *)left;
  const tinymock_installed_box *b = (const tinymock_installed_box *)right;
  return a && b && a->value == b->value;
}

static const cmeta_type_traits tinymock_installed_box_traits = {
  .flags = CMETA_TRAIT_EQUAL |
           CMETA_TRAIT_TRIVIAL_COPY |
           CMETA_TRAIT_TRIVIAL_DESTROY,
  .equal = tinymock_installed_box_equal
};

static const cmeta_type_desc tinymock_installed_box_type = {
  .name = "tinymock_installed_box",
  .size = sizeof(tinymock_installed_box),
  .align = _Alignof(tinymock_installed_box),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = &tinymock_installed_box_traits,
  .identity = NULL
};

FunctionDecl(value, int, tinymock_installed_add,
    (int, left, CMETA_PARAM_IN),
    (int, right, CMETA_PARAM_IN));

Function0Decl(value, void, tinymock_installed_shutdown);

FunctionDecl(value, int, tinymock_installed_real,
    (int, value, CMETA_PARAM_IN));

FunctionDeclAsAbi(value, tinymock_installed_box,
                  &tinymock_installed_box_type, CMETA_ABI_AGGREGATE,
                  tinymock_installed_box_copy,
    (tinymock_installed_box, input, CMETA_PARAM_IN,
     &tinymock_installed_box_type, CMETA_ABI_AGGREGATE));

FunctionDeclAsAbi(value, int, &cmeta_type_int, CMETA_ABI_SCALAR,
                  tinymock_installed_apply_callback,
    (tinymock_installed_callback, callback, CMETA_PARAM_IN,
     &tinymock_installed_callback_type, CMETA_ABI_FUNCTION_POINTER),
    (int, value, CMETA_PARAM_IN));

Function0DeclAsAbi(value, tinymock_installed_callback,
                   &tinymock_installed_callback_type,
                   CMETA_ABI_FUNCTION_POINTER,
                   tinymock_installed_callback_answer);

#endif /* TINYMOCK_INSTALLED_API_H */
