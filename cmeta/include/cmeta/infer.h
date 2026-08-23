#ifndef CMETA_INFER_H
#define CMETA_INFER_H

#include <cmeta/pp.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { CMETA_INFER_MAX_ARITY = 3u };

typedef uint32_t cmeta_infer_symbol;
typedef uint32_t cmeta_infer_value;

#ifdef __cplusplus
#define CMETA_INFER_SIZE(value) static_cast<size_t>(value)
#define CMETA_INFER_SYMBOL(value) static_cast<cmeta_infer_symbol>(value)
#define CMETA_INFER_VALUE(value) static_cast<cmeta_infer_value>(value)
#else
#define CMETA_INFER_SIZE(value) ((size_t)(value))
#define CMETA_INFER_SYMBOL(value) ((cmeta_infer_symbol)(value))
#define CMETA_INFER_VALUE(value) ((cmeta_infer_value)(value))
#endif

typedef enum cmeta_infer_status {
    CMETA_INFER_OK = 0,
    CMETA_INFER_INVALID_ARGUMENT,
    CMETA_INFER_CAPACITY_EXCEEDED,
    CMETA_INFER_DUPLICATE_RULE,
    CMETA_INFER_AMBIGUOUS_RULE,
    CMETA_INFER_NO_RULE
} cmeta_infer_status;

typedef struct cmeta_infer_rule {
    cmeta_infer_symbol symbols[CMETA_INFER_MAX_ARITY];
    cmeta_infer_value result;
} cmeta_infer_rule;

typedef struct cmeta_infer_relation {
    const cmeta_infer_rule *rules;
    size_t rule_count;
    size_t arity;
} cmeta_infer_relation;

typedef struct cmeta_infer_state {
    cmeta_infer_value result;
    bool accepting;
} cmeta_infer_state;

typedef struct cmeta_infer_transition {
    size_t from;
    cmeta_infer_symbol symbol;
    size_t to;
} cmeta_infer_transition;

/* DFA owns no storage. The caller retains both workspaces until all reads end.
 * Reinitialization or rebuild is a control-plane operation and requires no
 * concurrent evaluators. A successfully built DFA is immutable to evaluators. */
typedef struct cmeta_infer_dfa {
    cmeta_infer_state *states;
    size_t state_capacity;
    size_t state_count;
    cmeta_infer_transition *transitions;
    size_t transition_capacity;
    size_t transition_count;
    size_t arity;
    bool built;
} cmeta_infer_dfa;

#define CMETA_INFER_STATE_BOUND(rule_count, arity) \
    (1u + CMETA_INFER_SIZE(rule_count) * CMETA_INFER_SIZE(arity))
#define CMETA_INFER_TRANSITION_BOUND(rule_count, arity) \
    (CMETA_INFER_SIZE(rule_count) * CMETA_INFER_SIZE(arity))

void cmeta_infer_dfa_init(cmeta_infer_dfa *dfa,
                          cmeta_infer_state *states,
                          size_t state_capacity,
                          cmeta_infer_transition *transitions,
                          size_t transition_capacity);

cmeta_infer_status cmeta_infer_dfa_build(
    cmeta_infer_dfa *dfa, const cmeta_infer_relation *relation);

cmeta_infer_status cmeta_infer_dfa_eval(
    const cmeta_infer_dfa *dfa,
    const cmeta_infer_symbol *symbols,
    size_t symbol_count,
    cmeta_infer_value *out_result);

const char *cmeta_infer_status_string(cmeta_infer_status status);

#ifdef __cplusplus
}
#endif

#define CMETA_INFER_RULE_1(row, ignored) \
    CMETA_INFER_RULE_1_I(CMETA_PP_UNPAREN row)
#define CMETA_INFER_RULE_1_I(...) CMETA_INFER_RULE_1_II(__VA_ARGS__)
#define CMETA_INFER_RULE_1_II(first, result) \
    { { CMETA_INFER_SYMBOL(first), 0u, 0u }, CMETA_INFER_VALUE(result) },

#define CMETA_INFER_RULE_2(row, ignored) \
    CMETA_INFER_RULE_2_I(CMETA_PP_UNPAREN row)
#define CMETA_INFER_RULE_2_I(...) CMETA_INFER_RULE_2_II(__VA_ARGS__)
#define CMETA_INFER_RULE_2_II(first, second, result) \
    { { CMETA_INFER_SYMBOL(first), CMETA_INFER_SYMBOL(second), 0u }, \
      CMETA_INFER_VALUE(result) },

#define CMETA_INFER_RULE_3(row, ignored) \
    CMETA_INFER_RULE_3_I(CMETA_PP_UNPAREN row)
#define CMETA_INFER_RULE_3_I(...) CMETA_INFER_RULE_3_II(__VA_ARGS__)
#define CMETA_INFER_RULE_3_II(first, second, third, result) \
    { { CMETA_INFER_SYMBOL(first), CMETA_INFER_SYMBOL(second), \
        CMETA_INFER_SYMBOL(third) }, \
      CMETA_INFER_VALUE(result) },

#define CMETA_INFER_ROWS_NAME_I(name) name##_cmeta_infer_rows
#define CMETA_INFER_ROWS_NAME(name) CMETA_INFER_ROWS_NAME_I(name)
#define CMETA_INFER_COUNT_NAME_I(name) name##_cmeta_infer_rule_count
#define CMETA_INFER_COUNT_NAME(name) CMETA_INFER_COUNT_NAME_I(name)
#define CMETA_INFER_ARITY_NAME_I(name) name##_cmeta_infer_rule_arity
#define CMETA_INFER_ARITY_NAME(name) CMETA_INFER_ARITY_NAME_I(name)

#define CMETA_INFERENCE_RULES(name, rule_arity, mapper, ...) \
    static const cmeta_infer_rule CMETA_INFER_ROWS_NAME(name)[] = { \
        CMETA_PP_FOR_EACH_A(mapper, ~, __VA_ARGS__) \
    }; \
    enum { \
        CMETA_INFER_COUNT_NAME(name) = \
            sizeof(CMETA_INFER_ROWS_NAME(name)) / \
            sizeof(CMETA_INFER_ROWS_NAME(name)[0]), \
        CMETA_INFER_ARITY_NAME(name) = (rule_arity) \
    }; \
    static const cmeta_infer_relation name = { \
        CMETA_INFER_ROWS_NAME(name), \
        CMETA_INFER_COUNT_NAME(name), \
        CMETA_INFER_ARITY_NAME(name) \
    }

#ifndef InferenceRules1
#define InferenceRules1(name, ...) \
    CMETA_INFERENCE_RULES(name, 1u, CMETA_INFER_RULE_1, __VA_ARGS__)
#endif
#ifndef InferenceRules2
#define InferenceRules2(name, ...) \
    CMETA_INFERENCE_RULES(name, 2u, CMETA_INFER_RULE_2, __VA_ARGS__)
#endif
#ifndef InferenceRules3
#define InferenceRules3(name, ...) \
    CMETA_INFERENCE_RULES(name, 3u, CMETA_INFER_RULE_3, __VA_ARGS__)
#endif

#ifndef InferenceRuleCount
#define InferenceRuleCount(name) CMETA_INFER_COUNT_NAME(name)
#endif
#ifndef InferenceRuleArity
#define InferenceRuleArity(name) CMETA_INFER_ARITY_NAME(name)
#endif

#endif /* CMETA_INFER_H */
