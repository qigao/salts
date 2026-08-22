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

suite("CMeta aggregate public header") {
    it("exposes range metadata without container facade generators") {
        cmeta_range range = {0};

        check_equal(sizeof(range), sizeof(cmeta_range));
    }
}
