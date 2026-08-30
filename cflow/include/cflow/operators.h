#ifndef CFLOW_OPERATORS_H
#define CFLOW_OPERATORS_H

#include <cmeta/meta.h>

/*
 * CFlow operator universe — single declaration schema.
 *
 * CFlowOperators(M) is the one authoritative operator list. Declaration rows are
 * structured for readability, but Operators normalizes each row back to the
 * existing 15-field consumer signature before M sees it. Consumers continue
 * to use Replay(CFlowOperators, M) to derive enum constants, descriptors,
 * callable wrappers, Stream methods, graph wrappers and Subscription dispatch
 * tables. No repeatedly-included .def file is involved.
 */
#define CFlowOperators(M) \
    Operators(M, \
        (FILTER, filter, \
            (call, 1, 0, -1), \
            (fn, 1, INPUT, NONE, NONE, BOOL), \
            (flow, SAME, FILTER, NONE), \
            (semantic, filter), \
            (effect, CMETA_EFFECT_PURE)), \
        (MAP, map, \
            (call, 1, 0, -1), \
            (fn, 1, INPUT, NONE, NONE, VALUE), \
            (flow, RETURN, ONE, NONE), \
            (semantic, map), \
            (effect, CMETA_EFFECT_PURE)), \
        (TRANSFORM, transform, \
            (call, 1, 0, -1), \
            (fn, 1, INPUT, NONE, NONE, VALUE), \
            (flow, RETURN, ONE, NONE), \
            (semantic, map), \
            (effect, CMETA_EFFECT_PURE)), \
        (FLAT_MAP, flatMap, \
            (call, 1, 0, -1), \
            (fn, 3, INPUT, OUT_PTR, CURSOR, GENERATOR), \
            (flow, POINTEE1, EXPAND, NONE), \
            (semantic, flat_map), \
            (effect, CMETA_EFFECT_PURE)), \
        (REDUCE, reduce, \
            (call, 1, 0, -1), \
            (fn, 2, INPUT, INPUT, NONE, INPUT), \
            (flow, SAME, REDUCE, NONE), \
            (semantic, reduce), \
            (effect, CMETA_EFFECT_STATEFUL)), \
        (ZIP, zip, \
            (call, 2, 1, 0), \
            (fn, 2, INPUT, SUBGRAPH, NONE, VALUE), \
            (flow, RETURN, ONE, SUBGRAPH_1TO1), \
            (semantic, high_level), \
            (effect, CMETA_EFFECT_PURE)))

#endif
