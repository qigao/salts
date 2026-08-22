#include "levenshtein_automaton.h"
#include "turbo_error.h"
#include "turbostl_status_internal.h"
#include <turbostl/vec.h>

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define LEVENSHTEIN_INF (SIZE_MAX / 4U)

struct lev_automaton_s {
  vec_t pattern;
  size_t max_distance;
  bool initialized;
};

struct lev_utf8_automaton_s {
  vec_t pattern;
  size_t max_distance;
  bool initialized;
};

static size_t lev_safe_add(size_t lhs, size_t rhs) {
  if (lhs > LEVENSHTEIN_INF || rhs > LEVENSHTEIN_INF - lhs) return LEVENSHTEIN_INF;
  return lhs + rhs;
}

static int lev_init_common(lev_automaton_t *lev, vstr pattern,
                           size_t max_distance, bool utf8_pattern) {
  turbostl_status status;
  if (!lev || (!pattern.data && pattern.len != 0U) ||
      max_distance > LEVENSHTEIN_INF)
    return TURBO_EINVAL;
  if (lev->initialized) return TURBO_EINVAL;

  if (utf8_pattern && !vstr_utf8_valid(pattern)) return TURBO_EINVAL;
  if (pattern.len == 0U) return TURBO_EINVAL;

  status = vec_init_bytes(
      &lev->pattern, utf8_pattern ? sizeof(uint32_t) : sizeof(uint8_t),
      utf8_pattern ? _Alignof(uint32_t) : _Alignof(uint8_t), pattern.len);
  if (status != TURBO_STL_OK)
    return turbo_core_status_from_stl(status);

  if (utf8_pattern) {
    vstr rest = pattern;
    uint32_t cp = 0;
    while (rest.len > 0U) {
      if (!vstr_utf8_next(&rest, &cp)) {
        vec_destroy(&lev->pattern);
        return TURBO_EINVAL;
      }
      status = vec_push(&lev->pattern, &cp);
      if (status != TURBO_STL_OK) {
        vec_destroy(&lev->pattern);
        return turbo_core_status_from_stl(status);
      }
    }
  } else {
    for (size_t i = 0; i < pattern.len; ++i) {
      uint8_t byte = (uint8_t)pattern.data[i];
      status = vec_push(&lev->pattern, &byte);
      if (status != TURBO_STL_OK) {
        vec_destroy(&lev->pattern);
        return turbo_core_status_from_stl(status);
      }
    }
  }

  lev->max_distance = max_distance;
  lev->initialized = true;
  return TURBO_OK;
}

int lev_automaton_init(lev_automaton_t *lev, vstr pattern, size_t max_distance) {
  return lev_init_common(lev, pattern, max_distance, false);
}

lev_automaton_t *lev_automaton_create(void) {
  return (lev_automaton_t *)calloc(1, sizeof(lev_automaton_t));
}

void lev_automaton_free(lev_automaton_t *lev) {
  if (!lev) return;
  lev_automaton_destroy(lev);
  free(lev);
}

void lev_automaton_destroy(lev_automaton_t *lev) {
  if (!lev || !lev->initialized) return;
  vec_destroy(&lev->pattern);
  memset(lev, 0, sizeof(*lev));
}

int lev_utf8_automaton_init(lev_utf8_automaton_t *lev, vstr pattern, size_t max_distance) {
  return lev_init_common((lev_automaton_t *)lev, pattern, max_distance, true);
}

lev_utf8_automaton_t *lev_utf8_automaton_create(void) {
  return (lev_utf8_automaton_t *)calloc(1, sizeof(lev_utf8_automaton_t));
}

void lev_utf8_automaton_free(lev_utf8_automaton_t *lev) {
  if (!lev) return;
  lev_utf8_automaton_destroy(lev);
  free(lev);
}

void lev_utf8_automaton_destroy(lev_utf8_automaton_t *lev) {
  if (!lev || !lev->initialized) return;
  vec_destroy(&lev->pattern);
  memset(lev, 0, sizeof(*lev));
}

