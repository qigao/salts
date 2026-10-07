#ifndef CMETA_TEST_DATA_REFLECT_FIXTURE_H
#define CMETA_TEST_DATA_REFLECT_FIXTURE_H

#include "fixtures/data_reflect_native.h"
#include <cmeta/data_reflect.h>

cmeta_reflect_value(NativeLeaf, "test.native.Leaf",
    cmeta_field(NativeCount, count)
    cmeta_field(double, score)
);
cmeta_reflect_value(NativeRecord, "test.native.Record",
    cmeta_field(long, serial)
    cmeta_data_field(NativeLeaf, leaf, cmeta_reflected_data(NativeLeaf),
        cmeta_reflected_storage(NativeLeaf))
    cmeta_data_field_id(int, renamed, "test.native.Record.original", &cmeta_data_int)
    cmeta_field(bool, active)
    cmeta_field(float, weight)
);

/* A distinct semantic integer descriptor demonstrates that explicit field
 * semantics survive even when native storage is an ordinary builtin type. */
static const cmeta_data_integer_shape native_identifier_shape = {32u};
static const cmeta_data_desc native_identifier_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
    "test.native.Identifier", "Identifier", CMETA_DATA_SINT,
    &cmeta_type_int32, &native_identifier_shape,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
CMETA_STATIC_ASSERT(sizeof(int) == sizeof(int32_t), "Test identifier storage requires int32");
cmeta_reflect_value(NativeWideRecord, "test.native.WideRecord",
    cmeta_field(int, f01)
    cmeta_field(int, f02)
    cmeta_field(int, f03)
    cmeta_field(int, f04)
    cmeta_field(int, f05)
    cmeta_field(int, f06)
    cmeta_field(int, f07)
    cmeta_field(int, f08)
    cmeta_field(int, f09)
    cmeta_field(int, f10)
    cmeta_field(int, f11)
    cmeta_field(int, f12)
    cmeta_field(int, f13)
    cmeta_field(int, f14)
    cmeta_field(int, f15)
    cmeta_data_field(int, f16, &native_identifier_data, &cmeta_type_int32)
);

typedef NativeRecord NativeRecordView;
cmeta_reflect_data(NativeRecordView, "test.native.RecordView",
    cmeta_field(long, serial)
);
typedef struct NativeViewParent { NativeRecordView child; } NativeViewParent;
cmeta_reflect_value(NativeViewParent, "test.native.ViewParent",
    cmeta_data_field(NativeRecordView, child, cmeta_reflected_data(NativeRecordView),
        cmeta_reflected_storage(NativeRecordView))
);

typedef struct NativeConstView { const int value; } NativeConstView;
cmeta_reflect_data(NativeConstView, "test.native.ConstView",
    cmeta_data_field(const int, value, &cmeta_data_int, &cmeta_type_int)
);

#ifdef __cplusplus
extern "C" {
#endif
const cmeta_data_desc *cmeta_test_peer_record_data(void);
#ifdef __cplusplus
}
#endif

#endif
