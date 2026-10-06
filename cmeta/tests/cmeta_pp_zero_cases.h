#ifndef CMETA_PP_ZERO_CASES_H
#define CMETA_PP_ZERO_CASES_H

#include <cmeta/pp.h>
#ifdef __cplusplus
#include "tinytest.hpp"
#else
#include "tinytest.h"
#endif

CMETA_STATIC_ASSERT(CMETA_HAS_VA_OPT == 1, "modern language mode admits zero-argument helpers");

#define PP_ZERO_EMPTY
#define PP_ZERO_EMPTY_ALIAS PP_ZERO_EMPTY
#define PP_ZERO_ITEMS 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16
#define PP_ZERO_VALUE(item,context) ((item) + (context))
#define PP_ZERO_SUM(item,context) + PP_ZERO_VALUE(item,context)
#define PP_ZERO_COUNT(item,context) + 1
#define PP_ZERO_ACCUMULATE(item,context) context += (item)
#define PP_ZERO_TUPLE_VALUE(a,b) ((a) + (b))
#define PP_ZERO_TUPLE_SUM(item,context) + CMETA_PP_TUPLE_APPLY(PP_ZERO_TUPLE_VALUE,item)
#define PP_ZERO_FORWARD(M,C,...) CMETA_PP_MAP_ZERO(M,C,__VA_ARGS__)
#define PP_ZERO_OVERLOAD_0() 7
#define PP_ZERO_OVERLOAD_2(a,b) ((a) + (b))
#define PP_ZERO_CALL(...) \
    CMETA_PP_CAT(PP_ZERO_OVERLOAD_,CMETA_PP_NARG_ZERO(__VA_ARGS__))(__VA_ARGS__)

CMETA_STATIC_ASSERT(CMETA_PP_HAS_ARGS() == 0, "omitted arguments");
CMETA_STATIC_ASSERT(CMETA_PP_HAS_ARGS(PP_ZERO_EMPTY_ALIAS) == 0, "expanded empty alias");
CMETA_STATIC_ASSERT(CMETA_PP_HAS_ARGS(()) == 1, "parentheses are tokens");
CMETA_STATIC_ASSERT(CMETA_PP_HAS_ARGS(,) == 1, "a comma is a token");
CMETA_STATIC_ASSERT(CMETA_PP_NARG_ZERO() == 0, "zero arguments");
CMETA_STATIC_ASSERT(CMETA_PP_NARG_ZERO(PP_ZERO_EMPTY_ALIAS) == 0, "empty aliases count as zero");
CMETA_STATIC_ASSERT(CMETA_PP_NARG_ZERO(()) == 1, "empty tuple is one argument");
CMETA_STATIC_ASSERT(CMETA_PP_NARG_ZERO((1,2),(3,4)) == 2, "tuple commas stay protected");
CMETA_STATIC_ASSERT(CMETA_PP_NARG_ZERO(,) == 2, "two empty slots are not an empty list");
CMETA_STATIC_ASSERT((0 CMETA_PP_MAP_ZERO(PP_ZERO_COUNT,~,,)) == 2, "map preserves empty slots");
CMETA_STATIC_ASSERT(CMETA_PP_NARG_ZERO(PP_ZERO_ITEMS) == 16, "full finite bound");
CMETA_STATIC_ASSERT(CMETA_PP_TUPLE_APPLY(CMETA_PP_NARG_ZERO,()) == 0, "empty tuple replay");
CMETA_STATIC_ASSERT(CMETA_PP_TUPLE_APPLY(CMETA_PP_NARG_ZERO,(1,2)) == 2, "nonempty tuple replay");
CMETA_STATIC_ASSERT(PP_ZERO_CALL() == 7, "zero-argument overload");
CMETA_STATIC_ASSERT(PP_ZERO_CALL(PP_ZERO_EMPTY_ALIAS) == 7, "expanded zero overload");
CMETA_STATIC_ASSERT(PP_ZERO_CALL(3,4) == 7, "nonempty overload");

suite("CMeta standard zero-argument preprocessor helpers") {
    it("emits no mapper invocation or punctuation for absent and expanded-empty input") {
        CMETA_PP_MAP_ZERO(UNDEFINED,~)
        CMETA_PP_MAP_COMMA_ZERO(UNDEFINED,~,)
        CMETA_PP_MAP_SEMI_ZERO(UNDEFINED,~,PP_ZERO_EMPTY_ALIAS)
        PP_ZERO_FORWARD(UNDEFINED,~)
        const int values[] = { 7
            CMETA_PP_MAP_PREFIX_COMMA_ZERO(UNDEFINED,~)
            CMETA_PP_MAP_PREFIX_COMMA_ZERO(UNDEFINED,~,PP_ZERO_EMPTY_ALIAS)
            CMETA_PP_PREFIX_COMMA()
            CMETA_PP_PREFIX_COMMA(PP_ZERO_EMPTY_ALIAS) };
        check_equal(sizeof(values) / sizeof(values[0]), 1u);
        check_equal(values[0], 7);
    }
    it("reuses finite map ordering and separators for one through sixteen items") {
        const int singleton[] = { CMETA_PP_MAP_COMMA_ZERO(PP_ZERO_VALUE,1,6) };
        const int values[] = { CMETA_PP_MAP_COMMA_ZERO(PP_ZERO_VALUE,1,PP_ZERO_ITEMS) };
        const int prefix[] = { 0 CMETA_PP_MAP_PREFIX_COMMA_ZERO(PP_ZERO_VALUE,0,PP_ZERO_ITEMS) };
        const int forwarded[] = { 0 CMETA_PP_PREFIX_COMMA(PP_ZERO_ITEMS) };
        int sum = 0 CMETA_PP_MAP_ZERO(PP_ZERO_SUM,0,PP_ZERO_ITEMS);
        check_equal(sizeof(singleton) / sizeof(singleton[0]), 1u);
        check_equal(singleton[0], 7);
        check_equal(sizeof(values) / sizeof(values[0]), 16u);
        check_equal(sizeof(prefix) / sizeof(prefix[0]), 17u);
        for (int index = 0; index < 16; ++index) {
            check_equal(values[index], index + 2);
            check_equal(prefix[index + 1], index + 1);
            check_equal(forwarded[index + 1], index + 1);
        }
        check_equal(sum, 136);
        sum = 0;
        CMETA_PP_MAP_SEMI_ZERO(PP_ZERO_ACCUMULATE,sum,PP_ZERO_ITEMS);
        check_equal(sum, 136);
    }
    it("keeps tuple items intact and evaluates emitted expressions once") {
        int calls = 0;
        const int value[] = { CMETA_PP_MAP_COMMA_ZERO(PP_ZERO_VALUE,0,++calls) };
        const int sum = 0 PP_ZERO_FORWARD(PP_ZERO_TUPLE_SUM,~,(1,2),(1,3));
        check_equal(calls, 1);
        check_equal(value[0], 1);
        check_equal(sum, 7);
    }
}

#endif
