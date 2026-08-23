#include <turbostl/typed.h>
#include "tinytest.h"

Struct(construction_payload,
    (TYPE(Vec, int), values),
    (TYPE(Map, int, long), index)
);

suite("TurboSTL construction binding") {
    it("exposes declared type metadata for container fields") {
        const cmeta_field_desc *values =
            cmeta_struct_find_field(construction_payload_meta(), "values");
        construction_payload payload = {0};

        check_true(values != NULL);
        check_true(values->declared_type != NULL);
        check_equal(cmeta_container_bind_types(
                        &payload.values, values->declared_type),
                    CMETA_OK);
    }
}
