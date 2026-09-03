#include "ac_automaton.h"
#include "cstl_status_internal.h"
#include <cstl/vec.h>

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct {
  int32_t next[256];
  int32_t fail;
  int32_t outputs;
} ac_byte_node_t;

typedef struct {
  int32_t child;
  uint32_t codepoint;
  int32_t next_edge;
} ac_utf8_edge_t;

typedef struct {
  uint32_t pattern_id;
  uint32_t pattern_len;
  int32_t next_output;
} ac_output_t;

typedef struct {
  int32_t fail;
  int32_t outputs;
  int32_t first_edge;
} ac_utf8_node_t;

/* Node/output indices are signed 32-bit in the public automaton layout. This
 * is the compatibility-preserving hard bound for incremental construction. */
#define AC_AUTOMATON_ENTRY_LIMIT ((size_t)INT32_MAX)

struct ac_automaton_s {
  vec_t nodes;
  vec_t outputs;
  bool initialized;
  bool built;
  uint32_t next_pattern_id;
};

struct ac_utf8_automaton_s {
  vec_t nodes;
  vec_t outputs;
  vec_t edges;
  bool initialized;
  bool built;
  uint32_t next_pattern_id;
};

static int32_t ac_byte_new_node(ac_automaton_t *ac, int *out_error) {
  ac_byte_node_t node;
  memset(node.next, 0xFF, sizeof(node.next));
  node.fail = -1;
  node.outputs = -1;

  stl_status status = vec_push(&ac->nodes, &node);
  if (status != STL_OK) {
    if (out_error) *out_error = salts_core_status_from_stl(status);
    return -1;
  }

  return (int32_t)(vec_size(&ac->nodes) - 1U);
}

static ac_byte_node_t *ac_byte_node_at(ac_automaton_t *ac, int32_t index) {
  return (ac_byte_node_t *)vec_at(&ac->nodes, (size_t)index);
}

static const ac_byte_node_t *ac_byte_node_at_const(const ac_automaton_t *ac, int32_t index) {
  return (const ac_byte_node_t *)vec_at_const(&ac->nodes, (size_t)index);
}

static int32_t ac_new_output(ac_automaton_t *ac, uint32_t pattern_id, uint32_t pattern_len,
                            int32_t head, int *out_error) {
  ac_output_t output;
  output.pattern_id = pattern_id;
  output.pattern_len = pattern_len;
  output.next_output = head;

  stl_status status = vec_push(&ac->outputs, &output);
  if (status != STL_OK) {
    if (out_error) *out_error = salts_core_status_from_stl(status);
    return -1;
  }
  return (int32_t)(vec_size(&ac->outputs) - 1U);
}

static int ac_automaton_init_common(ac_automaton_t *ac) {
  stl_status status;
  int error = SALTS_OK;
  if (!ac) return SALTS_EINVAL;
  if (ac->initialized) return SALTS_EINVAL;

  status = vec_init_bytes(&ac->nodes, sizeof(ac_byte_node_t),
                                _Alignof(ac_byte_node_t), AC_AUTOMATON_ENTRY_LIMIT);
  if (status != STL_OK) return salts_core_status_from_stl(status);
  status = vec_init_bytes(&ac->outputs, sizeof(ac_output_t),
                                _Alignof(ac_output_t), AC_AUTOMATON_ENTRY_LIMIT);
  if (status != STL_OK) {
    vec_destroy(&ac->nodes);
    return salts_core_status_from_stl(status);
  }

  if (ac_byte_new_node(ac, &error) < 0) {
    vec_destroy(&ac->outputs);
    vec_destroy(&ac->nodes);
    return error;
  }

  ac->initialized = true;
  ac->built = false;
  ac->next_pattern_id = 0U;
  return SALTS_OK;
}

int ac_automaton_init(ac_automaton_t *ac) { return ac_automaton_init_common(ac); }

