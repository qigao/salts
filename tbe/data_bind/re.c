#include "re.h"

#include <limits.h>

#if !defined(RE_MALLOC) || !defined(RE_FREE)
  #include <stdlib.h>
#endif

#ifndef RE_MALLOC
  #define RE_MALLOC(size) malloc(size)
#endif

#ifndef RE_FREE
  #define RE_FREE(pointer) free(pointer)
#endif

struct regex_t {
  char *pattern;
  size_t pattern_len;
};

typedef struct re_exec {
  const char *pattern;
  const char *text;
  size_t text_len;
  re_limits_t limits;
  uint64_t steps;
  size_t workspace_used;
  uint32_t depth;
  re_status_t status;
} re_exec_t;

static int re_find_char_class_end(re_exec_t *exec, size_t start, size_t end, size_t *out);
static int re_find_group_end(re_exec_t *exec, size_t start, size_t end, size_t *out);
static int re_find_branch_end(re_exec_t *exec, size_t start, size_t end, size_t *out);
static int re_validate_expr(re_exec_t *exec, size_t start, size_t end);
static int re_validate_sequence(re_exec_t *exec, size_t start, size_t end);
static int re_atom_end_at(re_exec_t *exec, size_t start, size_t end, int validate_nested,
                          size_t *out);
static int re_match_expr(re_exec_t *exec, size_t start, size_t end, size_t pos, size_t *out_pos);
static int re_match_sequence(re_exec_t *exec, size_t start, size_t end, size_t pos,
                             size_t *out_pos);
static int re_match_atom_once(re_exec_t *exec, size_t start, size_t atom_end, size_t pos,
                              size_t *out_pos);

static size_t re_cstr_len(const char *text) {
  size_t len = 0;
  if (text == NULL) return 0;
  while (text[len] != '\0')
    ++len;
  return len;
}

static int re_is_digit(unsigned char c) { return c >= '0' && c <= '9'; }