static int lev_match_bytes(const uint8_t *pattern, size_t pattern_len, size_t max_distance,
                          vstr text, levenshtein_match_cb cb, void *user_data) {
  size_t *prev_dp = NULL;
  size_t *curr_dp = NULL;
  size_t *prev_start = NULL;
  size_t *curr_start = NULL;
  size_t *tmp_start = NULL;
  size_t *tmp_dp = NULL;

  prev_dp = (size_t *)malloc((pattern_len + 1U) * sizeof(size_t));
  curr_dp = (size_t *)malloc((pattern_len + 1U) * sizeof(size_t));
  prev_start = (size_t *)malloc((pattern_len + 1U) * sizeof(size_t));
  curr_start = (size_t *)malloc((pattern_len + 1U) * sizeof(size_t));
  if (!prev_dp || !curr_dp || !prev_start || !curr_start) {
    free(prev_dp);
    free(curr_dp);
    free(prev_start);
    free(curr_start);
    return TURBO_ENOMEM;
  }

  for (size_t j = 0; j <= pattern_len; ++j) {
    prev_dp[j] = j;
    prev_start[j] = 0U;
  }

  for (size_t i = 0; i < text.len; ++i) {
    uint8_t text_byte = (uint8_t)text.data[i];
    curr_dp[0] = 0U;
    curr_start[0] = i + 1U;

    for (size_t j = 1; j <= pattern_len; ++j) {
      size_t del = lev_safe_add(prev_dp[j], 1U);
      size_t ins = lev_safe_add(curr_dp[j - 1U], 1U);
      size_t sub = lev_safe_add(prev_dp[j - 1U], (pattern[j - 1] == text_byte) ? 0U : 1U);
      size_t best = del;
      size_t best_start = prev_start[j];

      if (ins < best || (ins == best && curr_start[j - 1U] < best_start)) {
        best = ins;
        best_start = curr_start[j - 1U];
      }
      if (sub < best || (sub == best && prev_start[j - 1U] < best_start)) {
        best = sub;
        best_start = prev_start[j - 1U];
      }
      curr_dp[j] = best;
      curr_start[j] = best_start;
    }

    if (i + 1U >= pattern_len && curr_dp[pattern_len] <= max_distance &&
        curr_start[pattern_len] <= i + 1U) {
      if (!cb(curr_start[pattern_len], i + 1U, curr_dp[pattern_len], user_data)) {
        free(prev_dp);
        free(curr_dp);
        free(prev_start);
        free(curr_start);
        return TURBO_OK;
      }
    }

    tmp_dp = prev_dp;
    prev_dp = curr_dp;
    curr_dp = tmp_dp;
    tmp_start = prev_start;
    prev_start = curr_start;
    curr_start = tmp_start;
  }

  free(prev_dp);
  free(curr_dp);
  free(prev_start);
  free(curr_start);
  return TURBO_OK;
}

int lev_automaton_match(const lev_automaton_t *lev, vstr text, levenshtein_match_cb cb, void *user_data) {
  size_t pattern_len = 0;
  const uint8_t *pattern = NULL;

  if (!lev || !lev->initialized || (!text.data && text.len != 0U) || !cb) return TURBO_EINVAL;
  pattern_len = vec_size(&lev->pattern);
  pattern = (const uint8_t *)vec_data_const(&lev->pattern);
  if (!pattern || pattern_len == 0U) return TURBO_EINVAL;

  return lev_match_bytes(pattern, pattern_len, lev->max_distance, text, cb, user_data);
}

