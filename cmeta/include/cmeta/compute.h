#ifndef CMETA_COMPUTE_H
#define CMETA_COMPUTE_H

#include <cmeta/pp.h>

#ifdef __cplusplus
#define CMETA_COMPUTE_STATIC_ASSERT(condition, message) \
    static_assert((condition), message)
#else
#define CMETA_COMPUTE_STATIC_ASSERT(condition, message) \
    _Static_assert((condition), message)
#endif

/* Consumes the natural semicolon after a public function declaration. */
#define CMETA_COMPUTE_DECLARATION_END \
    CMETA_COMPUTE_STATIC_ASSERT(1, "CMeta finite function declaration")

/* Finite compile-time functions -------------------------------------------
 *
 * Function names and input keys are stable preprocessor identifiers. Each
 * declaration emits ordinary typedefs or enum constants, so evaluation is a
 * fixed token lookup and an absent row remains a compile-time error.
 */
#define CMETA_COMPUTE_TYPE_KEY_1(function_name, input) \
    CMETA_COMPUTE_TYPE_KEY_1_I(function_name, input)
#define CMETA_COMPUTE_TYPE_KEY_1_I(function_name, input) \
    cmeta_type_fn_##function_name##_1_##input

#define CMETA_COMPUTE_TYPE_KEY_2(function_name, left, right) \
    CMETA_COMPUTE_TYPE_KEY_2_I(function_name, left, right)
#define CMETA_COMPUTE_TYPE_KEY_2_I(function_name, left, right) \
    cmeta_type_fn_##function_name##_2_##left##_##right

#define CMETA_COMPUTE_TYPE_KEY_3(function_name, first, second, third) \
    CMETA_COMPUTE_TYPE_KEY_3_I(function_name, first, second, third)
#define CMETA_COMPUTE_TYPE_KEY_3_I(function_name, first, second, third) \
    cmeta_type_fn_##function_name##_3_##first##_##second##_##third

#define CMETA_COMPUTE_VALUE_KEY_1(function_name, input) \
    CMETA_COMPUTE_VALUE_KEY_1_I(function_name, input)
#define CMETA_COMPUTE_VALUE_KEY_1_I(function_name, input) \
    cmeta_value_fn_##function_name##_1_##input

#define CMETA_COMPUTE_VALUE_KEY_2(function_name, left, right) \
    CMETA_COMPUTE_VALUE_KEY_2_I(function_name, left, right)
#define CMETA_COMPUTE_VALUE_KEY_2_I(function_name, left, right) \
    cmeta_value_fn_##function_name##_2_##left##_##right

#define CMETA_COMPUTE_VALUE_KEY_3(function_name, first, second, third) \
    CMETA_COMPUTE_VALUE_KEY_3_I(function_name, first, second, third)
#define CMETA_COMPUTE_VALUE_KEY_3_I(function_name, first, second, third) \
    cmeta_value_fn_##function_name##_3_##first##_##second##_##third

#define CMETA_COMPUTE_TYPE_FUNCTION_1_ROW(row, function_name) \
    CMETA_COMPUTE_TYPE_FUNCTION_1_ROW_I( \
        function_name, CMETA_PP_UNPAREN row)
#define CMETA_COMPUTE_TYPE_FUNCTION_1_ROW_I(function_name, ...) \
    CMETA_COMPUTE_TYPE_FUNCTION_1_ROW_II(function_name, __VA_ARGS__)
#define CMETA_COMPUTE_TYPE_FUNCTION_1_ROW_II(function_name, input, result) \
    typedef result CMETA_COMPUTE_TYPE_KEY_1(function_name, input);

#define CMETA_COMPUTE_TYPE_FUNCTION_2_ROW(row, function_name) \
    CMETA_COMPUTE_TYPE_FUNCTION_2_ROW_I( \
        function_name, CMETA_PP_UNPAREN row)
#define CMETA_COMPUTE_TYPE_FUNCTION_2_ROW_I(function_name, ...) \
    CMETA_COMPUTE_TYPE_FUNCTION_2_ROW_II(function_name, __VA_ARGS__)
#define CMETA_COMPUTE_TYPE_FUNCTION_2_ROW_II( \
    function_name, left, right, result) \
    typedef result CMETA_COMPUTE_TYPE_KEY_2(function_name, left, right);

#define CMETA_COMPUTE_TYPE_FUNCTION_3_ROW(row, function_name) \
    CMETA_COMPUTE_TYPE_FUNCTION_3_ROW_I( \
        function_name, CMETA_PP_UNPAREN row)
#define CMETA_COMPUTE_TYPE_FUNCTION_3_ROW_I(function_name, ...) \
    CMETA_COMPUTE_TYPE_FUNCTION_3_ROW_II(function_name, __VA_ARGS__)
