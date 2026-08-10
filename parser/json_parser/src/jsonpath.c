/**
 * @file jsonpath.c
 * @brief JSONPath parser and matcher for json_value_t trees
 */

#include "json_parser.h"
#include "json_types.h"
#include "json_unicode.h"
#include "jsonpath_grammar_gen.h"
#include "jsonpath_types.h"
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSONPATH_ERROR_LEN 256
#define JSONPATH_NO_INDEX UINT32_MAX
#define JSONPATH_MAX_INSTRUCTIONS (1024U * 1024U)
#define JSONPATH_MAX_CONSTANT_BYTES (64U * 1024U * 1024U)
#define JSONPATH_STREAM_MAX_SEGMENTS 64U
#define JSONPATH_STREAM_MAX_ALTERNATIVES 64U
#define JSONPATH_STREAM_MAX_DEPTH 256U

typedef struct jsonpath_instruction_s {
  int type;
  int num;
  uint32_t down;
  uint32_t sibling;
  uint32_t string_offset;
  uint32_t string_len;
  size_t key_hash;
  double number;
} jsonpath_instruction_t;

struct json_path_program_s {
  jsonpath_instruction_t *instructions;
  char *constants;
  uint32_t instruction_count;
  uint32_t entry;
  uint32_t constant_bytes;
};

struct json_path_result_s {
  json_value_t **items;
  size_t count;
  size_t capacity;
  int error;
};

typedef enum {
  JSONPATH_STREAM_KEY,
  JSONPATH_STREAM_INDEX,
  JSONPATH_STREAM_WILDCARD
} jsonpath_stream_segment_kind_t;

typedef struct {
  jsonpath_stream_segment_kind_t kind;
  const jsonpath_instruction_t *instruction;
} jsonpath_stream_segment_t;

typedef struct {
  jsonpath_stream_segment_t segments[JSONPATH_STREAM_MAX_SEGMENTS];
  size_t count;
} jsonpath_stream_alternative_t;

typedef struct {
  unsigned long long active;
  unsigned long long selected;
  bool forward;
  bool object;
  size_t path_depth;
  size_t index;
} jsonpath_stream_frame_t;

struct json_path_stream_s {
  const json_path_program_t *program;
  json_path_stream_handler_t handler;
  void *ctx;
  json_sax_parser_t *parser;
  jsonpath_stream_alternative_t alternatives[JSONPATH_STREAM_MAX_ALTERNATIVES];
  size_t alternative_count;
  jsonpath_stream_frame_t frames[JSONPATH_STREAM_MAX_DEPTH];
  size_t depth;
  unsigned long long pending_active;
  bool pending_valid;
  bool root_started;
  bool failed;
  size_t matches;
  char error[JSONPATH_ERROR_LEN];
};

static char g_jsonpath_error[JSONPATH_ERROR_LEN] = {0};

void *JsonPathParseAlloc(void *(*mallocProc)(size_t));
void JsonPathParseFree(void *parser, void (*freeProc)(void *));
void JsonPathParse(void *parser, int tokenType, jsonpath_opcode_t *token,
                   jsonpath_parse_ctx_t *ctx);

static char *jsonpath_strdup_len(const char *str, size_t len) {
  char *out = (char *)malloc(len + 1);
  if (!out) return NULL;
  memcpy(out, str, len);
  out[len] = '\0';
  return out;
}

jsonpath_opcode_t *jsonpath_append_op(jsonpath_opcode_t *a, jsonpath_opcode_t *b) {
  jsonpath_opcode_t *tail = a;

  if (!a) return b;

  while (tail->sibling)
    tail = tail->sibling;

  tail->sibling = b;
  return a;
}

jsonpath_opcode_t *jsonpath_alloc_op(jsonpath_parse_ctx_t *ctx, int type, int num, double number,
                                     const char *str, ...) {
  jsonpath_opcode_t *child;
  jsonpath_opcode_t *op = (jsonpath_opcode_t *)calloc(1, sizeof(*op));
  if (!op) {
    if (ctx) ctx->error_code = -2;
    return NULL;
  }

  op->type = type;
  op->num = num;
  op->number = number;

  if (str) {
    op->str = jsonpath_strdup_len(str, strlen(str));
    if (!op->str) {
      free(op);
      if (ctx) ctx->error_code = -2;
      return NULL;
    }
  }

  va_list ap;
  va_start(ap, str);
  while ((child = va_arg(ap, jsonpath_opcode_t *)) != NULL) {
    if (!op->down) op->down = child;
    else jsonpath_append_op(op->down, child);
  }
  va_end(ap);

  op->next = ctx->pool;
  ctx->pool = op;
  return op;
}

static void jsonpath_free_ctx(jsonpath_parse_ctx_t *ctx) {
  jsonpath_opcode_t *op = ctx ? ctx->pool : NULL;
  while (op) {
    jsonpath_opcode_t *next = op->next;
    free(op->str);
    free(op);
    op = next;
  }
  free(ctx);
}

static int jsonpath_append_utf8(char *out, size_t *pos, int code) {
  if (code <= 0) return 0;
  if (code <= 0x7F) {
    out[(*pos)++] = (char)code;
  } else if (code <= 0x7FF) {
    out[(*pos)++] = (char)(0xC0 | (code >> 6));
    out[(*pos)++] = (char)(0x80 | (code & 0x3F));
  } else if (code <= 0xFFFF) {
    out[(*pos)++] = (char)(0xE0 | (code >> 12));
    out[(*pos)++] = (char)(0x80 | ((code >> 6) & 0x3F));
    out[(*pos)++] = (char)(0x80 | (code & 0x3F));
  } else if (code <= 0x10FFFF) {
    out[(*pos)++] = (char)(0xF0 | (code >> 18));
    out[(*pos)++] = (char)(0x80 | ((code >> 12) & 0x3F));
    out[(*pos)++] = (char)(0x80 | ((code >> 6) & 0x3F));
    out[(*pos)++] = (char)(0x80 | (code & 0x3F));
  }
  return 0;
}

