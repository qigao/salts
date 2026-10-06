#include <salts/plugin_decl.h>
#ifdef __cplusplus
#include "tinytest.hpp"
#else
#include "tinytest.h"
#endif

enum { DECL_RESULT = 37, DECL_ARITY = 16 };
static int calls;
Function0InvokeDeclAsAbi(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,decl_zero);
int decl_zero(void) { ++calls; return DECL_RESULT; }
Function0InvokeDeclAsAbi(stateful,void,&cmeta_type_void,CMETA_ABI_VOID,decl_void_zero);
void decl_void_zero(void) { ++calls; }
FunctionInvokeDeclAsAbi(stateful,void,&cmeta_type_void,CMETA_ABI_VOID,decl_void,
    (int,input,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR));
void decl_void(int input) { calls += input; }

static const cmeta_type_desc decl_pointer_type = {
    "int *",sizeof(int *),CMETA_ALIGNOF(int *),CMETA_T_POINTER,&cmeta_type_int,NULL,NULL
};
FunctionInvokeDeclAsAbiResult(value,int *,&decl_pointer_type,CMETA_ABI_OBJECT_POINTER,
    CMETA_RESULT_BORROWED | CMETA_RESULT_NULLABLE,decl_pointer,
    (int *,input,CMETA_PARAM_IN | CMETA_PARAM_NULLABLE,&decl_pointer_type,CMETA_ABI_OBJECT_POINTER));
int *decl_pointer(int *input) { ++calls; return input; }

typedef struct decl_record { int left; double right; } decl_record;
static const cmeta_type_identity decl_record_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.declarations.record");
static const cmeta_type_desc decl_record_type = {
    "decl_record",sizeof(decl_record),CMETA_ALIGNOF(decl_record),CMETA_T_OBJECT,
    NULL,NULL,&decl_record_identity
};
FunctionInvokeDeclAsAbi(value,decl_record,&decl_record_type,CMETA_ABI_AGGREGATE,decl_swap,
    (decl_record,input,CMETA_PARAM_IN,&decl_record_type,CMETA_ABI_AGGREGATE));
decl_record decl_swap(decl_record input) {
    ++calls;
    input.left += 1;
    input.right += 1.0;
    return input;
}

