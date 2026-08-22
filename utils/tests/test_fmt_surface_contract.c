#include "fmt.h"

#include <string.h>

static int fmt_contract_case;

int fmt_print(char *buf, size_t size, const char *pattern, const fmt_arg_t *args,
              size_t arg_count) {
  if (buf == NULL || size < 5U || pattern == NULL) return -1;

  if (fmt_contract_case == 0) {
    if (strcmp(pattern, "ready") != 0 || args != NULL || arg_count != 0U)
      return -1;
    memcpy(buf, "ready", 6U);
    return 5;
  }

  if (fmt_contract_case != 1 || strcmp(pattern, "{}:{}") != 0 ||
      args == NULL || arg_count != 2U || args[0].type != FMT_TYPE_INT ||
      args[0].val.i != 7 || args[1].type != FMT_TYPE_STR ||
      strcmp(args[1].val.s, "ok") != 0)
    return -1;

  memcpy(buf, "7:ok", 5U);
  return 4;
}

int main(void) {
  char buf[32];

  fmt_contract_case = 0;
  if (fmt_text(buf, sizeof(buf), "ready") != 5 || strcmp(buf, "ready") != 0)
    return 1;

  fmt_contract_case = 1;
  if (fmt(buf, sizeof(buf), "{}:{}", 7, "ok") != 4 || strcmp(buf, "7:ok") != 0)
    return 1;
  if (FMT_ARG_COUNT(7) != 1 || FMT_ARG_COUNT(7, "ok") != 2)
    return 1;
  return 0;
}