static int re_is_alpha(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

static int re_is_whitespace(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\f' || c == '\r' || c == '\n' || c == '\v';
}

static int re_is_alphanum(unsigned char c) { return c == '_' || re_is_alpha(c) || re_is_digit(c); }

static int re_match_dot(unsigned char c) {
#if defined(RE_DOT_MATCHES_NEWLINE) && (RE_DOT_MATCHES_NEWLINE == 1)
  (void)c;
  return 1;
#else
  return c != '\n' && c != '\r';
#endif
}

static int re_match_metachar(unsigned char c, unsigned char metachar) {
  switch (metachar) {
  case 'd':
    return re_is_digit(c);
  case 'D':
    return !re_is_digit(c);
  case 'w':
    return re_is_alphanum(c);
  case 'W':
    return !re_is_alphanum(c);
  case 's':
    return re_is_whitespace(c);
  case 'S':
    return !re_is_whitespace(c);
  default:
    return c == metachar;
  }
}

static int re_is_quantifier(char c) { return c == '*' || c == '+' || c == '?'; }

static void re_fail(re_exec_t *exec, re_status_t status) {
  if (exec->status == RE_STATUS_OK) exec->status = status;
}

static int re_step(re_exec_t *exec) {
  if (exec->status != RE_STATUS_OK) return 0;
  if (exec->steps >= exec->limits.max_steps) {
    re_fail(exec, RE_STATUS_STEP_LIMIT);
    return 0;
  }
  ++exec->steps;
  return 1;
}

static int re_enter(re_exec_t *exec) {
  if (!re_step(exec)) return 0;
  if (exec->depth >= exec->limits.max_depth) {
    re_fail(exec, RE_STATUS_DEPTH_LIMIT);
    return 0;
  }
  ++exec->depth;
  return 1;
}

static void re_leave(re_exec_t *exec) {
  if (exec->depth != 0) --exec->depth;
}

static void *re_workspace_alloc(re_exec_t *exec, size_t size) {
  void *allocation;
  if (size > exec->limits.max_workspace_bytes ||
      exec->workspace_used > exec->limits.max_workspace_bytes - size) {
    re_fail(exec, RE_STATUS_WORKSPACE_LIMIT);
    return NULL;
  }
  allocation = RE_MALLOC(size);
  if (allocation == NULL) {
    re_fail(exec, RE_STATUS_NO_MEMORY);
    return NULL;
  }
  exec->workspace_used += size;
  return allocation;
}

static void re_workspace_free(re_exec_t *exec, void *allocation, size_t size) {
  if (allocation == NULL) return;
  RE_FREE(allocation);
  exec->workspace_used -= size;
}

re_limits_t re_limits_default(void) {
  const re_limits_t limits = RE_LIMITS_INIT;
  return limits;
}

static re_status_t re_normalize_limits(const re_limits_t *input, re_limits_t *out) {
  if (out == NULL) return RE_STATUS_INVALID_ARGUMENT;
  if (input == NULL) {
    *out = re_limits_default();
    return RE_STATUS_OK;
  }
  if (input->struct_size < sizeof(*input) || input->max_pattern_bytes == 0 ||
      input->max_text_bytes == 0 || input->max_depth == 0 || input->max_workspace_bytes == 0 ||
      input->max_steps == 0)
    return RE_STATUS_INVALID_ARGUMENT;
  *out = *input;
  return RE_STATUS_OK;
}

static int re_find_char_class_end(re_exec_t *exec, size_t start, size_t end, size_t *out) {
  size_t i = start + 1;
  if (i < end && exec->pattern[i] == '^') ++i;
  while (i < end) {
    if (!re_step(exec)) return 0;
    if (exec->pattern[i] == '\\') {
      if (i + 1 >= end) return 0;
      i += 2;
      continue;
    }
    if (exec->pattern[i] == ']') {
      *out = i;
      return 1;
    }
    ++i;
  }
  return 0;
}

static int re_find_group_end(re_exec_t *exec, size_t start, size_t end, size_t *out) {
  size_t depth = 1;
  size_t i;
  for (i = start + 1; i < end; ++i) {
    size_t class_end;
    if (!re_step(exec)) return 0;
    if (exec->pattern[i] == '\\') {
      if (i + 1 >= end) return 0;
      ++i;
      continue;
    }
    if (exec->pattern[i] == '[') {
      if (!re_find_char_class_end(exec, i, end, &class_end)) return 0;
      i = class_end;
      continue;
    }
    if (exec->pattern[i] == '(') {
      ++depth;
    } else if (exec->pattern[i] == ')') {
      --depth;
      if (depth == 0) {
        *out = i;
        return 1;
      }
    }
  }
  return 0;
}

static int re_find_branch_end(re_exec_t *exec, size_t start, size_t end, size_t *out) {
  size_t depth = 0;
  size_t i;
  for (i = start; i < end; ++i) {
    size_t class_end;
    if (!re_step(exec)) return 0;
    if (exec->pattern[i] == '\\') {
      if (i + 1 >= end) return 0;
      ++i;
      continue;
    }
    if (exec->pattern[i] == '[') {
      if (!re_find_char_class_end(exec, i, end, &class_end)) return 0;
      i = class_end;
      continue;
    }
    if (exec->pattern[i] == '(') {
      ++depth;
    } else if (exec->pattern[i] == ')') {
      if (depth == 0) return 0;
      --depth;
    } else if (exec->pattern[i] == '|' && depth == 0) {
      *out = i;
      return 1;
    }
  }
  *out = end;
  return 1;
}

static int re_atom_end_at(re_exec_t *exec, size_t start, size_t end, int validate_nested,
                          size_t *out) {
  size_t nested_end;
  if (!re_step(exec) || start >= end) return 0;
  if (exec->pattern[start] == '[') {
    if (!re_find_char_class_end(exec, start, end, &nested_end)) return 0;
    *out = nested_end + 1;
    return 1;
  }
  if (exec->pattern[start] == '(') {
    if (!re_find_group_end(exec, start, end, &nested_end)) return 0;
    if (validate_nested && !re_validate_expr(exec, start + 1, nested_end)) return 0;
    *out = nested_end + 1;
    return 1;
  }
  if (exec->pattern[start] == '\\') {
    if (start + 1 >= end) return 0;
    *out = start + 2;
    return 1;
  }
  if (exec->pattern[start] == '|' || exec->pattern[start] == ')' ||
      re_is_quantifier(exec->pattern[start]))
    return 0;
  *out = start + 1;
  return 1;
}

static int re_validate_sequence(re_exec_t *exec, size_t start, size_t end) {
  size_t i = start;
  int valid = 0;
  if (!re_enter(exec)) return 0;
  while (i < end) {
    size_t atom_end;
    if (!re_atom_end_at(exec, i, end, 1, &atom_end)) goto cleanup;
    if (atom_end < end && re_is_quantifier(exec->pattern[atom_end])) {
      if (exec->pattern[i] == '^' || exec->pattern[i] == '$') goto cleanup;
      ++atom_end;
    }
    i = atom_end;
  }
  valid = 1;

cleanup:
  re_leave(exec);
  return valid;
}

static int re_validate_expr(re_exec_t *exec, size_t start, size_t end) {
  size_t branch_start = start;
  int valid = 0;
  if (!re_enter(exec)) return 0;
  while (branch_start <= end) {
    size_t branch_end;
    if (!re_find_branch_end(exec, branch_start, end, &branch_end)) goto cleanup;
    if (!re_validate_sequence(exec, branch_start, branch_end)) goto cleanup;
    if (branch_end == end) {
      valid = 1;
      goto cleanup;
    }
    branch_start = branch_end + 1;
  }
  valid = 1;

cleanup:
  re_leave(exec);
  return valid;
}

static re_status_t re_validate_with_limits(const char *pattern, size_t pattern_len,
                                           const re_limits_t *limits) {
  re_exec_t exec;
  if (pattern == NULL && pattern_len != 0) return RE_STATUS_INVALID_ARGUMENT;
  if (pattern_len > limits->max_pattern_bytes) return RE_STATUS_PATTERN_LIMIT;
  exec.pattern = pattern;
  exec.text = NULL;
  exec.text_len = 0;
  exec.limits = *limits;
  exec.steps = 0;
  exec.workspace_used = 0;
  exec.depth = 0;
  exec.status = RE_STATUS_OK;
  if (!re_validate_expr(&exec, 0, pattern_len))
    return exec.status == RE_STATUS_OK ? RE_STATUS_INVALID_PATTERN : exec.status;
  return exec.status;
}

re_status_t re_validate_n(const char *pattern, size_t pattern_len, const re_limits_t *limits) {
  re_limits_t normalized;
  re_status_t status = re_normalize_limits(limits, &normalized);
  if (status != RE_STATUS_OK) return status;
  return re_validate_with_limits(pattern, pattern_len, &normalized);
}

re_status_t re_compile_n(const char *pattern, size_t pattern_len, const re_limits_t *limits,
                         re_t *out_pattern) {
  struct regex_t *compiled = NULL;
  re_limits_t normalized;
  re_status_t status;
  size_t i;
  if (out_pattern == NULL) return RE_STATUS_INVALID_ARGUMENT;
  *out_pattern = NULL;
  status = re_normalize_limits(limits, &normalized);
  if (status != RE_STATUS_OK) return status;
  status = re_validate_with_limits(pattern, pattern_len, &normalized);
  if (status != RE_STATUS_OK) return status;
  compiled = (struct regex_t *)RE_MALLOC(sizeof(*compiled));
  if (compiled == NULL) return RE_STATUS_NO_MEMORY;
  compiled->pattern = (char *)RE_MALLOC(pattern_len + 1);
  if (compiled->pattern == NULL) {
    RE_FREE(compiled);
    return RE_STATUS_NO_MEMORY;
  }
  for (i = 0; i < pattern_len; ++i)
    compiled->pattern[i] = pattern[i];
  compiled->pattern[pattern_len] = '\0';
  compiled->pattern_len = pattern_len;
  *out_pattern = compiled;
  return RE_STATUS_OK;
}

void re_destroy(re_t pattern) {
  if (pattern == NULL) return;
  RE_FREE(pattern->pattern);
  pattern->pattern = NULL;
  pattern->pattern_len = 0;
  RE_FREE(pattern);
}

static int re_match_char_class(re_exec_t *exec, unsigned char c, size_t start, size_t len) {
  size_t i = 0;
  while (i < len) {
    unsigned char item;
    if (!re_step(exec)) return 0;
    item = (unsigned char)exec->pattern[start + i];
    if (i + 2 < len && item != '-' && exec->pattern[start + i + 1] == '-' &&
        (unsigned char)exec->pattern[start + i + 2] != '-' && c >= item &&
        c <= (unsigned char)exec->pattern[start + i + 2])
      return 1;
    if (item == '\\') {
      if (i + 1 < len && re_match_metachar(c, (unsigned char)exec->pattern[start + i + 1]))
        return 1;
      i += 2;
      continue;
    }
    if (c == item && (c != '-' || i == 0 || i == len - 1)) return 1;
    ++i;
  }
  return 0;
}

static int re_match_atom_once(re_exec_t *exec, size_t start, size_t atom_end, size_t pos,
                              size_t *out_pos) {
  if (!re_step(exec)) return 0;
  if (exec->pattern[start] == '^') {
    *out_pos = pos;
    return pos == 0;
  }
  if (exec->pattern[start] == '$') {
    *out_pos = pos;
    return pos == exec->text_len;
  }
  if (exec->pattern[start] == '(') {
    size_t group_end;
    if (!re_find_group_end(exec, start, atom_end, &group_end)) return 0;
    return re_match_expr(exec, start + 1, group_end, pos, out_pos);
  }
  if (pos >= exec->text_len) return 0;
  if (exec->pattern[start] == '.') {
    if (!re_match_dot((unsigned char)exec->text[pos])) return 0;
    *out_pos = pos + 1;
    return 1;
  }
  if (exec->pattern[start] == '[') {
    size_t class_end;
    size_t class_start;
    int inverted;
    int in_class;
    if (!re_find_char_class_end(exec, start, atom_end, &class_end)) return 0;
    inverted = start + 1 < class_end && exec->pattern[start + 1] == '^';
    class_start = inverted ? start + 2 : start + 1;
    in_class = re_match_char_class(exec, (unsigned char)exec->text[pos], class_start,
                                   class_end - class_start);
    if (exec->status != RE_STATUS_OK || (inverted ? in_class : !in_class)) return 0;
    *out_pos = pos + 1;
    return 1;
  }
  if (exec->pattern[start] == '\\') {
    if (!re_match_metachar((unsigned char)exec->text[pos], (unsigned char)exec->pattern[start + 1]))
      return 0;
    *out_pos = pos + 1;
    return 1;
  }
  if ((unsigned char)exec->text[pos] != (unsigned char)exec->pattern[start]) return 0;
  *out_pos = pos + 1;
  return 1;
}

static int re_match_sequence(re_exec_t *exec, size_t start, size_t end, size_t pos,
                             size_t *out_pos) {
  size_t atom_end;
  size_t rest_start;
  char quantifier = 0;
  int matched = 0;
  if (!re_enter(exec)) return 0;
  if (start >= end) {
    *out_pos = pos;
    matched = 1;
    goto cleanup;
  }
  if (!re_atom_end_at(exec, start, end, 0, &atom_end)) goto cleanup;
  rest_start = atom_end;
  if (rest_start < end && re_is_quantifier(exec->pattern[rest_start]))
    quantifier = exec->pattern[rest_start++];
  if (quantifier == 0) {
    size_t next_pos;
    matched = re_match_atom_once(exec, start, atom_end, pos, &next_pos) &&
              re_match_sequence(exec, rest_start, end, next_pos, out_pos);
    goto cleanup;
  }
  if (quantifier == '?') {
    size_t next_pos;
    if (re_match_sequence(exec, rest_start, end, pos, out_pos)) {
      matched = 1;
      goto cleanup;
    }
    if (exec->status != RE_STATUS_OK) goto cleanup;
    matched = re_match_atom_once(exec, start, atom_end, pos, &next_pos) &&
              re_match_sequence(exec, rest_start, end, next_pos, out_pos);
    goto cleanup;
  }
  {
    size_t repeat_count = 0;
    size_t repeat_capacity = exec->text_len - pos + 1;
    size_t *positions;
    size_t positions_size;
    size_t current_pos = pos;
    size_t min_count = quantifier == '+' ? 1 : 0;
    if (repeat_capacity > ((size_t)-1) / sizeof(*positions)) {
      re_fail(exec, RE_STATUS_WORKSPACE_LIMIT);
      goto cleanup;
    }
    positions_size = repeat_capacity * sizeof(*positions);
    positions = (size_t *)re_workspace_alloc(exec, positions_size);
    if (positions == NULL) goto cleanup;
    positions[0] = pos;
    while (repeat_count + 1 < repeat_capacity) {
      size_t next_pos;
      if (!re_match_atom_once(exec, start, atom_end, current_pos, &next_pos) ||
          next_pos == current_pos)
        break;
      current_pos = next_pos;
      positions[++repeat_count] = current_pos;
    }
    if (exec->status == RE_STATUS_OK) {
      while (repeat_count >= min_count) {
        if (re_match_sequence(exec, rest_start, end, positions[repeat_count], out_pos)) {
          matched = 1;
          break;
        }
        if (exec->status != RE_STATUS_OK || repeat_count == 0) break;
        --repeat_count;
      }
    }
    re_workspace_free(exec, positions, positions_size);
  }

cleanup:
  re_leave(exec);
  return matched;
}

static int re_match_expr(re_exec_t *exec, size_t start, size_t end, size_t pos, size_t *out_pos) {
  size_t branch_start = start;
  int matched = 0;
  if (!re_enter(exec)) return 0;
  while (branch_start <= end) {
    size_t branch_end;
    if (!re_find_branch_end(exec, branch_start, end, &branch_end)) goto cleanup;
    if (re_match_sequence(exec, branch_start, branch_end, pos, out_pos)) {
      matched = 1;
      goto cleanup;
    }
    if (exec->status != RE_STATUS_OK || branch_end == end) goto cleanup;
    branch_start = branch_end + 1;
  }

cleanup:
  re_leave(exec);
  return matched;
}

static re_status_t re_match_borrowed(const char *pattern, size_t pattern_len, const char *text,
                                     size_t text_len, const re_limits_t *limits,
                                     re_match_result_t *out_match) {
  re_exec_t exec;
  size_t index;
  out_match->index = RE_NPOS;
  out_match->length = 0;
  if (pattern_len > limits->max_pattern_bytes) return RE_STATUS_PATTERN_LIMIT;
  if (text == NULL && text_len != 0) return RE_STATUS_INVALID_ARGUMENT;
  if (text_len > limits->max_text_bytes) return RE_STATUS_TEXT_LIMIT;
  exec.pattern = pattern;
  exec.text = text;
  exec.text_len = text_len;
  exec.limits = *limits;
  exec.steps = 0;
  exec.workspace_used = 0;
  exec.depth = 0;
  exec.status = RE_STATUS_OK;
  for (index = 0; index <= text_len; ++index) {
    size_t out_pos = index;
    if (!re_step(&exec)) return exec.status;
    if (re_match_expr(&exec, 0, pattern_len, index, &out_pos)) {
      out_match->index = index;
      out_match->length = out_pos - index;
      return RE_STATUS_OK;
    }
    if (exec.status != RE_STATUS_OK) return exec.status;
    if (pattern_len != 0 && pattern[0] == '^') break;
  }
  return RE_STATUS_NO_MATCH;
}

re_status_t re_matchn(re_t pattern, const char *text, size_t text_len, const re_limits_t *limits,
                      re_match_result_t *out_match) {
  re_limits_t normalized;
  re_status_t status;
  if (out_match == NULL || pattern == NULL) return RE_STATUS_INVALID_ARGUMENT;
  out_match->index = RE_NPOS;
  out_match->length = 0;
  status = re_normalize_limits(limits, &normalized);
  if (status != RE_STATUS_OK) return status;
  return re_match_borrowed(pattern->pattern, pattern->pattern_len, text, text_len, &normalized,
                           out_match);
}

re_status_t re_match_n(const char *pattern, size_t pattern_len, const char *text, size_t text_len,
                       const re_limits_t *limits, re_match_result_t *out_match) {
  re_limits_t normalized;
  re_status_t status;
  if (out_match == NULL) return RE_STATUS_INVALID_ARGUMENT;
  out_match->index = RE_NPOS;
  out_match->length = 0;
  status = re_normalize_limits(limits, &normalized);
  if (status != RE_STATUS_OK) return status;
  status = re_validate_with_limits(pattern, pattern_len, &normalized);
  if (status != RE_STATUS_OK) return status;
  return re_match_borrowed(pattern, pattern_len, text, text_len, &normalized, out_match);
}

const char *re_status_string(re_status_t status) {
  switch (status) {
  case RE_STATUS_OK:
    return "ok";
  case RE_STATUS_NO_MATCH:
    return "no match";
  case RE_STATUS_INVALID_ARGUMENT:
    return "invalid argument";
  case RE_STATUS_INVALID_PATTERN:
    return "invalid pattern";
  case RE_STATUS_PATTERN_LIMIT:
    return "pattern limit exceeded";
  case RE_STATUS_TEXT_LIMIT:
    return "text limit exceeded";
  case RE_STATUS_DEPTH_LIMIT:
    return "depth limit exceeded";
  case RE_STATUS_STEP_LIMIT:
    return "step limit exceeded";
  case RE_STATUS_WORKSPACE_LIMIT:
    return "workspace limit exceeded";
  case RE_STATUS_NO_MEMORY:
    return "out of memory";
  default:
    return "unknown regex status";
  }
}

re_t re_compile(const char *pattern) {
  re_t compiled = NULL;
  if (pattern == NULL ||
      re_compile_n(pattern, re_cstr_len(pattern), NULL, &compiled) != RE_STATUS_OK)
    return NULL;
  return compiled;
}

int re_matchp(re_t pattern, const char *text, int *matchlength) {
  re_match_result_t match;
  re_status_t status;
  if (matchlength == NULL) return -1;
  *matchlength = 0;
  if (text == NULL) return -1;
  status = re_matchn(pattern, text, re_cstr_len(text), NULL, &match);
  if (status != RE_STATUS_OK || match.index > INT_MAX || match.length > INT_MAX) return -1;
  *matchlength = (int)match.length;
  return (int)match.index;
}

int re_match(const char *pattern, const char *text, int *matchlength) {
  re_t compiled;
  int index;
  if (matchlength == NULL) return -1;
  *matchlength = 0;
  compiled = re_compile(pattern);
  if (compiled == NULL) return -1;
  index = re_matchp(compiled, text, matchlength);
  re_destroy(compiled);
  return index;
}