static int jsonpath_parse_string(const char *buf, char quote, char **out_str,
                                 jsonpath_parse_ctx_t *ctx) {
  size_t cap = strlen(buf) + 1;
  size_t input_len = cap - 1;
  char *out = (char *)malloc(cap);
  size_t pos = 0;
  size_t i = 1;

  if (!out) return -2;

  while (buf[i]) {
    unsigned char c = (unsigned char)buf[i++];

    if (c == (unsigned char)quote) {
      out[pos] = '\0';
      *out_str = out;
      return (int)i;
    }

    if (c != '\\') {
      out[pos++] = (char)c;
      continue;
    }

    size_t escape = i - 1;
    c = (unsigned char)buf[i++];
    if (!c) {
      free(out);
      return -1;
    }

    switch (c) {
    case '"':
    case '\'':
    case '\\':
    case '/':
      out[pos++] = (char)c;
      break;
    case 'b':
      out[pos++] = '\b';
      break;
    case 'f':
      out[pos++] = '\f';
      break;
    case 'n':
      out[pos++] = '\n';
      break;
    case 'r':
      out[pos++] = '\r';
      break;
    case 't':
      out[pos++] = '\t';
      break;
    case 'u': {
      uint32_t codepoint;
      size_t next = escape;
      if (!json_unicode_decode_escape(buf, input_len, &next, &codepoint)) {
        free(out);
        ctx->error_pos = ctx->off + (int)escape;
        return -3;
      }
      pos += json_unicode_append_utf8(out + pos, codepoint);
      i = next;
      break;
    }
    case 'x': {
      int h0 = json_unicode_hex(buf[i]);
      int h1 = json_unicode_hex(buf[i + 1]);
      if (h0 < 0 || h1 < 0) {
        free(out);
        ctx->error_pos = ctx->off + (int)i;
        return -3;
      }
      jsonpath_append_utf8(out, &pos, (h0 << 4) | h1);
      i += 2;
      break;
    }
    default:
      if (quote == '/') {
        out[pos++] = '\\';
      }
      out[pos++] = (char)c;
      break;
    }
  }

  free(out);
  return -1;
}

static int jsonpath_lex(const char *input, jsonpath_parse_ctx_t *ctx, jsonpath_opcode_t **out_op) {
  jsonpath_opcode_t *op = NULL;
  char *str = NULL;
  char *end = NULL;
  int type = 0;
  int consumed = 1;

  *out_op = NULL;

  switch (*input) {
  case ' ':
  case '\t':
  case '\r':
  case '\n':
    return 1;
  case '&':
    if (input[1] == '&') {
      type = JSONPATH_TOKEN_AND;
      consumed = 2;
    }
    break;
  case '|':
    if (input[1] == '|') {
      type = JSONPATH_TOKEN_OR;
      consumed = 2;
    }
    break;
  case '<':
    type = input[1] == '=' ? JSONPATH_TOKEN_LE : JSONPATH_TOKEN_LT;
    consumed = input[1] == '=' ? 2 : 1;
    break;
  case '>':
    type = input[1] == '=' ? JSONPATH_TOKEN_GE : JSONPATH_TOKEN_GT;
    consumed = input[1] == '=' ? 2 : 1;
    break;
  case '!':
    type = input[1] == '=' ? JSONPATH_TOKEN_NE : JSONPATH_TOKEN_NOT;
    consumed = input[1] == '=' ? 2 : 1;
    break;
  case '=':
    type = JSONPATH_TOKEN_EQ;
    break;
  case '~':
    type = JSONPATH_TOKEN_MATCH;
    break;
  case ',':
    type = JSONPATH_TOKEN_UNION;
    break;
  case '.':
    type = JSONPATH_TOKEN_DOT;
    break;
  case '[':
    type = JSONPATH_TOKEN_BROPEN;
    break;
  case ']':
    type = JSONPATH_TOKEN_BRCLOSE;
    break;
  case '(':
    type = JSONPATH_TOKEN_POPEN;
    break;
  case ')':
    type = JSONPATH_TOKEN_PCLOSE;
    break;
  case '$':
    type = JSONPATH_TOKEN_ROOT;
    break;
  case '@':
    type = JSONPATH_TOKEN_THIS;
    break;
  case '*':
    type = JSONPATH_TOKEN_WILDCARD;
    break;
  case '\'':
  case '"':
    consumed = jsonpath_parse_string(input, *input, &str, ctx);
    if (consumed < 0) return consumed;
    type = JSONPATH_TOKEN_STRING;
    break;
  case '/':
    consumed = jsonpath_parse_string(input, '/', &str, ctx);
    if (consumed < 0) return consumed;
    while (isalpha((unsigned char)input[consumed]))
      consumed++;
    type = JSONPATH_TOKEN_REGEXP;
    break;
  default:
    if (*input == '-' || isdigit((unsigned char)*input)) {
      double number = strtod(input, &end);
      if (end == input) return -3;
      consumed = (int)(end - input);
      op = jsonpath_alloc_op(ctx, JSONPATH_TOKEN_NUMBER, (int)number, number, NULL, NULL);
      if (!op) return -2;
      *out_op = op;
      return consumed;
    }

    if (*input == '_' || isalpha((unsigned char)*input)) {
      const char *start = input;
      while (*input == '_' || isalnum((unsigned char)*input))
        input++;

      consumed = (int)(input - start);
      if (consumed == 4 && memcmp(start, "true", 4) == 0) {
        op = jsonpath_alloc_op(ctx, JSONPATH_TOKEN_BOOL, 1, 1.0, NULL, NULL);
      } else if (consumed == 5 && memcmp(start, "false", 5) == 0) {
        op = jsonpath_alloc_op(ctx, JSONPATH_TOKEN_BOOL, 0, 0.0, NULL, NULL);
      } else {
        str = jsonpath_strdup_len(start, (size_t)consumed);
        if (!str) return -2;
        op = jsonpath_alloc_op(ctx, JSONPATH_TOKEN_LABEL, 0, 0.0, str, NULL);
        free(str);
      }
      if (!op) return -2;
      *out_op = op;
      return consumed;
    }
    break;
  }

  if (!type) return -4;

  op = jsonpath_alloc_op(ctx, type, 0, 0.0, str, NULL);
  free(str);
  if (!op) return -2;

  *out_op = op;
  return consumed;
}

static jsonpath_parse_ctx_t *jsonpath_parse(const char *expr) {
  jsonpath_parse_ctx_t *ctx;
  void *parser;
  const char *ptr;
  int consumed;

  if (!expr || !*expr) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Empty JSONPath expression");
    return NULL;
  }

  ctx = (jsonpath_parse_ctx_t *)calloc(1, sizeof(*ctx));
  if (!ctx) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Out of memory");
    return NULL;
  }

  parser = JsonPathParseAlloc(malloc);
  if (!parser) {
    jsonpath_free_ctx(ctx);
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Out of memory");
    return NULL;
  }

  ptr = expr;
  while (*ptr && !ctx->error_code) {
    jsonpath_opcode_t *op = NULL;
    consumed = jsonpath_lex(ptr, ctx, &op);
    if (consumed < 0) {
      ctx->error_code = consumed;
      ctx->error_pos = ctx->error_pos ? ctx->error_pos : ctx->off;
      break;
    }

    if (op) JsonPathParse(parser, op->type, op, ctx);

    ptr += consumed;
    ctx->off += consumed;
  }

  if (!ctx->error_code) JsonPathParse(parser, 0, NULL, ctx);

  JsonPathParseFree(parser, free);

  if (ctx->error_code || !ctx->path) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Invalid JSONPath expression at offset %d",
             ctx->error_pos);
    jsonpath_free_ctx(ctx);
    return NULL;
  }

  return ctx;
}