void ac_automaton_destroy(ac_automaton_t *ac) {
  if (!ac || !ac->initialized) return;

  vec_destroy(&ac->nodes);
  vec_destroy(&ac->outputs);
  memset(ac, 0, sizeof(*ac));
}

ac_automaton_t *ac_automaton_create(void) {
  ac_automaton_t *ac = (ac_automaton_t *)calloc(1, sizeof(*ac));
  if (!ac) return NULL;

  if (ac_automaton_init(ac) != SALTS_OK) {
    free(ac);
    return NULL;
  }
  return ac;
}

void ac_automaton_free(ac_automaton_t *ac) {
  if (!ac) return;
  ac_automaton_destroy(ac);
  free(ac);
}

int ac_automaton_add_pattern(ac_automaton_t *ac, vstr pattern, uint32_t *pattern_id) {
  int32_t state;
  uint32_t idx;
  ac_byte_node_t *node = NULL;
  int32_t output_index;
  int error = SALTS_OK;

  if (!ac || !ac->initialized || pattern.len == SIZE_MAX || (pattern.len > 0 && !pattern.data))
    return SALTS_EINVAL;
  if (pattern.len == 0 || pattern.len > UINT32_MAX || ac->next_pattern_id == UINT32_MAX) return SALTS_EINVAL;

  state = 0;
  for (idx = 0; idx < pattern.len; ++idx) {
    unsigned char ch = (unsigned char)pattern.data[idx];
    node = ac_byte_node_at(ac, state);
    if (!node) return SALTS_EINVAL;
    if (node->next[ch] < 0) {
      int32_t next_state = ac_byte_new_node(ac, &error);
      if (next_state < 0) return error;
      node = ac_byte_node_at(ac, state);
      if (!node) return SALTS_EINVAL;
      node->next[ch] = next_state;
    }
    state = node->next[ch];
  }

  if (state < 0) return SALTS_EINVAL;
  output_index = ac_new_output(ac, ac->next_pattern_id, (uint32_t)pattern.len,
                              ac_byte_node_at(ac, state)->outputs, &error);
  if (output_index < 0) return error;
  ac_byte_node_at(ac, state)->outputs = output_index;

  if (pattern_id) *pattern_id = ac->next_pattern_id++;
  else {
    ++ac->next_pattern_id;
  }

  ac->built = false;
  return SALTS_OK;
}

int ac_automaton_build(ac_automaton_t *ac) {
  vec_t queue = {0};
  size_t head = 0;

  if (!ac || !ac->initialized) return SALTS_EINVAL;
  {
    stl_status status = vec_init_bytes(
        &queue, sizeof(uint32_t), _Alignof(uint32_t), vec_size(&ac->nodes));
    if (status != STL_OK) return salts_core_status_from_stl(status);
  }

  for (size_t ch = 0; ch < 256; ++ch) {
    int32_t child = ac_byte_node_at_const(ac, 0)->next[ch];
    if (child >= 0) {
      ac_byte_node_t *child_node = ac_byte_node_at(ac, child);
      if (!child_node) {
        vec_destroy(&queue);
        return SALTS_EINVAL;
      }
      child_node->fail = 0;
      {
        stl_status status = vec_push(&queue, &child);
        if (status != STL_OK) {
          vec_destroy(&queue);
          return salts_core_status_from_stl(status);
        }
      }
    }
  }

  while (head < vec_size(&queue)) {
    uint32_t node_index;
    ac_byte_node_t *node = NULL;
    size_t ch = 0;
    int32_t state = -1;

    if (vec_at(&queue, head) == NULL) {
      vec_destroy(&queue);
      return SALTS_EINVAL;
    }
    node_index = *(uint32_t *)vec_at(&queue, head++);
    node = ac_byte_node_at(ac, (int32_t)node_index);
    if (!node) {
      vec_destroy(&queue);
      return SALTS_EINVAL;
    }

    for (ch = 0; ch < 256; ++ch) {
      int32_t child = node->next[ch];
      if (child < 0) continue;

      state = node->fail;
      while (state >= 0) {
        const ac_byte_node_t *fail_node = ac_byte_node_at_const(ac, state);
        int32_t transition = fail_node ? fail_node->next[ch] : -1;
        if (transition >= 0) break;
        state = fail_node ? fail_node->fail : -1;
      }
      if (state < 0) state = 0;
      else state = ac_byte_node_at(ac, state)->next[ch];

      ac_byte_node_t *child_node = ac_byte_node_at(ac, child);
      if (!child_node) {
        vec_destroy(&queue);
        return SALTS_EINVAL;
      }
      child_node->fail = state;
      {
        stl_status status = vec_push(&queue, &child);
        if (status != STL_OK) {
          vec_destroy(&queue);
          return salts_core_status_from_stl(status);
        }
      }
    }
  }

  vec_destroy(&queue);
  ac->built = true;
  return SALTS_OK;
}

