#include "fmt.h"

#include <assert.h>
#include <string.h>

static int fmt_contract_case;

int fmt_print(char *buf, size_t size, const char *pattern, const fmt_arg_t *args,
              size_t arg_count) {
  assert(buf != NULL);
  assert(size >= 5U);
  assert(pattern != NULL);

  if (fmt_contract_case == 0) {
    assert(strcmp(pattern, "ready") == 0);
    assert(args == NULL);
    assert(arg_count == 0U);
    memcpy(buf, "ready", 6U);
    return 5;
  }

  assert(fmt_contract_case == 1);
  assert(strcmp(pattern, "{}:{}") == 0);
  assert(args != NULL);
  assert(arg_count == 2U);
  assert(args[0].type == FMT_TYPE_INT);
  assert(args[0].val.i == 7);
  assert(args[1].type == FMT_TYPE_STR);
  assert(strcmp(args[1].val.s, "ok") == 0);
  memcpy(buf, "7:ok", 5U);
  return 4;
}

int main(void) {
  char buf[32];

  fmt_contract_case = 0;
  assert(fmt_text(buf, sizeof(buf), "ready") == 5);
  assert(strcmp(buf, "ready") == 0);

  fmt_contract_case = 1;
  assert(fmt(buf, sizeof(buf), "{}:{}", 7, "ok") == 4);
  assert(strcmp(buf, "7:ok") == 0);
  assert(FMT_ARG_COUNT(7) == 1);
  assert(FMT_ARG_COUNT(7, "ok") == 2);
  return 0;
}