static json_path_program_t *jsonpath_program_fail(jsonpath_parse_ctx_t *ctx,
                                                  const char *message) {
  if (message) snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "%s", message);
  jsonpath_free_ctx(ctx);
  return NULL;
}

static int jsonpath_program_discover(jsonpath_opcode_t *op,
                                     jsonpath_opcode_t **ordered,
                                     size_t *ordered_count,
                                     jsonpath_opcode_t **stack,
                                     size_t *stack_count) {
  if (!op || !ordered || !ordered_count || !stack || !stack_count) return 1;
  if (op->compiled_index_plus_one != 0) return 1;
  if (*ordered_count >= JSONPATH_MAX_INSTRUCTIONS ||
      *ordered_count >= (size_t)UINT32_MAX)
    return 0;
  op->compiled_index_plus_one = ++*ordered_count;
  ordered[*ordered_count - 1U] = op;
  if (*stack_count >= JSONPATH_MAX_INSTRUCTIONS) return 0;
  stack[(*stack_count)++] = op;
  return 1;
}

/* O(tokens + constant bytes) time and O(tokens + constant bytes) owned space. */
static json_path_program_t *jsonpath_program_lower(jsonpath_parse_ctx_t *ctx) {
  jsonpath_opcode_t *op;
  jsonpath_opcode_t **scratch;
  jsonpath_opcode_t **ordered;
  jsonpath_opcode_t **stack;
  json_path_program_t *program;
  size_t pool_count = 0;
  size_t ordered_count = 0;
  size_t stack_count = 0;
  size_t constants = 0;
  size_t i;
  char *constant_ptr;

  if (!ctx || !ctx->path) return jsonpath_program_fail(ctx, "Invalid JSONPath program");

  for (op = ctx->pool; op; op = op->next) {
    if (pool_count == SIZE_MAX) return jsonpath_program_fail(ctx, "JSONPath is too large");
    ++pool_count;
  }
  if (pool_count == 0 || pool_count > JSONPATH_MAX_INSTRUCTIONS ||
      pool_count > SIZE_MAX / (2U * sizeof(*scratch)))
    return jsonpath_program_fail(ctx, "JSONPath exceeds program limits");

  scratch = (jsonpath_opcode_t **)malloc(pool_count * 2U * sizeof(*scratch));
  if (!scratch) return jsonpath_program_fail(ctx, "Out of memory");
  ordered = scratch;
  stack = scratch + pool_count;
  for (op = ctx->pool; op; op = op->next) op->compiled_index_plus_one = 0;

  if (!jsonpath_program_discover(ctx->path, ordered, &ordered_count, stack,
                                 &stack_count)) {
    free(scratch);
    return jsonpath_program_fail(ctx, "JSONPath exceeds program limits");
  }
  while (stack_count > 0) {
    op = stack[--stack_count];
    if (op->down && !jsonpath_program_discover(op->down, ordered, &ordered_count,
                                               stack, &stack_count)) {
      free(scratch);
      return jsonpath_program_fail(ctx, "JSONPath exceeds program limits");
    }
    if (op->sibling &&
        !jsonpath_program_discover(op->sibling, ordered, &ordered_count, stack,
                                   &stack_count)) {
      free(scratch);
      return jsonpath_program_fail(ctx, "JSONPath exceeds program limits");
    }
  }

  for (i = 0; i < ordered_count; ++i) {
    if (!ordered[i]->str) continue;
    size_t len = strlen(ordered[i]->str);
    if (len == SIZE_MAX || constants > SIZE_MAX - len - 1U ||
        constants + len + 1U > JSONPATH_MAX_CONSTANT_BYTES ||
        constants + len + 1U > UINT32_MAX) {
      free(scratch);
      return jsonpath_program_fail(ctx, "JSONPath constants exceed limits");
    }
    constants += len + 1U;
  }

  program = (json_path_program_t *)calloc(1, sizeof(*program));
  if (!program) {
    free(scratch);
    return jsonpath_program_fail(ctx, "Out of memory");
  }
  program->instructions = (jsonpath_instruction_t *)calloc(
      ordered_count, sizeof(*program->instructions));
  if (!program->instructions) {
    free(scratch);
    free(program);
    return jsonpath_program_fail(ctx, "Out of memory");
  }
  if (constants > 0) {
    program->constants = (char *)malloc(constants);
    if (!program->constants) {
      free(scratch);
      free(program->instructions);
      free(program);
      return jsonpath_program_fail(ctx, "Out of memory");
    }
  }

  program->instruction_count = (uint32_t)ordered_count;
  program->constant_bytes = (uint32_t)constants;
  program->entry = ctx->path->down
                       ? (uint32_t)(ctx->path->down->compiled_index_plus_one - 1U)
                       : JSONPATH_NO_INDEX;
  constant_ptr = program->constants;
  for (i = 0; i < ordered_count; ++i) {
    jsonpath_instruction_t *instruction = &program->instructions[i];
    op = ordered[i];
    instruction->type = op->type;
    instruction->num = op->num;
    instruction->number = op->number;
    instruction->down = op->down
                            ? (uint32_t)(op->down->compiled_index_plus_one - 1U)
                            : JSONPATH_NO_INDEX;
    instruction->sibling = op->sibling
                               ? (uint32_t)(op->sibling->compiled_index_plus_one - 1U)
                               : JSONPATH_NO_INDEX;
    instruction->string_offset = UINT32_MAX;
    instruction->string_len = 0;
    if (op->str) {
      size_t len = strlen(op->str);
      instruction->string_offset = (uint32_t)(constant_ptr - program->constants);
      instruction->string_len = (uint32_t)len;
      memcpy(constant_ptr, op->str, len + 1U);
      constant_ptr += len + 1U;
      if (op->type == JSONPATH_TOKEN_LABEL || op->type == JSONPATH_TOKEN_STRING)
        instruction->key_hash = json_object_key_hash(op->str, len);
    }
  }

  free(scratch);
  jsonpath_free_ctx(ctx);
  return program;
}

