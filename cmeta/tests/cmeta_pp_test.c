#include <cmeta/pp.h>
#include <stdint.h>
#ifdef __cplusplus
#include "tinytest.hpp"
#else
#include "tinytest.h"
#endif

#define PP_ALIAS 7
#define PP_VOID_ALIAS void
#define PP_VALUE(x,c) ((x) + (c))
#define PP_ADD(x,c) + PP_VALUE(x,c)
#define PP_ACCUMULATE(x,c) c += (x)
#define PP_DECL(t,n,c) t n
#define PP_NAME(t,n,c) n
#define PP_PAIR_SUM(t,n,c) + (n)
#define PP_SUM_1(a) (a)
#define PP_SUM_2(a,b) ((a)+(b))
#define PP_SUM_PREFIX PP_SUM_
#define PP_MAX_ITEMS 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16
#define PP_SUM_TUPLE(a,b) ((a)+(b))

#if CMETA_HAS_BUILTIN(cmeta_unregistered_builtin) || \
    CMETA_HAS_ATTRIBUTE(cmeta_unregistered_attribute) || \
    CMETA_HAS_FEATURE(cmeta_unregistered_feature)
#error "Unknown compiler capabilities must be unsupported"
#endif

enum { PP_READ = 1u, PP_WRITE = 2u, PP_FLAGS = PP_READ | PP_WRITE };
static const unsigned pp_checked_flags = PP_READ + CMETA_FLAGS_REQUIRE(PP_READ,PP_FLAGS);
CMETA_STATIC_ASSERT(CMETA_LAYOUT_REQUIRE(sizeof(char) == 1) == 0, "layout requirement");
CMETA_STATIC_ASSERT(CMETA_FLAGS_REQUIRE(0u,0u) == 0, "empty flag set");
CMETA_STATIC_ASSERT(CMETA_FLAGS_REQUIRE(UINT64_MAX,UINT64_MAX) == 0, "full flag width");
CMETA_STATIC_ASSERT(CMETA_HAS_COUNTER == 1, "supported test toolchains provide unique names");

#if CMETA_HAS_FEATURE(cxx_constexpr)
static constexpr int pp_constexpr_feature = PP_READ;
CMETA_STATIC_ASSERT(pp_constexpr_feature == PP_READ, "admitted C++ feature");
#endif

CMETA_STATIC_ASSERT(CMETA_PP_BOOL(0) == 0, "zero is false");
CMETA_STATIC_ASSERT((0 CMETA_PP_PAIR_MAP_N(16,PP_PAIR_SUM,~,int,1,int,2,int,3,int,4,int,5,int,6,int,7,int,8,int,9,int,10,int,11,int,12,int,13,int,14,int,15,int,16)) == 136,
    "full pair bound");
CMETA_STATIC_ASSERT(CMETA_CONST_REQUIRE(1) == 0, "expression assertion contributes zero");
CMETA_STATIC_ASSERT(CMETA_PP_BOOL(16) == 1, "nonzero token is true");
CMETA_STATIC_ASSERT(CMETA_PP_NOT(PP_ALIAS) == 0, "expand before testing");
CMETA_STATIC_ASSERT(CMETA_PP_AND(1,0) == 0, "and");
CMETA_STATIC_ASSERT(CMETA_PP_OR(0,7) == 1, "or");
CMETA_STATIC_ASSERT(CMETA_PP_IIF(0)(9,7) == 7, "iif");
CMETA_STATIC_ASSERT(CMETA_PP_IF(7)(7,9) == 7, "if normalizes");
CMETA_STATIC_ASSERT(CMETA_PP_IS_VOID(PP_VOID_ALIAS), "expanded registered token");
CMETA_STATIC_ASSERT(!CMETA_PP_IS_VOID(int), "unregistered token");
CMETA_STATIC_ASSERT(CMETA_PP_TUPLE_GET_15((PP_MAX_ITEMS)) == 16, "max tuple index");
CMETA_STATIC_ASSERT(CMETA_PP_TUPLE_HEAD((7)) == 7, "singleton tuple");
CMETA_STATIC_ASSERT(CMETA_PP_TUPLE_HEAD(CMETA_PP_TUPLE_TAIL((1,7))) == 7, "tail");
CMETA_STATIC_ASSERT(CMETA_PP_TUPLE_APPLY(PP_SUM_TUPLE,(3,4)) == 7, "tuple apply");
CMETA_STATIC_ASSERT(CMETA_PP_OVERLOAD(PP_SUM_PREFIX,3,4)(3,4) == 7, "overload expansion");

static int pp_pair_sum(CMETA_PP_PAIR_MAP_COMMA_N(2,PP_DECL,~,int,a,int,b)) {
    const int values[] = { CMETA_PP_PAIR_MAP_COMMA_N(2,PP_NAME,~,int,a,int,b) };
    return values[0] + values[1];
}

suite("CMeta finite preprocessor kernel") {
    it("uses admitted compiler capabilities and constant initializer requirements") {
        check_equal(pp_checked_flags,PP_READ);
#if CMETA_HAS_BUILTIN(__builtin_expect)
        int evaluations = 0;
        check_equal(__builtin_expect(++evaluations,1),1);
        check_equal(evaluations,1);
#endif
    }
    it("maps zero items without punctuation or mapper expansion") {
        const int empty[] = { 7 CMETA_PP_MAP_PREFIX_COMMA_N(0,UNDEFINED,~,)
            CMETA_PP_PAIR_MAP_COMMA_N(0,UNDEFINED,~,) };
        CMETA_PP_MAP_N(0,UNDEFINED,~,)
        CMETA_PP_MAP_COMMA_N(0,UNDEFINED,~,)
        CMETA_PP_MAP_SEMI_N(0,UNDEFINED,~,)
        check_equal(sizeof(empty) / sizeof(empty[0]), 1u);
        check_equal(empty[0], 7);
    }
    it("maps the full bound and places separators") {
        const int values[] = { CMETA_PP_MAP_COMMA(PP_VALUE,1,PP_MAX_ITEMS) };
        const int prefixed[] = { 0 CMETA_PP_MAP_PREFIX_COMMA(PP_VALUE,0,1,2) };
        int sum = 0 CMETA_PP_MAP(PP_ADD,0,PP_MAX_ITEMS);
        check_equal(sizeof(values) / sizeof(values[0]), 16u);
        check_equal(values[15], 17);
        check_equal(prefixed[2], 2);
        check_equal(sum, 136);
        sum = 0;
        CMETA_PP_MAP_SEMI(PP_ACCUMULATE,sum,1,2,3);
        check_equal(sum,6);
    }
    it("projects flat pairs into declarations and call arguments") {
        check_equal(pp_pair_sum(3,4), 7);
    }
    it("expands stringify and emits unique identifiers on one line") {
        enum { CMETA_PP_UNIQUE(local_) = 0, CMETA_PP_UNIQUE(local_) = 0 };
        check_equal(CMETA_PP_STRINGIFY(PP_ALIAS), "7");
        check_equal(CMETA_PP_STRINGIFY_I(PP_ALIAS), "PP_ALIAS");
    }
}
