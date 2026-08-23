#include <cmeta/infer.h>
#include <cmeta/meta.h>
#include "tinytest.h"

enum {
    TEST_OP_ADD = 1u,
    TEST_OP_COMPARE = 2u,
    TEST_TYPE_SMALL = 10u,
    TEST_TYPE_WIDE = 11u,
    TEST_TYPE_BOOL = 12u
};

#define TEST_COMMON_TYPE_ROWS \
    (TEST_TYPE_SMALL, TEST_TYPE_SMALL, TEST_TYPE_SMALL), \
    (TEST_TYPE_SMALL, TEST_TYPE_WIDE, TEST_TYPE_WIDE), \
    (TEST_TYPE_WIDE, TEST_TYPE_SMALL, TEST_TYPE_WIDE)

ValueFunction(TestCommonType, TEST_COMMON_TYPE_ROWS);
InferenceRules(test_common_type_relation, TEST_COMMON_TYPE_ROWS);

#define TEST_OPERATION_ROWS \
    (TEST_OP_ADD, TEST_TYPE_SMALL, TEST_TYPE_WIDE, TEST_TYPE_WIDE), \
    (TEST_OP_COMPARE, TEST_TYPE_WIDE, TEST_TYPE_WIDE, TEST_TYPE_BOOL)

InferenceRules(test_operation_relation, TEST_OPERATION_ROWS);

#define TEST_DUPLICATE_ROWS \
    (TEST_TYPE_SMALL, TEST_TYPE_WIDE), \
    (TEST_TYPE_SMALL, TEST_TYPE_WIDE)

InferenceRules(test_duplicate_relation, TEST_DUPLICATE_ROWS);

#define TEST_AMBIGUOUS_ROWS \
    (TEST_TYPE_SMALL, TEST_TYPE_SMALL), \
    (TEST_TYPE_SMALL, TEST_TYPE_WIDE)

InferenceRules(test_ambiguous_relation, TEST_AMBIGUOUS_ROWS);

static cmeta_infer_status build_common_dfa(
    cmeta_infer_dfa *dfa,
    cmeta_infer_state *states,
    size_t state_capacity,
    cmeta_infer_transition *transitions,
    size_t transition_capacity) {
    cmeta_infer_dfa_init(dfa, states, state_capacity,
                         transitions, transition_capacity);
    return cmeta_infer_dfa_build(dfa, &test_common_type_relation);
}