json_path_program_t *json_path_compile(const char *expr) {
  jsonpath_parse_ctx_t *ctx;
  json_path_program_t *program;
  if (!expr) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Invalid arguments");
    return NULL;
  }
  ctx = jsonpath_parse(expr);
  if (!ctx) return NULL;
  program = jsonpath_program_lower(ctx);
  if (program) g_jsonpath_error[0] = '\0';
  return program;
}

void json_path_program_free(json_path_program_t *program) {
  if (!program) return;
  free(program->constants);
  free(program->instructions);
  free(program);
}

static int jsonpath_result_push(json_path_result_t *result, const json_value_t *value) {
  json_value_t **items;
  size_t new_capacity;

  if (!result || !value) return 1;

  if (result->count == result->capacity) {
    if (result->capacity > SIZE_MAX / 2U) {
      result->error = 1;
      return 0;
    }
    new_capacity = result->capacity ? result->capacity * 2U : 8U;
    if (new_capacity > SIZE_MAX / sizeof(*items)) {
      result->error = 1;
      return 0;
    }
    items = (json_value_t **)realloc(result->items, new_capacity * sizeof(*items));
    if (!items) {
      result->error = 1;
      return 0;
    }
    result->items = items;
    result->capacity = new_capacity;
  }

  result->items[result->count++] = (json_value_t *)value;
  return 1;
}

typedef struct jsonpath_runtime_value_s {
  int type;
  int num;
  double number;
  const char *str;
} jsonpath_runtime_value_t;

static const jsonpath_instruction_t *jsonpath_program_instruction(
    const json_path_program_t *program, uint32_t index) {
  if (!program || index == JSONPATH_NO_INDEX || index >= program->instruction_count)
    return NULL;
  return &program->instructions[index];
}

static const char *jsonpath_program_string(const json_path_program_t *program,
                                           const jsonpath_instruction_t *instruction) {
  if (!program || !instruction || instruction->string_offset == UINT32_MAX ||
      instruction->string_offset >= program->constant_bytes)
    return NULL;
  return program->constants + instruction->string_offset;
}

static int jsonpath_stream_fail(json_path_stream_t *stream, const char *message) {
  if (stream && !stream->failed) {
    snprintf(stream->error, sizeof(stream->error), "%s", message);
    stream->failed = true;
  }
  return -1;
}

static bool jsonpath_stream_instruction_to_segment(
    const jsonpath_instruction_t *instruction, jsonpath_stream_segment_t *segment) {
  if (!instruction || !segment) return false;
  segment->instruction = instruction;
  switch (instruction->type) {
  case JSONPATH_TOKEN_LABEL:
  case JSONPATH_TOKEN_STRING:
    segment->kind = JSONPATH_STREAM_KEY;
    return true;
  case JSONPATH_TOKEN_NUMBER:
    if (instruction->num < 0) return false;
    segment->kind = JSONPATH_STREAM_INDEX;
    return true;
  case JSONPATH_TOKEN_WILDCARD:
    segment->kind = JSONPATH_STREAM_WILDCARD;
    return true;
  default: return false;
  }
}

static bool jsonpath_stream_collect(json_path_stream_t *stream, uint32_t index,
                                    const jsonpath_stream_alternative_t *prefix) {
  const jsonpath_instruction_t *instruction;
  jsonpath_stream_alternative_t next;

  if (index == JSONPATH_NO_INDEX) {
    if (stream->alternative_count >= JSONPATH_STREAM_MAX_ALTERNATIVES) return false;
    stream->alternatives[stream->alternative_count++] = *prefix;
    return true;
  }
  instruction = jsonpath_program_instruction(stream->program, index);
  if (!instruction) return false;

  if (instruction->type == JSONPATH_TOKEN_UNION) {
    uint32_t child = instruction->down;
    if (child == JSONPATH_NO_INDEX) return false;
    while (child != JSONPATH_NO_INDEX) {
      const jsonpath_instruction_t *alternative =
          jsonpath_program_instruction(stream->program, child);
      if (!alternative || prefix->count >= JSONPATH_STREAM_MAX_SEGMENTS) return false;
      next = *prefix;
      if (!jsonpath_stream_instruction_to_segment(
              alternative, &next.segments[next.count++]))
        return false;
      if (!jsonpath_stream_collect(stream, instruction->sibling, &next)) return false;
      child = alternative->sibling;
    }
    return true;
  }

  if (prefix->count >= JSONPATH_STREAM_MAX_SEGMENTS) return false;
  next = *prefix;
  if (!jsonpath_stream_instruction_to_segment(instruction, &next.segments[next.count++]))
    return false;
  return jsonpath_stream_collect(stream, instruction->sibling, &next);
}

static unsigned long long jsonpath_stream_all_alternatives(
    const json_path_stream_t *stream) {
  if (!stream || stream->alternative_count == 0) return 0;
  if (stream->alternative_count == JSONPATH_STREAM_MAX_ALTERNATIVES)
    return ULLONG_MAX;
  return (1ULL << stream->alternative_count) - 1ULL;
}

static bool jsonpath_stream_segment_matches(
    const json_path_stream_t *stream, const jsonpath_stream_segment_t *segment,
    bool object, const char *key, size_t key_len, size_t key_hash, size_t index) {
  const jsonpath_instruction_t *instruction;
  const char *expected;
  if (!stream || !segment || !(instruction = segment->instruction)) return false;
  switch (segment->kind) {
  case JSONPATH_STREAM_WILDCARD: return true;
  case JSONPATH_STREAM_INDEX:
    return !object && (size_t)instruction->num == index;
  case JSONPATH_STREAM_KEY:
    expected = jsonpath_program_string(stream->program, instruction);
    return object && expected && instruction->key_hash == key_hash &&
           instruction->string_len == key_len &&
           memcmp(expected, key, key_len) == 0;
  }
  return false;
}

static unsigned long long jsonpath_stream_child_active(
    const json_path_stream_t *stream, unsigned long long parent_active,
    size_t segment_index, bool object, const char *key, size_t key_len,
    size_t key_hash, size_t index) {
  unsigned long long active = 0;
  for (size_t i = 0; i < stream->alternative_count; ++i) {
    const unsigned long long bit = 1ULL << i;
    const jsonpath_stream_alternative_t *alternative = &stream->alternatives[i];
    if (!(parent_active & bit) || segment_index >= alternative->count) continue;
    if (jsonpath_stream_segment_matches(stream, &alternative->segments[segment_index],
                                        object, key, key_len, key_hash, index))
      active |= bit;
  }
  return active;
}

