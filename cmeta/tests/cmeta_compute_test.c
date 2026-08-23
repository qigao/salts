#include <cmeta/meta.h>
#include "tinytest.h"

typedef struct cmeta_compute_small {
    int value;
} cmeta_compute_small;

typedef struct cmeta_compute_wide {
    long value;
} cmeta_compute_wide;

typedef struct cmeta_compute_opaque {
    void *value;
} cmeta_compute_opaque;

#define CMETA_COMPUTE_TYPE_IS(actual, expected) \
    _Generic((actual){0}, expected: 1, default: 0)

TypeFunction1(CMetaStorage,
    (small, cmeta_compute_small),
    (wide, cmeta_compute_wide)
);

TypeFunction2(CMetaCommon,
    (small, small, cmeta_compute_small),
    (small, wide, cmeta_compute_wide),
    (wide, small, cmeta_compute_wide),
    (wide, wide, cmeta_compute_wide)
);

TypeFunction3(CMetaOperationResult,
    (add, small, small, int),
    (add, small, wide, long),
    (compare, wide, wide, int)
);

TypeFunction1(CMetaFragmented,
    (first, int)
);
TypeFunction1(CMetaFragmented,
    (second, long)
);

TypeFunction1(CMetaOverloaded,
    (small, int)
);
TypeFunction2(CMetaOverloaded,
    (small, wide, long)
);

ValueFunction1(CMetaRank,
    (small, 1),
    (wide, 2),
    (opaque, 3)
);

ValueFunction2(CMetaPairCost,
    (small, small, 2),
    (small, wide, 3),
    (wide, wide, 4)
);

ValueFunction3(CMetaDispatchCode,
    (add, small, small, 11),
    (add, small, wide, 12),
    (compare, wide, wide, 21)
);

Predicate(CMetaHashable,
    (small, 1),
    (wide, 1),
    (opaque, 0)
);

Require(CMetaHashable, small);

#define CMETA_COMPUTE_ROWS(M) \
    Schema(M, \
        (small, cmeta_compute_small), \
        (wide, cmeta_compute_wide), \
        (opaque, cmeta_compute_opaque))

#define CMETA_COMPUTE_ALL_CHECKS(M) \
    Schema(M, \
        (Satisfies(CMetaHashable, small)), \
        (Satisfies(CMetaHashable, wide)))

#define CMETA_COMPUTE_ANY_CHECKS(M) \
    Schema(M, \
        (Satisfies(CMetaHashable, opaque)), \
        (Satisfies(CMetaHashable, wide)))

_Static_assert(CMETA_COMPUTE_TYPE_IS(
                   TypeEval1(CMetaStorage, small), cmeta_compute_small),
               "unary type functions select their declared result");
_Static_assert(CMETA_COMPUTE_TYPE_IS(
                   TypeEval2(CMetaCommon, small, wide), cmeta_compute_wide),
               "binary type functions select their declared result");
_Static_assert(CMETA_COMPUTE_TYPE_IS(
                   TypeEval3(CMetaOperationResult, add, small, wide), long),
               "ternary type functions select their declared result");
_Static_assert(CMETA_COMPUTE_TYPE_IS(
                   TypeEval1(CMetaFragmented, first), int),
               "a finite function may be declared in bounded fragments");
_Static_assert(CMETA_COMPUTE_TYPE_IS(
                   TypeEval1(CMetaFragmented, second), long),
               "later fragments extend the same finite function");
_Static_assert(CMETA_COMPUTE_TYPE_IS(
                   TypeEval1(CMetaOverloaded, small), int),
               "a public function name can have a unary relation");
_Static_assert(CMETA_COMPUTE_TYPE_IS(
                   TypeEval2(CMetaOverloaded, small, wide), long),
               "arity keeps overload-like finite relations isolated");

_Static_assert(ValueEval1(CMetaRank, wide) == 2,
               "unary value functions produce integer constants");
_Static_assert(ValueEval2(CMetaPairCost, small, wide) == 3,
               "binary value functions produce integer constants");
_Static_assert(ValueEval3(CMetaDispatchCode, compare, wide, wide) == 21,
               "ternary value functions produce integer constants");
_Static_assert(Satisfies(CMetaHashable, small),
               "predicates expose their true rows");
_Static_assert(!Satisfies(CMetaHashable, opaque),
               "predicates expose their false rows");

_Static_assert(SchemaCount(CMETA_COMPUTE_ROWS) == 3u,
               "schema count accepts arbitrary row shapes");
_Static_assert(SchemaAll(CMETA_COMPUTE_ALL_CHECKS),
               "schema all folds single-expression rows");
_Static_assert(SchemaAny(CMETA_COMPUTE_ANY_CHECKS),
               "schema any folds single-expression rows");

spec("CMeta finite compile-time computation") {
    it("exposes type and value results as ordinary C declarations") {
        TypeEval2(CMetaCommon, small, wide) common = {7};

        check_equal(common.value, 7L);
        check_equal(ValueEval1(CMetaRank, small), 1);
        check_equal(ValueEval2(CMetaPairCost, wide, wide), 4);
    }
}
