#include "ac_automaton.h"
#include "levenshtein_automaton.h"
#include "tinytest.h"
#include "turbo_str.h"
#include "turbo_vstr.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t id;
  size_t start;
  size_t end;
  size_t distance;
} match_record_t;

typedef struct {
  match_record_t entries[32];
  size_t count;
} match_record_list_t;

static bool capture_ac_match(uint32_t pattern_id, size_t start, size_t end, void *user_data) {
  match_record_list_t *records = (match_record_list_t *)user_data;
  if (records->count < sizeof(records->entries) / sizeof(records->entries[0])) {
    records->entries[records->count].id = pattern_id;
    records->entries[records->count].start = start;
    records->entries[records->count].end = end;
    records->entries[records->count].distance = 0;
    ++records->count;
  }
  return true;
}

static bool capture_lev_match(size_t start, size_t end, size_t distance, void *user_data) {
  match_record_list_t *records = (match_record_list_t *)user_data;
  if (records->count < sizeof(records->entries) / sizeof(records->entries[0])) {
    records->entries[records->count].start = start;
    records->entries[records->count].end = end;
    records->entries[records->count].distance = distance;
    records->entries[records->count].id = 0;
    ++records->count;
  }
  return true;
}

static bool have_record(const match_record_list_t *records, uint32_t id, size_t start, size_t end,
                       size_t distance) {
  for (size_t i = 0; i < records->count; ++i) {
    if (records->entries[i].id == id && records->entries[i].start == start &&
        records->entries[i].end == end &&
        (distance == (size_t)-1 || records->entries[i].distance == distance)) {
      return true;
    }
  }
  return false;
}

static bool stop_after_one_lev(size_t start, size_t end, size_t distance, void *user_data_unused) {
  match_record_list_t *records = (match_record_list_t *)user_data_unused;
  (void)start;
  (void)end;
  (void)distance;
  if (records->count < sizeof(records->entries) / sizeof(records->entries[0])) {
    records->entries[records->count].id = 0;
    records->entries[records->count].start = start;
    records->entries[records->count].end = end;
    records->entries[records->count].distance = distance;
    ++records->count;
  }
  return false;
}

static bool stop_after_one_ac(uint32_t pattern_id, size_t start, size_t end, void *user_data_unused) {
  (void)pattern_id;
  (void)start;
  (void)end;
  (void)user_data_unused;
  return false;
}

spec("AC 自动机") {
  it("在字节文本中进行多模式匹配") {
    ac_automaton_t *ac = ac_automaton_create();
    uint32_t pid_aba = 0, pid_ba = 0, pid_bab = 0;
    vstr text = vstr_from_cstr("ababa");
    match_record_list_t matches = {0};
    int rc;
    check_not_null(ac);
    check_equal(ac_automaton_add_pattern(ac, vstr_from_cstr("aba"), &pid_aba), TURBO_OK);
    check_equal(ac_automaton_add_pattern(ac, vstr_from_cstr("ba"), &pid_ba), TURBO_OK);
    check_equal(ac_automaton_add_pattern(ac, vstr_from_cstr("bab"), &pid_bab), TURBO_OK);
    check_equal(ac_automaton_pattern_count(ac), 3U);

    check_equal(ac_automaton_build(ac), TURBO_OK);
    rc = ac_automaton_match(ac, text, capture_ac_match, &matches);
    check_equal(rc, TURBO_OK);
    check_equal(matches.count, 5U);
    check(have_record(&matches, pid_aba, 0, 3, (size_t)-1));
    check(have_record(&matches, pid_aba, 2, 5, (size_t)-1));
    check(have_record(&matches, pid_ba, 1, 3, (size_t)-1));
    check(have_record(&matches, pid_ba, 3, 5, (size_t)-1));
    check(have_record(&matches, pid_bab, 1, 4, (size_t)-1));
    ac_automaton_destroy(ac);
    ac_automaton_free(ac);
  }

  it("在 UTF-8 文本中进行多模式匹配") {
    ac_utf8_automaton_t *ac = ac_utf8_automaton_create();
    uint32_t pid_ni = 0, pid_hao = 0, pid_world = 0, pid_haosh = 0;
    vstr text = vstr_from_cstr("你好世界");
    match_record_list_t matches = {0};
    int rc;
    check_not_null(ac);
    check_equal(ac_utf8_automaton_add_pattern(ac, vstr_from_cstr("你"), &pid_ni), TURBO_OK);
    check_equal(ac_utf8_automaton_add_pattern(ac, vstr_from_cstr("好"), &pid_hao), TURBO_OK);
    check_equal(ac_utf8_automaton_add_pattern(ac, vstr_from_cstr("世界"), &pid_world), TURBO_OK);
    check_equal(ac_utf8_automaton_add_pattern(ac, vstr_from_cstr("好世"), &pid_haosh), TURBO_OK);
    check_equal(ac_utf8_automaton_pattern_count(ac), 4U);

    check_equal(ac_utf8_automaton_build(ac), TURBO_OK);
    rc = ac_utf8_automaton_match(ac, text, capture_ac_match, &matches);
    check_equal(rc, TURBO_OK);
    check_equal(matches.count, 4U);
    check(have_record(&matches, pid_ni, 0, 1, (size_t)-1));
    check(have_record(&matches, pid_hao, 1, 2, (size_t)-1));
    check(have_record(&matches, pid_haosh, 1, 3, (size_t)-1));
    check(have_record(&matches, pid_world, 2, 4, (size_t)-1));
    ac_utf8_automaton_destroy(ac);
    ac_utf8_automaton_free(ac);
  }

  it("支持回调早停") {
    ac_automaton_t *ac = ac_automaton_create();
    int rc;

    check_not_null(ac);
    rc = ac_automaton_add_pattern(ac, vstr_from_cstr("ab"), NULL);
    check_equal(rc, TURBO_OK);
    rc = ac_automaton_add_pattern(ac, vstr_from_cstr("b"), NULL);
    check_equal(rc, TURBO_OK);
    check_equal(ac_automaton_build(ac), TURBO_OK);

    rc = ac_automaton_match(ac, vstr_from_cstr("ab"), stop_after_one_ac, NULL);
    check_equal(rc, TURBO_OK);
    ac_automaton_destroy(ac);
    ac_automaton_free(ac);
  }
}