static int lev_match_utf8(const uint32_t *pattern, size_t pattern_len, size_t max_distance,
                         vstr text, levenshtein_match_cb cb, void *user_data) {
  size_t *prev_dp = NULL;
  size_t *curr_dp = NULL;
  size_t *prev_start = NULL;
  size_t *curr_start = NULL;
  size_t *tmp_ptr = NULL;

  uint32_t *text_cps = NULL;
  size_t text_cp_len = 0U;
  vstr rest = text;
  uint32_t cp = 0U;

  if (!vstr_utf8_valid(text)) return TURBO_EINVAL;
  prev_dp = (size_t *)malloc((pattern_len + 1U) * sizeof(size_t));
  curr_dp = (size_t *)malloc((pattern_len + 1U) * sizeof(size_t));
  prev_start = (size_t *)malloc((pattern_len + 1U) * sizeof(size_t));
  curr_start = (size_t *)malloc((pattern_len + 1U) * sizeof(size_t));
  if (!prev_dp || !curr_dp || !prev_start || !curr_start) {
    free(prev_dp);
    free(curr_dp);
    free(prev_start);
    free(curr_start);
    return TURBO_ENOMEM;
  }

  while (rest.len > 0U) {
    if (!vstr_utf8_next(&rest, &cp)) {
      free(prev_dp);
      free(curr_dp);
      free(prev_start);
      free(curr_start);
      return TURBO_EINVAL;
    }
    uint32_t *new_text = (uint32_t *)realloc(text_cps, (text_cp_len + 1U) * sizeof(uint32_t));
    if (!new_text) {
      free(text_cps);
      free(prev_dp);
      free(curr_dp);
      free(prev_start);
      free(curr_start);
      return TURBO_ENOMEM;
    }
    text_cps = new_text;
    text_cps[text_cp_len++] = cp;
  }

  for (size_t j = 0; j <= pattern_len; ++j) {
    prev_dp[j] = j;
    prev_start[j] = 0U;
  }

  for (size_t i = 0; i < text_cp_len; ++i) {
    uint32_t text_cp = text_cps[i];
    curr_dp[0] = 0U;
    curr_start[0] = i + 1U;

    for (size_t j = 1; j <= pattern_len; ++j) {
      size_t del = lev_safe_add(prev_dp[j], 1U);
      size_t ins = lev_safe_add(curr_dp[j - 1U], 1U);
      size_t sub = lev_safe_add(prev_dp[j - 1U], (pattern[j - 1] == text_cp) ? 0U : 1U);
      size_t best = del;
      size_t best_start = prev_start[j];

      if (ins < best || (ins == best && curr_start[j - 1U] < best_start)) {
        best = ins;
        best_start = curr_start[j - 1U];
      }
      if (sub < best || (sub == best && prev_start[j - 1U] < best_start)) {
        best = sub;
        best_start = prev_start[j - 1U];
      }
      curr_dp[j] = best;
      curr_start[j] = best_start;
    }

    if (i + 1U >= pattern_len && curr_dp[pattern_len] <= max_distance &&
        curr_start[pattern_len] <= i + 1U) {
      if (!cb(curr_start[pattern_len], i + 1U, curr_dp[pattern_len], user_data)) {
        free(prev_dp);
        free(curr_dp);
        free(prev_start);
        free(curr_start);
        free(text_cps);
        return TURBO_OK;
      }
    }

    tmp_ptr = prev_dp;
    prev_dp = curr_dp;
    curr_dp = tmp_ptr;
    tmp_ptr = prev_start;
    prev_start = curr_start;
    curr_start = tmp_ptr;
  }

  free(prev_dp);
  free(curr_dp);
  free(prev_start);
  free(curr_start);
  free(text_cps);
  return TURBO_OK;
}

int lev_utf8_automaton_match(const lev_utf8_automaton_t *lev, vstr text,
                             levenshtein_match_cb cb, void *user_data) {
  size_t pattern_len = 0;
  const uint32_t *pattern = NULL;

  if (!lev || !lev->initialized || (!text.data && text.len != 0U) || !cb) return TURBO_EINVAL;
  pattern_len = vec_size(&lev->pattern);
  pattern = (const uint32_t *)vec_data_const(&lev->pattern);
  if (!pattern || pattern_len == 0U) return TURBO_EINVAL;

  return lev_match_utf8(pattern, pattern_len, lev->max_distance, text, cb, user_data);
}
