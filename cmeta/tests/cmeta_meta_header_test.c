#include <cmeta/meta.h>
#include "tinytest.h"

#if defined(CMETA_FASTPATH_H) || defined(CMETA_TRACE_H) || defined(CMETA_SCOPE_H) || defined(SALTS_FASTPATH_H)
#error "core aggregate must not include structured or optional runtime facades"
#endif

#ifdef Containers
#error "cmeta/meta.h must not expose the Containers batch macro"
#endif

#ifdef CMETA_INSTANTIATE_CONTAINER
#error "cmeta/meta.h must not expose container batch helpers"
#endif

#ifdef CMETA_CONTAINER1_DEFINE
#error "cmeta/meta.h must not include cmeta/container.h"
#endif

suite("CMeta aggregate public header") {
    it("exposes range metadata without container facade generators") {
        cmeta_range range = {0};

        check_equal(sizeof(range), sizeof(cmeta_range));
    }
}