int ac_automaton_match(const ac_automaton_t *ac, vstr text, ac_match_cb cb, void *user_data) {
  int32_t state = 0;

  if (!ac || !ac->initialized || !cb || (text.len != 0U && !text.data)) return SALTS_EINVAL;
  if (!ac->built) return SALTS_EINVAL;

  for (size_t pos = 0; pos < text.len; ++pos) {
    unsigned char ch = (unsigned char)text.data[pos];
    int32_t next = ac_byte_node_at_const(ac, state)->next[ch];

    while (next < 0 && state != 0) {
      state = ac_byte_node_at_const(ac, state)->fail;
      next = ac_byte_node_at_const(ac, state)->next[ch];
    }
    if (next < 0) {
      next = 0;
    }
    state = next;

    for (int32_t out_state = state; out_state >= 0; out_state = ac_byte_node_at_const(ac, out_state)->fail) {
      const ac_output_t *output = NULL;
      for (int32_t out_idx = ac_byte_node_at_const(ac, out_state)->outputs; out_idx >= 0;
           out_idx = output->next_output) {
        output = (const ac_output_t *)vec_at_const(&ac->outputs, (size_t)out_idx);
        if (!output) return SALTS_EINVAL;
        if (output->pattern_len == 0) continue;
        if (output->pattern_len > pos + 1U) continue;
        if (!cb(output->pattern_id, pos + 1U - output->pattern_len, pos + 1U, user_data))
          return SALTS_OK;
      }
    }
  }
  return SALTS_OK;
}

uint32_t ac_automaton_pattern_count(const ac_automaton_t *ac) {
  if (!ac || !ac->initialized) return 0U;
  return ac->next_pattern_id;
}

static ac_utf8_node_t *ac_utf8_node_at(ac_utf8_automaton_t *ac, int32_t index) {
  return (ac_utf8_node_t *)vec_at(&ac->nodes, (size_t)index);
}

static const ac_utf8_node_t *ac_utf8_node_at_const(const ac_utf8_automaton_t *ac, int32_t index) {
  return (const ac_utf8_node_t *)vec_at_const(&ac->nodes, (size_t)index);
}

static const ac_utf8_edge_t *ac_utf8_edge_at_const(const ac_utf8_automaton_t *ac, int32_t index) {
  return (const ac_utf8_edge_t *)vec_at_const(&ac->edges, (size_t)index);
}

static int32_t ac_utf8_new_node(ac_utf8_automaton_t *ac, int *out_error) {
  ac_utf8_node_t node;
  node.fail = -1;
  node.outputs = -1;
  node.first_edge = -1;

  stl_status status = vec_push(&ac->nodes, &node);
  if (status != STL_OK) {
    if (out_error) *out_error = salts_core_status_from_stl(status);
    return -1;
  }
  return (int32_t)(vec_size(&ac->nodes) - 1U);
}