FunctionInvokeDeclAsAbi(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,decl_max,
    (int,p0,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (int,p1,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (int,p2,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (int,p3,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (int,p4,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (int,p5,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (int,p6,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (int,p7,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (int,p8,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (int,p9,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (int,p10,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (int,p11,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (int,p12,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (int,p13,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (int,p14,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (int,p15,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR));
int decl_max(int p0,int p1,int p2,int p3,int p4,int p5,int p6,int p7,int p8,int p9,int p10,int p11,int p12,int p13,int p14,int p15) { ++calls; return 1*p0+2*p1+3*p2+4*p3+5*p4+6*p5+7*p6+8*p7+9*p8+10*p9+11*p10+12*p11+13*p12+14*p13+15*p14+16*p15; }

#define DECL_INTERFACE_ROWS(X,I) X(I,R0,int,value,_)
CMETA_INTERFACE(decl_api,DECL_INTERFACE_ROWS);
static int decl_value(void *self) { return *CMETA_INVOKE_STORAGE(int,self); }
static const decl_api_vtable decl_vtable = {"decl",1u,decl_value};
static int decl_state = DECL_RESULT;
static decl_api decl_instance = {&decl_state,&decl_vtable};

#define DECL_EXPORTS(X) \
    X(function,decl_zero,"zero","decl.math",1u,1u) \
    X(function,decl_void_zero,"void-zero","decl.math",UINT32_MAX,UINT64_MAX) \
    X(function,decl_void,"void","decl.math",1u,1u) \
    X(function,decl_pointer,"pointer","decl.math",1u,1u) \
    X(function,decl_max,"max","decl.math",1u,1u) \
    X(interface,(decl_api,&decl_instance),"api","decl.api",1u,1u)
CMETA_PLUGIN_DECLARE(decl,"test.declarations",(1u,2u,3u),DECL_EXPORTS,CMETA_PLUGIN_PASSIVE());

suite("Plugin declarations") {
    before_each() { calls = 0; }
    it("publishes canonical descriptors and exact ABI query") {
        const cmeta_plugin_manifest *manifest = cmeta_plugin_query(CMETA_PLUGIN_ABI_VERSION);
        check_not_null(manifest);
        check_equal(cmeta_plugin_manifest_validate(manifest),CMETA_PLUGIN_OK);
        check_equal(manifest->export_count,6u);
        check_equal(manifest->version.minor,2u);
        check_true(manifest->exports[0].value.function.desc == FunctionMeta(decl_zero));
        check_true(manifest->exports[0].value.function.abi == FunctionAbi(decl_zero));
        check_equal(manifest->exports[1].contract_version,UINT32_MAX);
        check_equal(manifest->exports[1].capabilities,UINT64_MAX);
        check_null(cmeta_plugin_query(CMETA_PLUGIN_ABI_VERSION - 1u));
        check_null(cmeta_plugin_query(CMETA_PLUGIN_ABI_VERSION + 1u));
        check_null(manifest->self);
        check_null(manifest->destroy);
        check_equal(calls,0);
    }
    it("calls nonvoid and void zero-argument functions exactly once") {
        int result = 0;
        check_true(FunctionInvoke(decl_zero)(&result,NULL,0));
        check_equal(result,DECL_RESULT);
        check_equal(calls,1);
        check_true(FunctionInvoke(decl_void_zero)(NULL,NULL,0));
        check_equal(calls,2);
    }
    it("handles void results without requiring return storage") {
        int input = 3;
        void *params[] = {&input};
        check_true(FunctionInvoke(decl_void)(NULL,params,1));
        check_equal(calls,3);
    }
    it("rejects bad counts, missing storage and context before native execution") {
        int input = 1, result = 0;
        void *missing[] = {NULL};
        const cmeta_plugin_export *entry = &decl__exports[0];
        check_false(entry->value.function.invoke(&input,&result,NULL,0));
        check_false(FunctionInvoke(decl_zero)(NULL,NULL,0));
        check_false(FunctionInvoke(decl_zero)(&result,NULL,1));
        check_false(FunctionInvoke(decl_void)(NULL,NULL,1));
        check_false(FunctionInvoke(decl_void)(NULL,missing,1));
        check_false(FunctionInvoke(decl_void)(NULL,NULL,0));
        check_equal(calls,0);
    }
    it("distinguishes a null pointer value from missing parameter storage") {
        int value = DECL_RESULT;
        int *input = &value, *result = NULL;
        void *params[] = {&input};
        check_true(FunctionInvoke(decl_pointer)(&result,params,1));
        check_true(result == &value);
        input = NULL;
        check_true(FunctionInvoke(decl_pointer)(&result,params,1));
        check_null(result);
        check_equal(calls,2);
    }
    it("projects all sixteen parameters in order") {
        int values[DECL_ARITY], result = 0;
        void *params[DECL_ARITY];
        for (int i = 0; i < DECL_ARITY; ++i) { values[i] = i + 1; params[i] = &values[i]; }
        check_true(FunctionInvoke(decl_max)(&result,params,DECL_ARITY));
        check_equal(result,DECL_ARITY * (DECL_ARITY + 1) * (2 * DECL_ARITY + 1) / 6);
        check_equal(calls,1);
        params[DECL_ARITY - 1] = NULL;
        check_false(FunctionInvoke(decl_max)(&result,params,DECL_ARITY));
        check_equal(calls,1);
    }
    it("exports the exact mutable interface carrier") {
        const cmeta_plugin_export *entry = &decl__exports[5];
        check_true(entry->value.interface.desc == decl_api_interface());
        check_true(entry->value.interface.value == &decl_instance);
        check_equal(decl_api_value(&decl_instance),DECL_RESULT);
    }
    it("passes and returns an aggregate by its exact native type without publishing it") {
        decl_record input = {DECL_RESULT,2.0}, result = {0,0.0};
        void *params[] = {&input};
        check_true(FunctionInvoke(decl_swap)(&result,params,1));
        check_equal(result.left,DECL_RESULT + 1);
        check_within(result.right,3.0,0.001);
        check_equal(calls,1);
        check_equal(decl__manifest.export_count,6u);
        check_equal(input.left,DECL_RESULT);
    }
}