spec("Levenshtein 自动机") {
  it("按字节做近似子串匹配并返回编辑距离") {
    lev_automaton_t *lev = lev_automaton_create();
    match_record_list_t matches = {0};
    int rc;
    check_not_null(lev);

    rc = lev_automaton_init(lev, vstr_from_cstr("abc"), 1U);
    check_equal(rc, TURBO_OK);

    rc = lev_automaton_match(lev, vstr_from_cstr("abxd"), capture_lev_match, &matches);
    check_equal(rc, TURBO_OK);
    check_equal(matches.count, 1U);
    check_equal(matches.entries[0].start, 0U);
    check_equal(matches.entries[0].end, 3U);
    check_equal(matches.entries[0].distance, 1U);
    lev_automaton_destroy(lev);
    lev_automaton_free(lev);
  }

  it("按 UTF-8 code point 做 Levenshtein 匹配") {
    lev_utf8_automaton_t *lev = lev_utf8_automaton_create();
    match_record_list_t matches = {0};
    int rc;
    check_not_null(lev);

    rc = lev_utf8_automaton_init(lev, vstr_from_cstr("你好"), 1U);
    check_equal(rc, TURBO_OK);

    rc = lev_utf8_automaton_match(lev, vstr_from_cstr("你他"), capture_lev_match, &matches);
    check_equal(rc, TURBO_OK);
    check_equal(matches.count, 1U);
    check_equal(matches.entries[0].start, 0U);
    check_equal(matches.entries[0].end, 2U);
    check_equal(matches.entries[0].distance, 1U);
    lev_utf8_automaton_destroy(lev);
    lev_utf8_automaton_free(lev);
  }

  it("回调可在近似匹配中提前终止") {
    lev_automaton_t *lev = lev_automaton_create();
    match_record_list_t matches = {0};
    int rc;
    check_not_null(lev);

    rc = lev_automaton_init(lev, vstr_from_cstr("abcd"), 2U);
    check_equal(rc, TURBO_OK);
    rc = lev_automaton_match(lev, vstr_from_cstr("abxd"), stop_after_one_lev, &matches);
    check_equal(rc, TURBO_OK);
    check_equal(matches.count, 1U);
    lev_automaton_destroy(lev);
    lev_automaton_free(lev);
  }

  it("对非法输入返回 TURBO_EINVAL") {
    ac_automaton_t *ac = NULL;
    lev_automaton_t *lev = NULL;
    lev_utf8_automaton_t *lev_utf8 = NULL;
    match_record_list_t matches = {0};
    int rc;

    check_equal(ac_automaton_match(ac, vstr_from_cstr("abc"), capture_ac_match, &matches),
                TURBO_EINVAL);
    rc = lev_automaton_init(lev, (vstr){.data = NULL, .len = 0U}, 1U);
    check_equal(rc, TURBO_EINVAL);
    rc = lev_utf8_automaton_init(NULL, vstr_from_cstr("\x80"), 1U);
    check_equal(rc, TURBO_EINVAL);
    lev = lev_automaton_create();
    check_not_null(lev);
    lev_automaton_free(lev);
    lev_utf8 = lev_utf8_automaton_create();
    check_not_null(lev_utf8);
    rc = lev_utf8_automaton_init(lev_utf8, vstr_from_cstr("\x80"), 1U);
    check_equal(rc, TURBO_EINVAL);
    lev_utf8_automaton_destroy(lev_utf8);
    lev_utf8_automaton_free(lev_utf8);
  }
}