#define CMETA_COMPUTE_TYPE_FUNCTION_3_ROW_II( \
    function_name, first, second, third, result) \
    typedef result CMETA_COMPUTE_TYPE_KEY_3( \
        function_name, first, second, third);

#define CMETA_COMPUTE_VALUE_FUNCTION_1_ROW(row, function_name) \
    CMETA_COMPUTE_VALUE_FUNCTION_1_ROW_I( \
        function_name, CMETA_PP_UNPAREN row)
#define CMETA_COMPUTE_VALUE_FUNCTION_1_ROW_I(function_name, ...) \
    CMETA_COMPUTE_VALUE_FUNCTION_1_ROW_II(function_name, __VA_ARGS__)
#define CMETA_COMPUTE_VALUE_FUNCTION_1_ROW_II(function_name, input, result) \
    enum { CMETA_COMPUTE_VALUE_KEY_1(function_name, input) = (result) };

#define CMETA_COMPUTE_VALUE_FUNCTION_2_ROW(row, function_name) \
    CMETA_COMPUTE_VALUE_FUNCTION_2_ROW_I( \
        function_name, CMETA_PP_UNPAREN row)
#define CMETA_COMPUTE_VALUE_FUNCTION_2_ROW_I(function_name, ...) \
    CMETA_COMPUTE_VALUE_FUNCTION_2_ROW_II(function_name, __VA_ARGS__)
#define CMETA_COMPUTE_VALUE_FUNCTION_2_ROW_II( \
    function_name, left, right, result) \
    enum { CMETA_COMPUTE_VALUE_KEY_2(function_name, left, right) = (result) };

#define CMETA_COMPUTE_VALUE_FUNCTION_3_ROW(row, function_name) \
    CMETA_COMPUTE_VALUE_FUNCTION_3_ROW_I( \
        function_name, CMETA_PP_UNPAREN row)
#define CMETA_COMPUTE_VALUE_FUNCTION_3_ROW_I(function_name, ...) \
    CMETA_COMPUTE_VALUE_FUNCTION_3_ROW_II(function_name, __VA_ARGS__)
#define CMETA_COMPUTE_VALUE_FUNCTION_3_ROW_II( \
    function_name, first, second, third, result) \
    enum { CMETA_COMPUTE_VALUE_KEY_3( \
        function_name, first, second, third) = (result) };

#define CMETA_COMPUTE_TYPE_FUNCTION_1(function_name, ...) \
    CMETA_PP_FOR_EACH_A( \
        CMETA_COMPUTE_TYPE_FUNCTION_1_ROW, function_name, __VA_ARGS__) \
    CMETA_COMPUTE_DECLARATION_END

#define CMETA_COMPUTE_TYPE_FUNCTION_2(function_name, ...) \
    CMETA_PP_FOR_EACH_A( \
        CMETA_COMPUTE_TYPE_FUNCTION_2_ROW, function_name, __VA_ARGS__) \
    CMETA_COMPUTE_DECLARATION_END

#define CMETA_COMPUTE_TYPE_FUNCTION_3(function_name, ...) \
    CMETA_PP_FOR_EACH_A( \
        CMETA_COMPUTE_TYPE_FUNCTION_3_ROW, function_name, __VA_ARGS__) \
    CMETA_COMPUTE_DECLARATION_END

#define CMETA_COMPUTE_FUNCTION_DISPATCH_I( \
    function_family, input_arity, function_name, ...) \
    CMETA_PP_CAT(function_family, input_arity)(function_name, __VA_ARGS__)
#define CMETA_COMPUTE_FUNCTION_DISPATCH( \
    function_family, input_arity, function_name, ...) \
    CMETA_COMPUTE_FUNCTION_DISPATCH_I( \
        function_family, input_arity, function_name, __VA_ARGS__)

#ifndef TypeFunction
#define TypeFunction(function_name, ...) \
    CMETA_COMPUTE_FUNCTION_DISPATCH( \
        CMETA_COMPUTE_TYPE_FUNCTION_, \
        CMETA_PP_FIRST_RELATION_ARITY(__VA_ARGS__), \
        function_name, __VA_ARGS__)
#endif

#define CMETA_COMPUTE_TYPE_EVAL_1(function_name, input) \
    CMETA_COMPUTE_TYPE_KEY_1(function_name, input)

#define CMETA_COMPUTE_TYPE_EVAL_2(function_name, left, right) \
    CMETA_COMPUTE_TYPE_KEY_2(function_name, left, right)

#define CMETA_COMPUTE_TYPE_EVAL_3(function_name, first, second, third) \
    CMETA_COMPUTE_TYPE_KEY_3(function_name, first, second, third)

