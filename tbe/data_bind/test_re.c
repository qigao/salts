#include "re.h"
#include "tinytest.h"

static void check_match_at(const char *pattern, const char *text, int expected_idx,
                           int expected_len) {
  int len = -1;
  int idx = re_match(pattern, text, &len);
  check_int_eq(idx, expected_idx);
  check_int_eq(len, expected_len);
}

static void check_no_match(const char *pattern, const char *text) {
  int len = -1;
  int idx = re_match(pattern, text, &len);
  check_int_eq(idx, -1);
  check_int_eq(len, 0);
}

suite("bounded regex") {
  group("matching") {
    it("matches inverted character classes") {
      check_match_at("^[^abc]+$", "xyz", 0, 3);
      check_no_match("^[^abc]+$", "xay");
      check_match_at("^[^0-9]+$", "abc", 0, 3);
      check_no_match("^[^0-9]+$", "a7c");
      check_match_at("^[^\\d]+$", "abc", 0, 3);
      check_no_match("^[^\\d]+$", "abc7");
    }

    it("matches branches without leaking group scope") {
      check_match_at("foo|bar", "xxbarzz", 2, 3);
      check_match_at("foo|bar", "foo", 0, 3);
      check_match_at("^(cat|dog)$", "dog", 0, 3);
      check_no_match("^(cat|dog)$", "cow");
    }

    it("matches quantified groups and backtracks for a suffix") {
      check_match_at("^(a|b)+$", "abba", 0, 4);
      check_no_match("^(a|b)+$", "abbc");
      check_match_at("^(a|b)+c$", "abbc", 0, 4);
    }

    it("returns zero-length matches") {
      check_match_at("$", "a", 1, 0);
      check_match_at("a*", "", 0, 0);
      check_match_at("", "", 0, 0);
    }

    it("matches byte slices containing NUL") {
      static const char text[] = {'a', '\0', 'b'};
      re_match_result_t match = {0};
      check_int_eq(re_match_n("a.b", 3, text, sizeof(text), NULL, &match), RE_STATUS_OK);
      check_size_eq(match.index, 0);
      check_size_eq(match.length, 3);
    }
  }

  group("compiled handles") {
    it("keeps separately compiled patterns independent") {
      re_t first = NULL;
      re_t second = NULL;
      re_match_result_t match = {0};

      check_int_eq(re_compile_n("foo", 3, NULL, &first), RE_STATUS_OK);
      check_int_eq(re_compile_n("bar", 3, NULL, &second), RE_STATUS_OK);
      check_not_null(first);
      check_not_null(second);
      check_int_eq(re_matchn(first, "xxfoo", 5, NULL, &match), RE_STATUS_OK);
      check_size_eq(match.index, 2);
      check_int_eq(re_matchn(second, "xxbar", 5, NULL, &match), RE_STATUS_OK);
      check_size_eq(match.index, 2);
      check_int_eq(re_matchn(first, "bar", 3, NULL, &match), RE_STATUS_NO_MATCH);

      re_destroy(second);
      re_destroy(first);
    }

    it("rejects malformed patterns without returning a handle") {
      re_t pattern = (re_t)(uintptr_t)1;
      check_int_eq(re_compile_n("(a|b", 4, NULL, &pattern), RE_STATUS_INVALID_PATTERN);
      check_null(pattern);
      check_null(re_compile("*a"));
      check_null(re_compile("a**"));
    }
  }

  group("resource limits") {
    it("reports pattern and text limits separately") {
      re_limits_t limits = re_limits_default();
      re_match_result_t match = {0};
      limits.max_pattern_bytes = 2;
      check_int_eq(re_validate_n("abc", 3, &limits), RE_STATUS_PATTERN_LIMIT);

      limits = re_limits_default();
      limits.max_text_bytes = 2;
      check_int_eq(re_match_n("a", 1, "abc", 3, &limits, &match), RE_STATUS_TEXT_LIMIT);
      check_size_eq(match.index, RE_NPOS);
    }

    it("stops when the recursion depth budget is exhausted") {
      re_limits_t limits = re_limits_default();
      limits.max_depth = 2;
      check_int_eq(re_validate_n("((a))", 5, &limits), RE_STATUS_DEPTH_LIMIT);
    }

    it("stops when the execution step budget is exhausted") {
      re_limits_t limits = re_limits_default();
      re_match_result_t match = {0};
      limits.max_steps = 1;
      check_int_eq(re_match_n("a", 1, "a", 1, &limits, &match), RE_STATUS_STEP_LIMIT);
      check_size_eq(match.index, RE_NPOS);
    }

    it("stops before allocating beyond the workspace budget") {
      re_limits_t limits = re_limits_default();
      re_match_result_t match = {0};
      limits.max_workspace_bytes = (uint32_t)(sizeof(size_t) * 2);
      check_int_eq(re_match_n("(a*)*", 5, "a", 1, &limits, &match), RE_STATUS_WORKSPACE_LIMIT);
      check_size_eq(match.index, RE_NPOS);
    }

    it("rejects malformed limit structures") {
      re_limits_t limits = re_limits_default();
      limits.max_steps = 0;
      check_int_eq(re_validate_n("a", 1, &limits), RE_STATUS_INVALID_ARGUMENT);
      limits = re_limits_default();
      limits.struct_size = sizeof(limits) - 1;
      check_int_eq(re_validate_n("a", 1, &limits), RE_STATUS_INVALID_ARGUMENT);
    }
  }

  group("status strings") {
    it("returns stable diagnostics") {
      check_str_eq(re_status_string(RE_STATUS_STEP_LIMIT), "step limit exceeded");
      check_str_eq(re_status_string((re_status_t)99), "unknown regex status");
    }
  }
}
