#ifndef CMETA_GENERIC_H
#define CMETA_GENERIC_H

#include <cmeta/pp.h>

/* Small kind-probe layer -------------------------------------------------
 * A library registers a generic kind with CMETA_GENERIC_REGISTER(Kind).
 * registered kinds route to CMETA_TYPED_<Kind>; unregistered kinds route to
 * CMETA_TYPED_FALLBACK.  CFlow uses that fallback for lowercase operator
 * callables, so typed(List, ...) and typed(map, ...) can coexist.
 */
#define CMETA_GENERIC_PROBE() ~, 1
#define CMETA_GENERIC_SECOND(a, b, ...) b
#define CMETA_GENERIC_IS_PROBE(...) CMETA_GENERIC_SECOND(__VA_ARGS__, 0, 0)
#define CMETA_GENERIC_KIND_MARK(kind) CMETA_PP_CAT(CMETA_GENERIC_KIND_, kind)
#define CMETA_GENERIC_IS_KIND(kind) CMETA_GENERIC_IS_PROBE(CMETA_GENERIC_KIND_MARK(kind))

/* Registration itself must be a #define because the C preprocessor cannot
 * emit directives from macro expansion. Libraries therefore publish one
 * marker per finite generic kind alongside its typed adapter macro. */

#define CMETA_TYPED_GENERIC_I(kind, ...) CMETA_PP_CAT(CMETA_TYPED_, kind)(__VA_ARGS__)
#define CMETA_TYPED_GENERIC(kind, ...) CMETA_TYPED_GENERIC_I(kind, __VA_ARGS__)

#ifndef CMETA_TYPED_FALLBACK
#define CMETA_TYPED_FALLBACK(kind, ...) CMETA_PP_CAT(CMETA_TYPED_UNREGISTERED_, kind)(__VA_ARGS__)
#endif

#define CMETA_TYPED_ROUTE_1(kind, ...) CMETA_TYPED_GENERIC(kind, __VA_ARGS__)
#define CMETA_TYPED_ROUTE_0(kind, ...) CMETA_TYPED_FALLBACK(kind, __VA_ARGS__)
#define CMETA_TYPED_ROUTE_I(is_kind, kind, ...) \
    CMETA_PP_CAT(CMETA_TYPED_ROUTE_, is_kind)(kind, __VA_ARGS__)
#define CMETA_TYPED_ROUTE(kind, ...) \
    CMETA_TYPED_ROUTE_I(CMETA_GENERIC_IS_KIND(kind), kind, __VA_ARGS__)

/* `typed(kind, ...)` is the single public finite-generic declaration entry.
 * Multiple concrete instantiations are written as multiple typed(...) declarations;
 * CMeta intentionally has no separate batch-container DSL. */
#ifndef typed
#define typed(kind, ...) CMETA_TYPED_ROUTE(kind, __VA_ARGS__)
#endif

#endif /* CMETA_GENERIC_H */