static int32_t ac_utf8_new_edge(ac_utf8_automaton_t *ac, uint32_t cp, int32_t child,
                                int32_t head, int *out_error) {
  ac_utf8_edge_t edge;
  edge.codepoint = cp;
  edge.child = child;
  edge.next_edge = head;

  stl_status status = vec_push(&ac->edges, &edge);
  if (status != STL_OK) {
    if (out_error) *out_error = salts_core_status_from_stl(status);
    return -1;
  }
  return (int32_t)(vec_size(&ac->edges) - 1U);
}

static int32_t ac_utf8_find_child(ac_utf8_automaton_t *ac, int32_t node_index, uint32_t cp) {
  const ac_utf8_node_t *node = ac_utf8_node_at_const(ac, node_index);
  if (!node) return -1;

  for (int32_t edge_idx = node->first_edge; edge_idx >= 0; edge_idx = ac_utf8_edge_at_const(ac, edge_idx)->next_edge) {
    const ac_utf8_edge_t *edge = ac_utf8_edge_at_const(ac, edge_idx);
    if (!edge) return -1;
    if (edge->codepoint == cp) return edge->child;
  }
  return -1;
}

static int32_t ac_utf8_find_child_const(const ac_utf8_automaton_t *ac, int32_t node_index,
                                       uint32_t cp) {
  const ac_utf8_node_t *node = ac_utf8_node_at_const(ac, node_index);
  if (!node) return -1;

  for (int32_t edge_idx = node->first_edge; edge_idx >= 0;
       edge_idx = ac_utf8_edge_at_const(ac, edge_idx)->next_edge) {
    const ac_utf8_edge_t *edge = ac_utf8_edge_at_const(ac, edge_idx);
    if (!edge) return -1;
    if (edge->codepoint == cp) return edge->child;
  }
  return -1;
}

static int32_t ac_utf8_add_child(ac_utf8_automaton_t *ac, int32_t node_index,
                                 uint32_t cp, int *out_error) {
  ac_utf8_node_t *node = ac_utf8_node_at(ac, node_index);
  int32_t first_edge;
  if (!node) return -1;
  int32_t existing = ac_utf8_find_child(ac, node_index, cp);
  if (existing >= 0) return existing;
  first_edge = node->first_edge;

  int32_t child = ac_utf8_new_node(ac, out_error);
  if (child < 0) return -1;

  int32_t edge_idx = ac_utf8_new_edge(ac, cp, child, first_edge, out_error);
  if (edge_idx < 0) {
    return -1;
  }
  /* Adding the child may reallocate the node Vec, invalidating borrowed slots. */
  node = ac_utf8_node_at(ac, node_index);
  if (!node) return -1;
  node->first_edge = edge_idx;
  return child;
}

int ac_utf8_automaton_init(ac_utf8_automaton_t *ac) {
  stl_status status;
  int error = SALTS_OK;
  if (!ac) return SALTS_EINVAL;
  if (ac->initialized) return SALTS_EINVAL;

  status = vec_init_bytes(&ac->nodes, sizeof(ac_utf8_node_t),
                                _Alignof(ac_utf8_node_t), AC_AUTOMATON_ENTRY_LIMIT);
  if (status != STL_OK) return salts_core_status_from_stl(status);
  status = vec_init_bytes(&ac->outputs, sizeof(ac_output_t),
                                _Alignof(ac_output_t), AC_AUTOMATON_ENTRY_LIMIT);
  if (status != STL_OK) {
    vec_destroy(&ac->nodes);
    return salts_core_status_from_stl(status);
  }
  status = vec_init_bytes(&ac->edges, sizeof(ac_utf8_edge_t),
                                _Alignof(ac_utf8_edge_t), AC_AUTOMATON_ENTRY_LIMIT);
  if (status != STL_OK) {
    vec_destroy(&ac->outputs);
    vec_destroy(&ac->nodes);
    return salts_core_status_from_stl(status);
  }

  if (ac_utf8_new_node(ac, &error) < 0) {
    vec_destroy(&ac->edges);
    vec_destroy(&ac->outputs);
    vec_destroy(&ac->nodes);
    return error;
  }

  ac->initialized = true;
  ac->built = false;
  ac->next_pattern_id = 0U;
  return SALTS_OK;
}

