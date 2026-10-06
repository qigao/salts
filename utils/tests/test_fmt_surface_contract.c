#include "fmt.h"
#include "tinytest.h"

#include <string.h>

static int fmt_contract_case;

int fmt_print(char *buf, size_t size, const char *pattern,
              const fmt_arg_t *args, size_t arg_count) {
  if (buf == NULL || pattern == NULL) return -1;

  if (fmt_contract_case == 0) {
    if (size < sizeof("ready") || strcmp(pattern, "ready") != 0 ||
        args != NULL || arg_count != 0U)
      return -1;
    memcpy(buf, "ready", sizeof("ready"));
    return 5;
  }

  if (size < sizeof("7:ok") || fmt_contract_case != 1 || strcmp(pattern, "{}:{}") != 0 ||
      args == NULL || arg_count != 2U || args[0].type != FMT_TYPE_INT ||
      args[0].val.i != 7 || args[1].type != FMT_TYPE_STR ||
      strcmp(args[1].val.s, "ok") != 0)
    return -1;

  memcpy(buf, "7:ok", sizeof("7:ok"));
  return 4;
}

suite("fmt C surface contract") {
  before_each() { fmt_contract_case = 0; }

  group("public macro expansion") {
    it("passes literal text without arguments") {
      char buf[sizeof("ready")] = {0};
      check_equal(fmt_text(buf, sizeof(buf), "ready"), 5);
      check_equal(buf, "ready");
    }

    it("passes typed integer and string arguments") {
      fmt_contract_case = 1;
      char buf[sizeof("7:ok")] = {0};
      const int written = fmt(buf, sizeof(buf), "{}:{}", 7, "ok");
      check_equal(written, 4);
      check_equal(buf, "7:ok");
    }

    it("counts one and two arguments") {
      check_equal(FMT_ARG_COUNT(7), 1);
      check_equal(FMT_ARG_COUNT(7, "ok"), 2);
    }
  }
}
