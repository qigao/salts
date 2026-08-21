#define TINYTEST_NO_MAIN
#include "tinymock.h"
#include <stdio.h>

TINYMOCk_MOCK(int, repro, int, int)

int main() {
  mock_repro_reset();
  mock_repro_expect(TINYMOCk_ARG(2), TINYMOCk_ARG(3), TINYMOCk_RETURN(5));

  printf("before call ret=%d\n", repro(2,3));
  printf("cursor=%zu expected=%zu\n", tinymock_repro.cursor, tinymock_repro.expected_count);
  printf("arg0 after call: %lld\n",
         tinymock_repro.expectations[0].args[0].value.as.signed_value);
  return 0;
}
