#include "../tests/cmeta_data_reflect_fixture.h"
#include <cmeta/object.h>
#include "tinytest.h"
#include <stdlib.h>

enum { REFLECT_SAMPLES = 7, REFLECT_OPERATIONS = 10000, REFLECT_VALUE = 42 };
static volatile int reflect_read_sink;

suite("CMeta sixteen-field reflection reads") {
    bench("compares checked metadata versions and an admitted field binding") {
        NativeWideRecord record = {0};
        cmeta_data_desc legacy = *cmeta_reflected_data(NativeWideRecord);
        cmeta_object_ref legacy_ref, reflected_ref;
        cmeta_object_field_binding binding = CMETA_OBJECT_FIELD_BINDING_INIT;
        const cmeta_data_desc *field_data = NULL;
        const void *field = NULL;
        record.f16 = REFLECT_VALUE;
        legacy.abi_version = CMETA_DATA_DESC_ABI_VERSION;
        check_equal(cmeta_object_borrow(&legacy_ref, &record, &legacy, NULL), CMETA_OK);
        check_equal(cmeta_object_borrow(&reflected_ref, &record,
            cmeta_reflected_data(NativeWideRecord), NULL), CMETA_OK);
        check_equal(cmeta_object_field_bind(&reflected_ref, "f16", &binding), CMETA_OK);

        benchmark_ops("v1 checked field read", REFLECT_SAMPLES, REFLECT_OPERATIONS) {
            int result = 0;
            for (int i = 0; i < REFLECT_OPERATIONS; ++i) {
                if (cmeta_object_field_read(&legacy_ref, "f16", &field_data, &field) != CMETA_OK)
                    abort();
                result += *(const int *)field;
            }
            reflect_read_sink = result;
        }
        check_equal(reflect_read_sink, REFLECT_OPERATIONS * REFLECT_VALUE);
        benchmark_ops("v2 checked field read", REFLECT_SAMPLES, REFLECT_OPERATIONS) {
            int result = 0;
            for (int i = 0; i < REFLECT_OPERATIONS; ++i) {
                if (cmeta_object_field_read(&reflected_ref, "f16", &field_data, &field) != CMETA_OK)
                    abort();
                result += *(const int *)field;
            }
            reflect_read_sink = result;
        }
        check_equal(reflect_read_sink, REFLECT_OPERATIONS * REFLECT_VALUE);
        benchmark_ops("v2 admitted field read", REFLECT_SAMPLES, REFLECT_OPERATIONS) {
            int result = 0;
            for (int i = 0; i < REFLECT_OPERATIONS; ++i) {
                if (cmeta_object_field_read_admitted(&binding, &field) != CMETA_OK) abort();
                result += *(const int *)field;
            }
            reflect_read_sink = result;
        }
        check_equal(reflect_read_sink, REFLECT_OPERATIONS * REFLECT_VALUE);
        cmeta_object_release(&legacy_ref);
        cmeta_object_release(&reflected_ref);
    }
}