suite("CMeta finite DFA inference") {
    it("projects one row source into compile-time and DFA evaluators") {
        cmeta_infer_state states[
            CMETA_INFER_STATE_BOUND(InferenceRuleCount(test_common_type_relation),
                                    InferenceRuleArity(test_common_type_relation))];
        cmeta_infer_transition transitions[
            CMETA_INFER_TRANSITION_BOUND(InferenceRuleCount(test_common_type_relation),
                                         InferenceRuleArity(test_common_type_relation))];
        cmeta_infer_dfa dfa;
        cmeta_infer_value result = 0u;
        const cmeta_infer_symbol small_small[] = {
            TEST_TYPE_SMALL, TEST_TYPE_SMALL
        };
        const cmeta_infer_symbol small_wide[] = {
            TEST_TYPE_SMALL, TEST_TYPE_WIDE
        };
        const cmeta_infer_symbol wide_small[] = {
            TEST_TYPE_WIDE, TEST_TYPE_SMALL
        };

        check_equal(ValueEval(TestCommonType,
                               TEST_TYPE_SMALL,
                               TEST_TYPE_WIDE), TEST_TYPE_WIDE);
        check_equal(build_common_dfa(
                         &dfa, states, sizeof(states) / sizeof(states[0]),
                         transitions,
                         sizeof(transitions) / sizeof(transitions[0])),
                     CMETA_INFER_OK);
        check_equal(cmeta_infer_dfa_eval(
                         &dfa, small_small, 2u, &result),
                     CMETA_INFER_OK);
        check_equal(result, TEST_TYPE_SMALL);
        check_equal(cmeta_infer_dfa_eval(
                         &dfa, small_wide, 2u, &result),
                     CMETA_INFER_OK);
        check_equal(result, TEST_TYPE_WIDE);
        check_equal(cmeta_infer_dfa_eval(
                         &dfa, wide_small, 2u, &result),
                     CMETA_INFER_OK);
        check_equal(result, TEST_TYPE_WIDE);

        /* Three binary rows need seven states without prefix sharing. */
        check_equal(dfa.state_count, (size_t)6u);
        check_equal(dfa.transition_count, (size_t)5u);
    }

    it("distinguishes a missing rule from invalid input") {
        cmeta_infer_state states[
            CMETA_INFER_STATE_BOUND(InferenceRuleCount(test_common_type_relation),
                                    InferenceRuleArity(test_common_type_relation))];
        cmeta_infer_transition transitions[
            CMETA_INFER_TRANSITION_BOUND(InferenceRuleCount(test_common_type_relation),
                                         InferenceRuleArity(test_common_type_relation))];
        cmeta_infer_dfa dfa;
        cmeta_infer_value result = 99u;
        const cmeta_infer_symbol missing[] = {
            TEST_TYPE_WIDE, TEST_TYPE_WIDE
        };

        check_equal(build_common_dfa(
                         &dfa, states, sizeof(states) / sizeof(states[0]),
                         transitions,
                         sizeof(transitions) / sizeof(transitions[0])),
                     CMETA_INFER_OK);
        check_equal(cmeta_infer_dfa_eval(&dfa, missing, 2u, &result),
                    CMETA_INFER_NO_RULE);
        check_equal(result, 99u);
        check_equal(cmeta_infer_dfa_eval(&dfa, missing, 1u, &result),
                    CMETA_INFER_INVALID_ARGUMENT);
        check_equal(result, 99u);
    }

    it("fails fast when caller workspace is too small") {
        cmeta_infer_state states[1];
        cmeta_infer_transition transitions[1];
        cmeta_infer_dfa dfa;

        cmeta_infer_dfa_init(&dfa, states, 1u, transitions, 1u);
        check_equal(cmeta_infer_dfa_build(
                         &dfa, &test_common_type_relation),
                     CMETA_INFER_CAPACITY_EXCEEDED);
        check_equal(dfa.state_count, (size_t)0u);
        check_equal(dfa.transition_count, (size_t)0u);
    }

    it("rejects duplicate and ambiguous input rows separately") {
        cmeta_infer_state states[5];
        cmeta_infer_transition transitions[4];
        cmeta_infer_dfa dfa;

        cmeta_infer_dfa_init(&dfa, states, 5u, transitions, 4u);
        check_equal(cmeta_infer_dfa_build(
                         &dfa, &test_duplicate_relation),
                     CMETA_INFER_DUPLICATE_RULE);
        check_equal(dfa.state_count, (size_t)0u);

        cmeta_infer_dfa_init(&dfa, states, 5u, transitions, 4u);
        check_equal(cmeta_infer_dfa_build(
                         &dfa, &test_ambiguous_relation),
                     CMETA_INFER_AMBIGUOUS_RULE);
        check_equal(dfa.state_count, (size_t)0u);
    }

    it("supports ternary operation inference") {
        cmeta_infer_state states[
            CMETA_INFER_STATE_BOUND(InferenceRuleCount(test_operation_relation),
                                    InferenceRuleArity(test_operation_relation))];
        cmeta_infer_transition transitions[
            CMETA_INFER_TRANSITION_BOUND(InferenceRuleCount(test_operation_relation),
                                         InferenceRuleArity(test_operation_relation))];
        cmeta_infer_dfa dfa;
        cmeta_infer_value result = 0u;
        const cmeta_infer_symbol query[] = {
            TEST_OP_COMPARE, TEST_TYPE_WIDE, TEST_TYPE_WIDE
        };

        cmeta_infer_dfa_init(
            &dfa, states, sizeof(states) / sizeof(states[0]),
            transitions, sizeof(transitions) / sizeof(transitions[0]));
        check_equal(cmeta_infer_dfa_build(
                         &dfa, &test_operation_relation),
                     CMETA_INFER_OK);
        check_equal(cmeta_infer_dfa_eval(&dfa, query, 3u, &result),
                    CMETA_INFER_OK);
        check_equal(result, TEST_TYPE_BOOL);
    }

    it("names inference failures for boundary diagnostics") {
        check_equal(cmeta_infer_status_string(CMETA_INFER_AMBIGUOUS_RULE),
                    "ambiguous_rule");
    }
}