static unsigned long long jsonpath_stream_selected(
    const json_path_stream_t *stream, unsigned long long active, size_t path_depth) {
  unsigned long long selected = 0;
  for (size_t i = 0; i < stream->alternative_count; ++i) {
    const unsigned long long bit = 1ULL << i;
    if ((active & bit) && stream->alternatives[i].count == path_depth) selected |= bit;
  }
  return selected;
}

static int jsonpath_stream_callback_failed(json_path_stream_t *stream) {
  return jsonpath_stream_fail(stream, "JSONPath stream callback failed");
}

static int jsonpath_stream_match_start(json_path_stream_t *stream,
                                       unsigned long long selected,
                                       json_type_t type) {
  if (!selected || !stream->handler.on_match_start) return 0;
  return stream->handler.on_match_start(stream->ctx, type) == 0
             ? 0
             : jsonpath_stream_callback_failed(stream);
}

static int jsonpath_stream_match_end(json_path_stream_t *stream,
                                     unsigned long long selected,
                                     json_type_t type) {
  if (!selected) return 0;
  if (stream->handler.on_match_end &&
      stream->handler.on_match_end(stream->ctx, type) != 0)
    return jsonpath_stream_callback_failed(stream);
  ++stream->matches;
  return 0;
}

static int jsonpath_stream_prepare_value(json_path_stream_t *stream,
                                         unsigned long long *active,
                                         unsigned long long *selected,
                                         bool *forward, size_t *path_depth) {
  jsonpath_stream_frame_t *parent;
  if (!stream || !active || !selected || !forward || !path_depth)
    return jsonpath_stream_fail(stream, "Invalid JSONPath stream state");

  if (stream->depth == 0) {
    if (stream->root_started)
      return jsonpath_stream_fail(stream, "Unexpected second JSON root value");
    stream->root_started = true;
    *active = jsonpath_stream_all_alternatives(stream);
    *path_depth = 0;
    *selected = jsonpath_stream_selected(stream, *active, *path_depth);
    *forward = *selected != 0;
    return 0;
  }

  parent = &stream->frames[stream->depth - 1U];
  *path_depth = parent->path_depth + 1U;
  if (parent->object) {
    if (!stream->pending_valid)
      return jsonpath_stream_fail(stream, "Object value has no matching key state");
    *active = stream->pending_active;
    stream->pending_valid = false;
  } else {
    if (parent->index == SIZE_MAX)
      return jsonpath_stream_fail(stream, "JSON array index overflow");
    *active = jsonpath_stream_child_active(
        stream, parent->active, parent->path_depth, false, NULL, 0, 0,
        parent->index++);
  }
  *selected = jsonpath_stream_selected(stream, *active, *path_depth);
  *forward = parent->forward || *selected != 0;
  return 0;
}

static int jsonpath_stream_container_start(json_path_stream_t *stream,
                                           json_type_t type) {
  unsigned long long active;
  unsigned long long selected;
  bool forward;
  size_t path_depth;
  jsonpath_stream_frame_t *frame;
  int callback_result = 0;

  if (stream->depth >= JSONPATH_STREAM_MAX_DEPTH)
    return jsonpath_stream_fail(stream, "JSONPath stream depth exceeded");
  if (jsonpath_stream_prepare_value(stream, &active, &selected, &forward,
                                    &path_depth) != 0)
    return -1;
  if (jsonpath_stream_match_start(stream, selected, type) != 0) return -1;
  if (forward) {
    if (type == JSON_OBJECT && stream->handler.events.on_object_start)
      callback_result = stream->handler.events.on_object_start(stream->ctx);
    else if (type == JSON_ARRAY && stream->handler.events.on_array_start)
      callback_result = stream->handler.events.on_array_start(stream->ctx);
  }
  if (callback_result != 0) return jsonpath_stream_callback_failed(stream);

  frame = &stream->frames[stream->depth++];
  memset(frame, 0, sizeof(*frame));
  frame->active = active;
  frame->selected = selected;
  frame->forward = forward;
  frame->object = type == JSON_OBJECT;
  frame->path_depth = path_depth;
  return 0;
}

static int jsonpath_stream_container_end(json_path_stream_t *stream,
                                         json_type_t type) {
  jsonpath_stream_frame_t frame;
  int callback_result = 0;
  if (!stream || stream->depth == 0)
    return jsonpath_stream_fail(stream, "Unexpected JSON container end");
  frame = stream->frames[stream->depth - 1U];
  if (frame.object != (type == JSON_OBJECT))
    return jsonpath_stream_fail(stream, "Mismatched JSON container end");
  if (frame.forward) {
    if (type == JSON_OBJECT && stream->handler.events.on_object_end)
      callback_result = stream->handler.events.on_object_end(stream->ctx);
    else if (type == JSON_ARRAY && stream->handler.events.on_array_end)
      callback_result = stream->handler.events.on_array_end(stream->ctx);
  }
  if (callback_result != 0) return jsonpath_stream_callback_failed(stream);
  if (jsonpath_stream_match_end(stream, frame.selected, type) != 0) return -1;
  --stream->depth;
  return 0;
}

static int jsonpath_stream_scalar_start(json_path_stream_t *stream,
                                        json_type_t type,
                                        unsigned long long *selected,
                                        bool *forward) {
  unsigned long long active;
  size_t path_depth;
  if (jsonpath_stream_prepare_value(stream, &active, selected, forward,
                                    &path_depth) != 0)
    return -1;
  return jsonpath_stream_match_start(stream, *selected, type);
}

static int jsonpath_stream_on_null(void *ctx) {
  json_path_stream_t *stream = (json_path_stream_t *)ctx;
  unsigned long long selected;
  bool forward;
  if (jsonpath_stream_scalar_start(stream, JSON_NULL, &selected, &forward) != 0)
    return -1;
  if (forward && stream->handler.events.on_null &&
      stream->handler.events.on_null(stream->ctx) != 0)
    return jsonpath_stream_callback_failed(stream);
  return jsonpath_stream_match_end(stream, selected, JSON_NULL);
}

static int jsonpath_stream_on_bool(void *ctx, bool value) {
  json_path_stream_t *stream = (json_path_stream_t *)ctx;
  unsigned long long selected;
  bool forward;
  if (jsonpath_stream_scalar_start(stream, JSON_BOOL, &selected, &forward) != 0)
    return -1;
  if (forward && stream->handler.events.on_bool &&
      stream->handler.events.on_bool(stream->ctx, value) != 0)
    return jsonpath_stream_callback_failed(stream);
  return jsonpath_stream_match_end(stream, selected, JSON_BOOL);
}