#define CMETA_COMPUTE_EVAL_DISPATCH_I( \
    eval_family, input_arity, function_name, ...) \
    CMETA_PP_CAT(eval_family, input_arity)(function_name, __VA_ARGS__)
#define CMETA_COMPUTE_EVAL_DISPATCH( \
    eval_family, input_arity, function_name, ...) \
    CMETA_COMPUTE_EVAL_DISPATCH_I( \
        eval_family, input_arity, function_name, __VA_ARGS__)

#ifndef TypeEval
#define TypeEval(function_name, ...) \
    CMETA_COMPUTE_EVAL_DISPATCH( \
        CMETA_COMPUTE_TYPE_EVAL_, CMETA_PP_NARG(__VA_ARGS__), \
        function_name, __VA_ARGS__)
#endif

#define CMETA_COMPUTE_VALUE_FUNCTION_1(function_name, ...) \
    CMETA_PP_FOR_EACH_A( \
        CMETA_COMPUTE_VALUE_FUNCTION_1_ROW, function_name, __VA_ARGS__) \
    CMETA_COMPUTE_DECLARATION_END

#define CMETA_COMPUTE_VALUE_FUNCTION_2(function_name, ...) \
    CMETA_PP_FOR_EACH_A( \
        CMETA_COMPUTE_VALUE_FUNCTION_2_ROW, function_name, __VA_ARGS__) \
    CMETA_COMPUTE_DECLARATION_END

#define CMETA_COMPUTE_VALUE_FUNCTION_3(function_name, ...) \
    CMETA_PP_FOR_EACH_A( \
        CMETA_COMPUTE_VALUE_FUNCTION_3_ROW, function_name, __VA_ARGS__) \
    CMETA_COMPUTE_DECLARATION_END

#ifndef ValueFunction
#define ValueFunction(function_name, ...) \
    CMETA_COMPUTE_FUNCTION_DISPATCH( \
        CMETA_COMPUTE_VALUE_FUNCTION_, \
        CMETA_PP_FIRST_RELATION_ARITY(__VA_ARGS__), \
        function_name, __VA_ARGS__)
#endif

#define CMETA_COMPUTE_VALUE_EVAL_1(function_name, input) \
    CMETA_COMPUTE_VALUE_KEY_1(function_name, input)

#define CMETA_COMPUTE_VALUE_EVAL_2(function_name, left, right) \
    CMETA_COMPUTE_VALUE_KEY_2(function_name, left, right)

#define CMETA_COMPUTE_VALUE_EVAL_3(function_name, first, second, third) \
    CMETA_COMPUTE_VALUE_KEY_3(function_name, first, second, third)

#ifndef ValueEval
#define ValueEval(function_name, ...) \
    CMETA_COMPUTE_EVAL_DISPATCH( \
        CMETA_COMPUTE_VALUE_EVAL_, CMETA_PP_NARG(__VA_ARGS__), \
        function_name, __VA_ARGS__)
#endif

/* Predicates and constraints ---------------------------------------------- */
#ifndef Predicate
#define Predicate(predicate_name, ...) \
    ValueFunction(predicate_name, __VA_ARGS__)
#endif

#ifndef Satisfies
#define Satisfies(predicate_name, input) \
    ValueEval(predicate_name, input)
#endif

#define CMETA_COMPUTE_STRINGIZE_I(value) #value
#define CMETA_COMPUTE_STRINGIZE(value) CMETA_COMPUTE_STRINGIZE_I(value)

#ifndef Require
#define Require(predicate_name, input) \
    CMETA_COMPUTE_STATIC_ASSERT( \
        Satisfies(predicate_name, input), \
        "CMeta predicate " CMETA_COMPUTE_STRINGIZE(predicate_name) \
        " rejects " CMETA_COMPUTE_STRINGIZE(input))
#endif

/* Constant folds over existing schemas -----------------------------------
 * SchemaAll and SchemaAny intentionally accept only single-expression rows.
 * SchemaCount accepts any row shape because its mapper ignores row values.
 */
#define CMETA_COMPUTE_SCHEMA_COUNT_TERM(...) + 1u
#define CMETA_COMPUTE_SCHEMA_ALL_TERM(value) && !!(value)
#define CMETA_COMPUTE_SCHEMA_ANY_TERM(value) || !!(value)

#ifndef SchemaCount
#define SchemaCount(schema) \
    (0u Replay(schema, CMETA_COMPUTE_SCHEMA_COUNT_TERM))
#endif

#ifndef SchemaAll
#define SchemaAll(schema) \
    (1 Replay(schema, CMETA_COMPUTE_SCHEMA_ALL_TERM))
#endif

#ifndef SchemaAny
#define SchemaAny(schema) \
    (0 Replay(schema, CMETA_COMPUTE_SCHEMA_ANY_TERM))
#endif

#endif /* CMETA_COMPUTE_H */
