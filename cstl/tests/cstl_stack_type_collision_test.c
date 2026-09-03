#include <stddef.h>

#ifdef __APPLE__
  #include <signal.h>
#else
typedef struct platform_stack {
  void *base;
  size_t size;
  int flags;
} stack_t;
#endif

#define CSTL_NO_LEGACY_STACK_T 1
#include <cstl.h>
#include "tinytest.h"

spec("CSTL stack type compatibility") {
  it("keeps the canonical stack handle distinct from a platform stack_t") {
    cstl_stack_t stack = StackOf(int);
    int input = 9;
    int output = 0;

    check_equal(stack_init(&stack, 1u), STL_OK);
    check_equal(stack_push(&stack, &input), STL_OK);
    check_equal(stack_pop(&stack, &output), STL_OK);
    check_equal(output, input);
    stack_destroy(&stack);
  }
}
