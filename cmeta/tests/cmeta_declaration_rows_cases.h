#ifndef CMETA_DECLARATION_ROWS_CASES_H
#define CMETA_DECLARATION_ROWS_CASES_H

#include <cmeta/enum.h>
#include <cmeta/struct.h>
#include <cmeta/function.h>

typedef int cmeta_rows_int;
#define CMETA_ROWS_TYPE cmeta_rows_int
#define CMETA_ROWS_FIELD payload
#define CMETA_ROWS_RECORD cmeta_rows_record
#define CMETA_ROWS_ENUM cmeta_rows_state
#define CMETA_ROWS_SYMBOL CMETA_ROWS_EXPLICIT
#define CMETA_ROWS_FUNCTION cmeta_rows_mix
#define CMETA_ROWS_PARAMETER left

enum { CMETA_ROWS_EXPLICIT_VALUE = 5, CMETA_ROWS_FIELD_COUNT = 2 };

/* Exercise the lower-level spelling too: declaration and lookup must expand
 * the same alias before generating identifiers and semantic names. */
CMETA_STRUCT(CMETA_ROWS_RECORD,
    (CMETA_ROWS_TYPE,CMETA_ROWS_FIELD),
    (const char *,label));
CMETA_ENUM(CMETA_ROWS_ENUM,
    (CMETA_ROWS_ZERO,"zero"),
    (CMETA_ROWS_SYMBOL,CMETA_ROWS_EXPLICIT_VALUE,"explicit"),
    (CMETA_ROWS_NEXT,"next"));

#ifdef __cplusplus
#define CMETA_ROWS_INT(value) static_cast<int>(value)
#define CMETA_ROWS_FIRST_PARAM \
    (CMETA_ROWS_TYPE,CMETA_ROWS_PARAMETER,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)
#else
#define CMETA_ROWS_INT(value) ((int)(value))
#define CMETA_ROWS_FIRST_PARAM (CMETA_ROWS_TYPE,CMETA_ROWS_PARAMETER,CMETA_PARAM_IN)
#endif

FunctionDeclAsAbi(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,CMETA_ROWS_FUNCTION,
    CMETA_ROWS_FIRST_PARAM,
    (int,right,CMETA_PARAM_IN,&cmeta_type_int),
    (double,scale,CMETA_PARAM_IN,&cmeta_type_double,CMETA_ABI_SCALAR));
int cmeta_rows_mix(cmeta_rows_int left, int right, double scale) {
    return (left + right) * CMETA_ROWS_INT(scale);
}

suite("CMeta declaration row normalization") {
    it("keeps field names, native layout and type descriptors aligned through aliases") {
        const cmeta_struct_desc *record = StructMeta(CMETA_ROWS_RECORD);
        const cmeta_field_desc *field = FieldFind(CMETA_ROWS_RECORD,"payload");
        check_not_null(field);
        check_equal(record->name,"cmeta_rows_record");
        check_equal(FieldCount(CMETA_ROWS_RECORD),CMETA_ROWS_FIELD_COUNT);
        check_equal(record->size,sizeof(cmeta_rows_record));
        check_equal(record->align,CMETA_ALIGNOF(cmeta_rows_record));
        check_equal(field->name,"payload");
        check_equal(field->type_name,"cmeta_rows_int");
        check_equal(field->offset,offsetof(cmeta_rows_record,payload));
        check_equal(field->size,sizeof(cmeta_rows_int));
        check_true(field->type == &cmeta_type_int);
        field = FieldMeta(CMETA_ROWS_RECORD,1u);
        check_not_null(field);
        check_equal(field->offset,offsetof(cmeta_rows_record,label));
        check_equal(field->type_name,"const char *");
    }
    it("preserves explicit and implicit enum values and expands lookup aliases") {
        cmeta_rows_state parsed = CMETA_ROWS_ZERO;
        check_equal(EnumMeta(CMETA_ROWS_ENUM)->name,"cmeta_rows_state");
        check_equal(CMETA_ROWS_INT(CMETA_ROWS_ZERO),0);
        check_equal(CMETA_ROWS_INT(CMETA_ROWS_SYMBOL),CMETA_ROWS_EXPLICIT_VALUE);
        check_equal(CMETA_ROWS_INT(CMETA_ROWS_NEXT),CMETA_ROWS_EXPLICIT_VALUE + 1);
        check_equal(EnumSymbol(CMETA_ROWS_ENUM,CMETA_ROWS_SYMBOL),"CMETA_ROWS_EXPLICIT");
        check_equal(EnumString(CMETA_ROWS_ENUM,CMETA_ROWS_NEXT),"next");
        check_true(EnumParse(CMETA_ROWS_ENUM,"CMETA_ROWS_EXPLICIT",&parsed));
        check_equal(CMETA_ROWS_INT(parsed),CMETA_ROWS_INT(CMETA_ROWS_SYMBOL));
        check_true(EnumParse(CMETA_ROWS_ENUM,"next",&parsed));
        check_equal(CMETA_ROWS_INT(parsed),CMETA_ROWS_INT(CMETA_ROWS_NEXT));
        check_false(EnumParse(CMETA_ROWS_ENUM,"missing",&parsed));
        check_equal(CMETA_ROWS_INT(parsed),CMETA_ROWS_INT(CMETA_ROWS_NEXT));
    }
    it("projects mixed parameter rows without inventing a missing ABI carrier") {
        const cmeta_function_desc *function = FunctionMeta(CMETA_ROWS_FUNCTION);
        const cmeta_function_abi_desc *abi = FunctionAbi(CMETA_ROWS_FUNCTION);
        cmeta_rows_mix_function_type invoke = &cmeta_rows_mix;
        check_equal(invoke(2,3,2.0),10);
        check_true(cmeta_function_desc_valid(function));
        check_equal(function->name,"cmeta_rows_mix");
        check_equal(function->param_count,3u);
        check_equal(function->params[0].name,"left");
        check_true(function->params[0].type == &cmeta_type_int);
        check_equal(function->params[1].name,"right");
        check_true(function->params[1].type == &cmeta_type_int);
        check_equal(function->params[2].name,"scale");
        check_true(function->params[2].type == &cmeta_type_double);
        check_equal(CMETA_ROWS_INT(abi->param_carriers[0]),CMETA_ROWS_INT(CMETA_ABI_SCALAR));
        check_equal(CMETA_ROWS_INT(abi->param_carriers[1]),CMETA_ROWS_INT(CMETA_ABI_UNSPECIFIED));
        check_equal(CMETA_ROWS_INT(abi->param_carriers[2]),CMETA_ROWS_INT(CMETA_ABI_SCALAR));
        check_true(cmeta_function_abi_desc_valid(abi));
        check_false(cmeta_function_abi_contract_compatible(abi,abi));
    }
}

#endif
