#include <cmeta/bind.h>
#include <cmeta/scope.h>

#if defined(CMETA_TEST_callback)
typedef int WrongLifecycle;
static cmeta_status wrong_init(double *value) { *value = 0; return CMETA_OK; }
static void restore_int(WrongLifecycle *value) { *value = 0; }
static void move_int(WrongLifecycle *dst,WrongLifecycle *src) { *dst = *src; *src = 0; }
CMETA_DEFINE_LIFECYCLE(WrongLifecycle,&cmeta_type_int,wrong_init,restore_int,move_int,0)
#elif defined(CMETA_TEST_trivial)
typedef int Managed;
enum { Managed_cmeta_lifecycle_flags = CMETA_LIFECYCLE_MOVABLE };
static void rejected_scope(void) {
    cmeta_status status;
    cmeta_scope(status,cmeta_autos((Managed,value,trivial)),cmeta_body(CMETA_OK));
}
#elif defined(CMETA_TEST_packed)
#pragma pack(push,1)
Struct(Packed,(char,prefix),(int,number));
#pragma pack(pop)
#elif defined(CMETA_TEST_result_flags)
FunctionDeclAsAbiResult(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,
    CMETA_RESULT_OWNED | CMETA_RESULT_SHARED,invalid_result,
    (int,input,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR));
#elif defined(CMETA_TEST_interface_result)
#define INVALID_METHODS(X,I) X(I,F0,int,get,value,&cmeta_type_int,CMETA_ABI_VOID)
CMETA_INTERFACE(InvalidInterface,INVALID_METHODS);
#elif defined(CMETA_TEST_native_result)
FunctionDeclAsAbi(value,int,&cmeta_type_int,CMETA_ABI_VOID,invalid_native_result,
    (int,input,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR));
#elif defined(CMETA_TEST_capacity)
#define DROW(name) (double,name,CMETA_PARAM_IN,&cmeta_type_double,CMETA_ABI_SCALAR)
#define IROW (int,input,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)
FunctionDeclAsAbiResult(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE,
    excessive,DROW(a),DROW(b),DROW(c),DROW(d),DROW(e),IROW);
FunctionBindDeclAsAbiResult(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE,
    oversized,excessive,CMETA_SIG_U_I_I,(value,DROW(a)),(value,DROW(b)),
    (value,DROW(c)),(value,DROW(d)),(value,DROW(e)),(arg,IROW));
#else
static const cmeta_type_desc pointer_type = {
    "int pointer",sizeof(int *),CMETA_ALIGNOF(int *),CMETA_T_POINTER,&cmeta_type_int,NULL,NULL
};
#if defined(CMETA_TEST_capture_value)
#define CAPTURE_MODE value
#define CAPTURE_FLAGS CMETA_PARAM_IN
#else
#define CAPTURE_MODE borrow
#define CAPTURE_FLAGS (CMETA_PARAM_IN | CMETA_PARAM_OWNED)
#endif
#if defined(CMETA_TEST_native_signature)
#define NATIVE_TYPE double
#define NATIVE_DESC &cmeta_type_double
#define NATIVE_ABI CMETA_ABI_SCALAR
#define BOUND_TYPE int
#define BOUND_DESC &cmeta_type_int
#define BOUND_ABI CMETA_ABI_SCALAR
#undef CAPTURE_MODE
#define CAPTURE_MODE value
#undef CAPTURE_FLAGS
#define CAPTURE_FLAGS CMETA_PARAM_IN
#else
#define NATIVE_TYPE int *
#define NATIVE_DESC &pointer_type
#define NATIVE_ABI CMETA_ABI_OBJECT_POINTER
#define BOUND_TYPE int *
#define BOUND_DESC &pointer_type
#define BOUND_ABI CMETA_ABI_OBJECT_POINTER
#endif
FunctionDeclAsAbiResult(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE,
    original,(NATIVE_TYPE,captured,CAPTURE_FLAGS,NATIVE_DESC,NATIVE_ABI),
    (int,input,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR));
FunctionBindDeclAsAbiResult(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE,
    rejected,original,CMETA_SIG_U_I_I,
    (CAPTURE_MODE,(BOUND_TYPE,captured,CAPTURE_FLAGS,BOUND_DESC,BOUND_ABI)),
    (arg,(int,input,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)));
#endif
int main(void) { return 0; }