ac_utf8_automaton_t *ac_utf8_automaton_create(void) {
  ac_utf8_automaton_t *ac = (ac_utf8_automaton_t *)calloc(1, sizeof(*ac));
  if (!ac) return NULL;

  if (ac_utf8_automaton_init(ac) != SALTS_OK) {
    free(ac);
    return NULL;
  }
  return ac;
}

void ac_utf8_automaton_free(ac_utf8_automaton_t *ac) {
  if (!ac) return;
  ac_utf8_automaton_destroy(ac);
  free(ac);
}

void ac_utf8_automaton_destroy(ac_utf8_automaton_t *ac) {
  if (!ac || !ac->initialized) return;
  vec_destroy(&ac->nodes);
  vec_destroy(&ac->outputs);
  vec_destroy(&ac->edges);
  memset(ac, 0, sizeof(*ac));
}

int ac_utf8_automaton_add_pattern(ac_utf8_automaton_t *ac, vstr pattern, uint32_t *pattern_id) {
  int32_t state = 0;
  vstr rest = pattern;
  uint32_t cp_count = 0U;
  uint32_t cp = 0U;
  int error = SALTS_OK;

  if (!ac || !ac->initialized || pattern.len == SIZE_MAX) return SALTS_EINVAL;
  if (!pattern.data && pattern.len != 0U) return SALTS_EINVAL;
  if (!vstr_utf8_valid(pattern) || pattern.len == 0U || ac->next_pattern_id == UINT32_MAX) {
    return SALTS_EINVAL;
  }

  while (rest.len > 0U) {
    if (cp_count == UINT32_MAX) return SALTS_EINVAL;
    if (!vstr_utf8_next(&rest, &cp)) return SALTS_EINVAL;
    state = ac_utf8_add_child(ac, state, cp, &error);
    if (state < 0) return error;
    ++cp_count;
  }

  ac_utf8_node_t *terminal = ac_utf8_node_at(ac, state);
  if (!terminal) return SALTS_EINVAL;

  {
    stl_status status = vec_push(
        &ac->outputs, &(ac_output_t){.pattern_id = ac->next_pattern_id,
                                    .pattern_len = cp_count,
                                    .next_output = terminal->outputs});
    if (status != STL_OK) {
      return salts_core_status_from_stl(status);
    }
  }
  terminal->outputs = (int32_t)(vec_size(&ac->outputs) - 1U);

  if (pattern_id) *pattern_id = ac->next_pattern_id;
  ++ac->next_pattern_id;
  ac->built = false;
  return SALTS_OK;
}

