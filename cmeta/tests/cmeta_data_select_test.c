#include <cmeta/data_select.h>
#ifdef __cplusplus
#include "tinytest.hpp"
#else
#include "tinytest.h"
#endif

typedef struct SelectionRecord { int value; } SelectionRecord;
static const cmeta_type_identity selection_identity = CMETA_TYPE_ID_ATOM_INIT("test.selection.record");
static const cmeta_type_desc selection_type = {
    "SelectionRecord", sizeof(SelectionRecord), CMETA_ALIGNOF(SelectionRecord),
    CMETA_T_OBJECT, NULL, NULL, &selection_identity
};
static const unsigned char selection_shape = 0;
static const cmeta_data_desc selection_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION, "test.selection.data",
    "SelectionRecord", CMETA_DATA_CUSTOM, &selection_type, &selection_shape,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
static size_t selection_reads;
/* The selection frontend borrows the provider descriptor; provider admission
 * remains responsible for semantic validity, rather than inspecting a value. */
static const cmeta_data_desc *selection_record_data(void) {
    ++selection_reads;
    return &selection_data;
}
#define SELECTION_SCHEMA(M) Schema(M, (SelectionRecord, selection_record_data()), (int, &cmeta_data_int))

suite("CMeta schema-driven typed Data selection") {
    it("selects existing builtin descriptors without evaluating the pointer") {
        int values[2] = {0};
        int *pointer = values;
        const double *real = NULL;
        check_true(cmeta_data_of(pointer++) == &cmeta_data_int);
        check_true(pointer == values);
        check_true(cmeta_data_of(real) == &cmeta_data_double);
        check_true(CMETA_DATAOF(int) == cmeta_data_of(pointer));
    }
    it("evaluates only the chosen provider expression once for mutable and const views") {
        SelectionRecord record = {0};
        const SelectionRecord *view = &record;
        int scalar = 0;
        selection_reads = 0;
        check_true(cmeta_data_of_in(&scalar, SELECTION_SCHEMA) == &cmeta_data_int);
        check_equal(selection_reads, (size_t)0);
        check_true(cmeta_data_desc_valid(&selection_data));
        check_true(cmeta_data_of_in(&record, SELECTION_SCHEMA) == &selection_data);
        check_equal(selection_reads, (size_t)1);
        check_true(cmeta_data_of_in(view, SELECTION_SCHEMA) == &selection_data);
        check_equal(selection_reads, (size_t)2);
    }
}