static int jsonpath_stream_on_number(void *ctx, const char *value, size_t len) {
  json_path_stream_t *stream = (json_path_stream_t *)ctx;
  unsigned long long selected;
  bool forward;
  if (jsonpath_stream_scalar_start(stream, JSON_NUMBER, &selected, &forward) != 0)
    return -1;
  if (forward && stream->handler.events.on_number &&
      stream->handler.events.on_number(stream->ctx, value, len) != 0)
    return jsonpath_stream_callback_failed(stream);
  return jsonpath_stream_match_end(stream, selected, JSON_NUMBER);
}

static int jsonpath_stream_on_string(void *ctx, const char *value, size_t len) {
  json_path_stream_t *stream = (json_path_stream_t *)ctx;
  unsigned long long selected;
  bool forward;
  if (jsonpath_stream_scalar_start(stream, JSON_STRING, &selected, &forward) != 0)
    return -1;
  if (forward && stream->handler.events.on_string &&
      stream->handler.events.on_string(stream->ctx, value, len) != 0)
    return jsonpath_stream_callback_failed(stream);
  return jsonpath_stream_match_end(stream, selected, JSON_STRING);
}

static int jsonpath_stream_on_object_start(void *ctx) {
  return jsonpath_stream_container_start((json_path_stream_t *)ctx, JSON_OBJECT);
}

static int jsonpath_stream_on_array_start(void *ctx) {
  return jsonpath_stream_container_start((json_path_stream_t *)ctx, JSON_ARRAY);
}

static int jsonpath_stream_on_object_key(void *ctx, const char *key, size_t len) {
  json_path_stream_t *stream = (json_path_stream_t *)ctx;
  jsonpath_stream_frame_t *frame;
  if (!stream || stream->depth == 0)
    return jsonpath_stream_fail(stream, "Object key outside an object");
  frame = &stream->frames[stream->depth - 1U];
  if (!frame->object)
    return jsonpath_stream_fail(stream, "Object key inside an array");
  if (frame->forward && stream->handler.events.on_object_key &&
      stream->handler.events.on_object_key(stream->ctx, key, len) != 0)
    return jsonpath_stream_callback_failed(stream);
  stream->pending_active = jsonpath_stream_child_active(
      stream, frame->active, frame->path_depth, true, key, len,
      json_object_key_hash(key, len), 0);
  stream->pending_valid = true;
  return 0;
}

static int jsonpath_stream_on_object_end(void *ctx) {
  return jsonpath_stream_container_end((json_path_stream_t *)ctx, JSON_OBJECT);
}

static int jsonpath_stream_on_array_end(void *ctx) {
  return jsonpath_stream_container_end((json_path_stream_t *)ctx, JSON_ARRAY);
}

json_path_stream_t *json_path_stream_create(
    const json_path_program_t *program,
    const json_path_stream_handler_t *handler, void *ctx) {
  const jsonpath_stream_alternative_t empty = {0};
  const json_sax_handler_raw_t sax_handler = {
      .on_null = jsonpath_stream_on_null,
      .on_bool = jsonpath_stream_on_bool,
      .on_number = jsonpath_stream_on_number,
      .on_string = jsonpath_stream_on_string,
      .on_object_start = jsonpath_stream_on_object_start,
      .on_object_key = jsonpath_stream_on_object_key,
      .on_object_end = jsonpath_stream_on_object_end,
      .on_array_start = jsonpath_stream_on_array_start,
      .on_array_end = jsonpath_stream_on_array_end,
  };
  json_path_stream_t *stream;

  if (!program || !handler) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Invalid arguments");
    return NULL;
  }
  stream = (json_path_stream_t *)calloc(1, sizeof(*stream));
  if (!stream) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Out of memory");
    return NULL;
  }
  stream->program = program;
  stream->handler = *handler;
  stream->ctx = ctx;
  if (!jsonpath_stream_collect(stream, program->entry, &empty) ||
      stream->alternative_count == 0) {
    free(stream);
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error),
             "JSONPath expression is not streamable; use the DOM query API");
    return NULL;
  }
  stream->parser = json_sax_parser_create_raw(&sax_handler, stream);
  if (!stream->parser) {
    free(stream);
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error),
             "Unable to create JSONPath stream parser");
    return NULL;
  }
  g_jsonpath_error[0] = '\0';
  return stream;
}

int json_path_stream_feed(json_path_stream_t *stream, const char *data, size_t len) {
  int result;
  if (!stream || (!data && len > 0)) {
    if (stream) jsonpath_stream_fail(stream, "Invalid arguments");
    return -1;
  }
  if (stream->failed) return -1;
  result = json_sax_parser_feed(stream->parser, data, len);
  if (result != 0 && !stream->failed) {
    const char *error = json_sax_parser_error(stream->parser);
    jsonpath_stream_fail(stream, error && *error ? error : "JSON stream parse failed");
  }
  return result;
}

int json_path_stream_finish(json_path_stream_t *stream) {
  int result;
  if (!stream) return -1;
  if (stream->failed) return -1;
  result = json_sax_parser_finish(stream->parser);
  if (result != 0 && !stream->failed) {
    const char *error = json_sax_parser_error(stream->parser);
    jsonpath_stream_fail(stream, error && *error ? error : "JSON stream finish failed");
  }
  return result;
}

size_t json_path_stream_match_count(const json_path_stream_t *stream) {
  return stream ? stream->matches : 0;
}

const char *json_path_stream_error(const json_path_stream_t *stream) {
  if (!stream) return g_jsonpath_error[0] ? g_jsonpath_error : NULL;
  return stream->error[0] ? stream->error : NULL;
}

void json_path_stream_destroy(json_path_stream_t *stream) {
  if (!stream) return;
  json_sax_parser_destroy(stream->parser);
  free(stream);
}

static const json_value_t *jsonpath_program_match_next(
    const json_path_program_t *program, uint32_t index, const json_value_t *root,
    const json_value_t *cur, json_path_result_t *result, int first_only);

static int jsonpath_program_value_to_runtime(const json_value_t *value,
                                             jsonpath_runtime_value_t *out) {
  if (!value || !out) return 0;
  memset(out, 0, sizeof(*out));
  switch (json_type(value)) {
  case JSON_BOOL:
    out->type = JSONPATH_TOKEN_BOOL;
    out->num = json_bool(value) ? 1 : 0;
    out->number = (double)out->num;
    return 1;
  case JSON_NUMBER:
    out->type = JSONPATH_TOKEN_NUMBER;
    out->number = json_number(value);
    out->num = (int)out->number;
    return 1;
  case JSON_STRING:
    out->type = JSONPATH_TOKEN_STRING;
    out->str = json_string(value);
    return 1;
  default:
    return 0;
  }
}

