#include <cmeta/meta.h>
#include "tinytest.h"

#ifdef Containers
#error "cmeta/meta.h must not expose the Containers batch macro"
#endif

#ifdef CMETA_INSTANTIATE_CONTAINER
#error "cmeta/meta.h must not expose container batch helpers"
#endif

#ifdef CMETA_CONTAINER1_DEFINE
#error "cmeta/meta.h must not include cmeta/container.h"
#endif

#ifdef CMETA_FASTPATH_H
#error "cmeta/meta.h must not expose optional fastpath runtime"
#endif

#ifdef CMETA_TRACE_H
#error "cmeta/meta.h must not expose optional trace runtime"
#endif

#ifdef CMETA_SCOPE_H
#error "cmeta/meta.h must not expose structured lifetime facade"
#endif


#if defined(CMETA_POOL_H) || defined(CMETA_LOCAL_H) || \
    defined(SALTS_FASTPATH_H) || defined(SALTS_THREAD_PRIMITIVES_H) || \
    defined(SALTS_OBJECT_POOL_H) || defined(SALTS_ATOMIC_H) || defined(SALTS_RCU_H)
#error "core Reflection must not expose migrated runtime owners"
#endif

suite("CMeta aggregate public header") {
    it("exposes range metadata without container facade generators") {
        cmeta_range range = {0};

        check_equal(sizeof(range), sizeof(cmeta_range));
    }
}
