#ifndef CFLOW_OPERATORS_H
#define CFLOW_OPERATORS_H

#include <cmeta/meta.h>

/*
 * CFlow operator universe — single declaration schema.
 *
 * CFlowOperators(M) is the one authoritative operator list. Operators is a
 * semantic alias over CMeta Schema; consumers use Replay(CFlowOperators, M) to
 * derive enum constants, descriptors, callable wrappers, Stream methods, graph
 * wrappers and runtime dispatch tables. No repeatedly-included .def file is
 * involved.
 */
#define CFlowOperators(M) \
    Operators(M, \
        (FILTER,    filter,    1, 0, -1, 1, INPUT,    NONE,     NONE,   BOOL,      SAME,     FILTER, NONE,            filter,     CMETA_EFFECT_PURE), \
        (MAP,       map,       1, 0, -1, 1, INPUT,    NONE,     NONE,   VALUE,     RETURN,   ONE,    NONE,            map,        CMETA_EFFECT_PURE), \
        (TRANSFORM, transform, 1, 0, -1, 1, INPUT,    NONE,     NONE,   VALUE,     RETURN,   ONE,    NONE,            map,        CMETA_EFFECT_PURE), \
        (FLAT_MAP,  flatMap,   1, 0, -1, 3, INPUT,    OUT_PTR,  CURSOR, GENERATOR, POINTEE1, EXPAND, NONE,            flat_map,   CMETA_EFFECT_PURE), \
        (REDUCE,    reduce,    1, 0, -1, 2, INPUT,    INPUT,    NONE,   INPUT,     SAME,     REDUCE, NONE,            reduce,     CMETA_EFFECT_STATEFUL), \
        (ZIP,       zip,       2, 1,  0, 2, INPUT,    SUBGRAPH, NONE,   VALUE,     RETURN,   ONE,    SUBGRAPH_1TO1,   high_level, CMETA_EFFECT_PURE))

#endif