static const json_value_t *jsonpath_program_eval_path(
    const json_path_program_t *program, uint32_t index, const json_value_t *root,
    const json_value_t *cur) {
  const jsonpath_instruction_t *instruction =
      jsonpath_program_instruction(program, index);
  if (!instruction) return NULL;
  if (instruction->type == JSONPATH_TOKEN_ROOT)
    return jsonpath_program_match_next(program, instruction->down, root, root, NULL, 1);
  if (instruction->type == JSONPATH_TOKEN_THIS)
    return jsonpath_program_match_next(program, instruction->down, root, cur, NULL, 1);
  return NULL;
}

static int jsonpath_program_resolve(const json_path_program_t *program, uint32_t index,
                                    const json_value_t *root, const json_value_t *cur,
                                    jsonpath_runtime_value_t *out) {
  const jsonpath_instruction_t *instruction =
      jsonpath_program_instruction(program, index);
  const json_value_t *value;
  if (!instruction || !out) return 0;
  if (instruction->type == JSONPATH_TOKEN_ROOT || instruction->type == JSONPATH_TOKEN_THIS) {
    value = jsonpath_program_eval_path(program, index, root, cur);
    return jsonpath_program_value_to_runtime(value, out);
  }
  memset(out, 0, sizeof(*out));
  out->type = instruction->type;
  out->num = instruction->num;
  out->number = instruction->number;
  out->str = jsonpath_program_string(program, instruction);
  return 1;
}

static int jsonpath_program_compare(const json_path_program_t *program, uint32_t index,
                                    const json_value_t *root, const json_value_t *cur) {
  const jsonpath_instruction_t *instruction =
      jsonpath_program_instruction(program, index);
  const jsonpath_instruction_t *left_instruction;
  jsonpath_runtime_value_t left;
  jsonpath_runtime_value_t right;
  int cmp = 0;
  if (!instruction || instruction->down == JSONPATH_NO_INDEX ||
      !(left_instruction = jsonpath_program_instruction(program, instruction->down)) ||
      left_instruction->sibling == JSONPATH_NO_INDEX ||
      !jsonpath_program_resolve(program, instruction->down, root, cur, &left) ||
      !jsonpath_program_resolve(program, left_instruction->sibling, root, cur, &right) ||
      left.type != right.type)
    return 0;

  switch (left.type) {
  case JSONPATH_TOKEN_BOOL:
    cmp = left.num - right.num;
    break;
  case JSONPATH_TOKEN_NUMBER:
    if (left.number < right.number) cmp = -1;
    else if (left.number > right.number) cmp = 1;
    break;
  case JSONPATH_TOKEN_STRING:
    cmp = strcmp(left.str ? left.str : "", right.str ? right.str : "");
    break;
  default:
    return 0;
  }

  switch (instruction->type) {
  case JSONPATH_TOKEN_EQ: return cmp == 0;
  case JSONPATH_TOKEN_NE: return cmp != 0;
  case JSONPATH_TOKEN_LT: return cmp < 0;
  case JSONPATH_TOKEN_LE: return cmp <= 0;
  case JSONPATH_TOKEN_GT: return cmp > 0;
  case JSONPATH_TOKEN_GE: return cmp >= 0;
  default: return 0;
  }
}

static void jsonpath_program_value_to_string(const jsonpath_runtime_value_t *value,
                                             char *buf, size_t len) {
  if (!value || !buf || len == 0) return;
  switch (value->type) {
  case JSONPATH_TOKEN_BOOL: snprintf(buf, len, "%s", value->num ? "true" : "false"); break;
  case JSONPATH_TOKEN_NUMBER: snprintf(buf, len, "%.17g", value->number); break;
  case JSONPATH_TOKEN_STRING:
  case JSONPATH_TOKEN_REGEXP: snprintf(buf, len, "%s", value->str ? value->str : ""); break;
  default: buf[0] = '\0'; break;
  }
}

static int jsonpath_program_match_like(const json_path_program_t *program, uint32_t index,
                                       const json_value_t *root, const json_value_t *cur) {
  const jsonpath_instruction_t *instruction =
      jsonpath_program_instruction(program, index);
  const jsonpath_instruction_t *left_instruction;
  jsonpath_runtime_value_t left;
  jsonpath_runtime_value_t right;
  char left_buf[64];
  char right_buf[64];
  const char *haystack;
  const char *needle;
  if (!instruction || instruction->down == JSONPATH_NO_INDEX ||
      !(left_instruction = jsonpath_program_instruction(program, instruction->down)) ||
      left_instruction->sibling == JSONPATH_NO_INDEX ||
      !jsonpath_program_resolve(program, instruction->down, root, cur, &left) ||
      !jsonpath_program_resolve(program, left_instruction->sibling, root, cur, &right))
    return 0;
  jsonpath_program_value_to_string(&left, left_buf, sizeof(left_buf));
  jsonpath_program_value_to_string(&right, right_buf, sizeof(right_buf));
  haystack = left.str && left.type == JSONPATH_TOKEN_STRING ? left.str : left_buf;
  needle = right.str && (right.type == JSONPATH_TOKEN_STRING ||
                         right.type == JSONPATH_TOKEN_REGEXP)
               ? right.str
               : right_buf;
  if (left.type == JSONPATH_TOKEN_REGEXP) {
    haystack = right.str && right.type == JSONPATH_TOKEN_STRING ? right.str : right_buf;
    needle = left.str ? left.str : "";
  }
  return strstr(haystack ? haystack : "", needle ? needle : "") != NULL;
}