int ac_utf8_automaton_build(ac_utf8_automaton_t *ac) {
  vec_t queue = {0};
  size_t head = 0;

  if (!ac || !ac->initialized) return SALTS_EINVAL;
  {
    stl_status status = vec_init_bytes(
        &queue, sizeof(uint32_t), _Alignof(uint32_t), vec_size(&ac->nodes));
    if (status != STL_OK) return salts_core_status_from_stl(status);
  }

  for (int32_t edge_idx = ac_utf8_node_at_const(ac, 0U)->first_edge; edge_idx >= 0;
       edge_idx = ac_utf8_edge_at_const(ac, edge_idx)->next_edge) {
    const ac_utf8_edge_t *edge = ac_utf8_edge_at_const(ac, edge_idx);
    if (!edge) {
      vec_destroy(&queue);
      return SALTS_EINVAL;
    }
    ac_utf8_node_t *child = ac_utf8_node_at(ac, edge->child);
    if (!child) {
      vec_destroy(&queue);
      return SALTS_EINVAL;
    }
    child->fail = 0;
    {
      stl_status status = vec_push(&queue, &edge->child);
      if (status != STL_OK) {
        vec_destroy(&queue);
        return salts_core_status_from_stl(status);
      }
    }
  }

  while (head < vec_size(&queue)) {
    uint32_t state;
    const ac_utf8_node_t *node = NULL;
    int32_t state_fail;

    if (vec_at(&queue, head) == NULL) {
      vec_destroy(&queue);
      return SALTS_EINVAL;
    }
    state = *(uint32_t *)vec_at(&queue, head++);
    node = ac_utf8_node_at_const(ac, (int32_t)state);
    if (!node) {
      vec_destroy(&queue);
      return SALTS_EINVAL;
    }

    for (int32_t edge_idx = node->first_edge; edge_idx >= 0;
         edge_idx = ac_utf8_edge_at_const(ac, edge_idx)->next_edge) {
      const ac_utf8_edge_t *edge = ac_utf8_edge_at_const(ac, edge_idx);
      if (!edge) {
        vec_destroy(&queue);
        return SALTS_EINVAL;
      }

      state_fail = node->fail;
      while (state_fail >= 0) {
        const ac_utf8_node_t *fail_node = ac_utf8_node_at_const(ac, state_fail);
        if (!fail_node) {
          vec_destroy(&queue);
          return SALTS_EINVAL;
        }
        int32_t fail_target = ac_utf8_find_child(ac, state_fail, edge->codepoint);
        if (fail_target >= 0) {
          state_fail = fail_target;
          break;
        }
        state_fail = fail_node->fail;
      }
      if (state_fail < 0) state_fail = 0;

      ac_utf8_node_t *child_node = ac_utf8_node_at(ac, edge->child);
      if (!child_node) {
        vec_destroy(&queue);
        return SALTS_EINVAL;
      }
      child_node->fail = (int32_t)state_fail;
      {
        stl_status status = vec_push(&queue, &edge->child);
        if (status != STL_OK) {
          vec_destroy(&queue);
          return salts_core_status_from_stl(status);
        }
      }
    }
  }

  vec_destroy(&queue);
  ac->built = true;
  return SALTS_OK;
}

int ac_utf8_automaton_match(const ac_utf8_automaton_t *ac, vstr text, ac_match_cb cb,
                            void *user_data) {
  int32_t state = 0;
  size_t char_pos = 0;
  vstr rest = text;
  uint32_t cp = 0U;

  if (!ac || !ac->initialized || !cb || !ac->built || (text.len != 0U && !text.data))
    return SALTS_EINVAL;
  if (!vstr_utf8_valid(text)) return SALTS_EINVAL;

  while (rest.len > 0U) {
    if (!vstr_utf8_next(&rest, &cp)) return SALTS_EINVAL;
    int32_t child = ac_utf8_find_child_const(ac, state, cp);
    while (child < 0 && state != 0) {
      state = ac_utf8_node_at_const(ac, state)->fail;
      child = ac_utf8_find_child_const(ac, state, cp);
    }
    if (child < 0) child = 0;
    state = child;

    for (int32_t out_state = state; out_state >= 0; out_state = ac_utf8_node_at_const(ac, out_state)->fail) {
        for (int32_t out_idx = ac_utf8_node_at_const(ac, out_state)->outputs; out_idx >= 0;) {
        const ac_output_t *output = (const ac_output_t *)vec_at_const(&ac->outputs, (size_t)out_idx);
        if (!output) return SALTS_EINVAL;
        if (output->pattern_len != 0U && output->pattern_len <= char_pos + 1U) {
          if (!cb(output->pattern_id, char_pos + 1U - output->pattern_len, char_pos + 1U, user_data))
            return SALTS_OK;
        }
        out_idx = output->next_output;
      }
    }
    ++char_pos;
  }
  return SALTS_OK;
}

uint32_t ac_utf8_automaton_pattern_count(const ac_utf8_automaton_t *ac) {
  if (!ac || !ac->initialized) return 0U;
  return ac->next_pattern_id;
}