static int jsonpath_program_expr(const json_path_program_t *program, uint32_t index,
                                 const json_value_t *root, const json_value_t *cur,
                                 int array_index, const char *key) {
  const jsonpath_instruction_t *instruction =
      jsonpath_program_instruction(program, index);
  uint32_t child;
  if (!instruction) return 0;
  switch (instruction->type) {
  case JSONPATH_TOKEN_WILDCARD: return 1;
  case JSONPATH_TOKEN_EQ:
  case JSONPATH_TOKEN_NE:
  case JSONPATH_TOKEN_LT:
  case JSONPATH_TOKEN_LE:
  case JSONPATH_TOKEN_GT:
  case JSONPATH_TOKEN_GE:
    return jsonpath_program_compare(program, index, root, cur);
  case JSONPATH_TOKEN_MATCH:
    return jsonpath_program_match_like(program, index, root, cur);
  case JSONPATH_TOKEN_ROOT:
  case JSONPATH_TOKEN_THIS:
    return jsonpath_program_eval_path(program, index, root, cur) != NULL;
  case JSONPATH_TOKEN_NOT:
    return instruction->down != JSONPATH_NO_INDEX &&
           !jsonpath_program_expr(program, instruction->down, root, cur, array_index, key);
  case JSONPATH_TOKEN_AND:
    for (child = instruction->down; child != JSONPATH_NO_INDEX;) {
      if (!jsonpath_program_expr(program, child, root, cur, array_index, key)) return 0;
      child = jsonpath_program_instruction(program, child)->sibling;
    }
    return 1;
  case JSONPATH_TOKEN_OR:
  case JSONPATH_TOKEN_UNION:
    for (child = instruction->down; child != JSONPATH_NO_INDEX;) {
      if (jsonpath_program_expr(program, child, root, cur, array_index, key)) return 1;
      child = jsonpath_program_instruction(program, child)->sibling;
    }
    return 0;
  case JSONPATH_TOKEN_STRING: {
    const char *text = jsonpath_program_string(program, instruction);
    return key && text && instruction->string_len == strlen(key) &&
           memcmp(text, key, instruction->string_len) == 0;
  }
  case JSONPATH_TOKEN_NUMBER: return array_index == instruction->num;
  default: return 0;
  }
}

static const json_value_t *jsonpath_program_match_expr(
    const json_path_program_t *program, uint32_t index, const json_value_t *root,
    const json_value_t *cur, json_path_result_t *result, int first_only) {
  const jsonpath_instruction_t *instruction =
      jsonpath_program_instruction(program, index);
  const json_value_t *matched = NULL;
  size_t count;
  size_t i;
  if (!instruction || !cur) return NULL;
  if (json_type(cur) == JSON_OBJECT) {
    count = json_object_size(cur);
    for (i = 0; i < count; ++i) {
      const char *key = json_object_key(cur, i);
      const json_value_t *value = json_object_value(cur, i);
      if (jsonpath_program_expr(program, index, root, value, -1, key)) {
        matched = jsonpath_program_match_next(program, instruction->sibling, root, value,
                                              result, first_only);
        if (matched && first_only) return matched;
      }
    }
  } else if (json_type(cur) == JSON_ARRAY) {
    count = json_array_size(cur);
    for (i = 0; i < count; ++i) {
      const json_value_t *value = json_array_get(cur, i);
      if (jsonpath_program_expr(program, index, root, value, (int)i, NULL)) {
        matched = jsonpath_program_match_next(program, instruction->sibling, root, value,
                                              result, first_only);
        if (matched && first_only) return matched;
      }
    }
  }
  return matched;
}

static const json_value_t *jsonpath_program_match_next(
    const json_path_program_t *program, uint32_t index, const json_value_t *root,
    const json_value_t *cur, json_path_result_t *result, int first_only) {
  const jsonpath_instruction_t *instruction;
  const json_value_t *next;
  int array_index;
  size_t len;
  if (!cur) return NULL;
  if (index == JSONPATH_NO_INDEX) {
    jsonpath_result_push(result, cur);
    return cur;
  }
  instruction = jsonpath_program_instruction(program, index);
  if (!instruction) return NULL;
  switch (instruction->type) {
  case JSONPATH_TOKEN_STRING:
  case JSONPATH_TOKEN_LABEL: {
    const char *key = jsonpath_program_string(program, instruction);
    if (!key) return NULL;
    next = json_object_get_hashed_v(
        cur, tstr_v_from_buf(key, instruction->string_len), instruction->key_hash);
    return next ? jsonpath_program_match_next(program, instruction->sibling, root, next,
                                              result, first_only)
                : NULL;
  }
  case JSONPATH_TOKEN_NUMBER:
    if (json_type(cur) != JSON_ARRAY) return NULL;
    array_index = instruction->num;
    len = json_array_size(cur);
    if (array_index < 0) array_index += (int)len;
    if (array_index < 0 || (size_t)array_index >= len) return NULL;
    next = json_array_get(cur, (size_t)array_index);
    return jsonpath_program_match_next(program, instruction->sibling, root, next, result,
                                       first_only);
  default:
    return jsonpath_program_match_expr(program, index, root, cur, result, first_only);
  }
}

json_path_result_t *json_path_query_compiled(const json_value_t *root,
                                             const json_path_program_t *program) {
  json_path_result_t *result;
  if (!root || !program) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Invalid arguments");
    return NULL;
  }
  result = (json_path_result_t *)calloc(1, sizeof(*result));
  if (!result) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Out of memory");
    return NULL;
  }
  jsonpath_program_match_next(program, program->entry, root, root, result, 0);
  if (result->error) {
    json_path_result_free(result);
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Out of memory");
    return NULL;
  }
  g_jsonpath_error[0] = '\0';
  return result;
}

json_value_t *json_path_get_compiled(const json_value_t *root,
                                     const json_path_program_t *program) {
  const json_value_t *value;
  if (!root || !program) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Invalid arguments");
    return NULL;
  }
  value = jsonpath_program_match_next(program, program->entry, root, root, NULL, 1);
  g_jsonpath_error[0] = '\0';
  return (json_value_t *)value;
}

json_path_result_t *json_path_query(const json_value_t *root, const char *expr) {
  json_path_program_t *program;
  json_path_result_t *result;

  if (!root || !expr) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Invalid arguments");
    return NULL;
  }

  program = json_path_compile(expr);
  if (!program) return NULL;
  result = json_path_query_compiled(root, program);
  json_path_program_free(program);
  return result;
}

json_value_t *json_path_get(const json_value_t *root, const char *expr) {
  json_path_program_t *program;
  json_value_t *value;
  if (!root || !expr) {
    snprintf(g_jsonpath_error, sizeof(g_jsonpath_error), "Invalid arguments");
    return NULL;
  }
  program = json_path_compile(expr);
  if (!program) return NULL;
  value = json_path_get_compiled(root, program);
  json_path_program_free(program);
  return value;
}

size_t json_path_result_size(const json_path_result_t *result) {
  return result ? result->count : 0;
}

json_value_t *json_path_result_get(const json_path_result_t *result, size_t index) {
  if (!result || index >= result->count) return NULL;
  return result->items[index];
}

void json_path_result_free(json_path_result_t *result) {
  if (!result) return;
  free(result->items);
  free(result);
}

const char *json_path_get_error(void) { return g_jsonpath_error[0] ? g_jsonpath_error : NULL; }
