#include <cmeta/bind.h>
#ifdef __cplusplus
#include "tinytest.hpp"
#else
#include "tinytest.h"
#endif

FunctionDeclAsAbiResult(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE,
    bind_add,(int,left,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (int,right,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR));
int bind_add(int left,int right) { return left + right; }

FunctionBindDeclAsAbiResult(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE,
    plus_left,bind_add,CMETA_SIG_U_I_I,
    (value,(int,left,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)),
    (arg,(int,right,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)));
FunctionBindDeclAsAbiResult(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE,
    plus_right,bind_add,CMETA_SIG_U_I_I,
    (arg,(int,left,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)),
    (value,(int,right,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)));

static const cmeta_type_desc receiver_ptr = {
    "bind receiver",sizeof(int *),CMETA_ALIGNOF(int *),CMETA_T_POINTER,&cmeta_type_int,NULL,NULL
};
#define RECEIVER_FLAGS (CMETA_PARAM_INOUT | CMETA_PARAM_BORROWED | CMETA_PARAM_RECEIVER)
FunctionDeclAsAbiResult(stateful,int,&cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE,
    receiver_add,(int *,self,RECEIVER_FLAGS,&receiver_ptr,CMETA_ABI_OBJECT_POINTER),
    (int,delta,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR));
int receiver_add(int *self,int delta) { *self += delta; return *self; }
FunctionBindDeclAsAbiResult(stateful,int,&cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE,
    bound_receiver,receiver_add,CMETA_SIG_U_I_I,
    (borrow,(int *,self,RECEIVER_FLAGS,&receiver_ptr,CMETA_ABI_OBJECT_POINTER)),
    (arg,(int,delta,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)));

/* Both arithmetic and borrowed capture in native order; two remaining args. */
FunctionDeclAsAbiResult(stateful,long,&cmeta_type_long,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE,
    receiver_scaled,(int *,self,RECEIVER_FLAGS,&receiver_ptr,CMETA_ABI_OBJECT_POINTER),
    (long,a,CMETA_PARAM_IN,&cmeta_type_long,CMETA_ABI_SCALAR),
    (int,scale,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),
    (long,b,CMETA_PARAM_IN,&cmeta_type_long,CMETA_ABI_SCALAR));
long receiver_scaled(int *self,long a,int scale,long b) { return *self + (a+b)*scale; }
FunctionBindDeclAsAbiResult(stateful,long,&cmeta_type_long,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE,
    bound_scaled,receiver_scaled,CMETA_SIG_B_L_L_L,
    (borrow,(int *,self,RECEIVER_FLAGS,&receiver_ptr,CMETA_ABI_OBJECT_POINTER)),
    (arg,(long,a,CMETA_PARAM_IN,&cmeta_type_long,CMETA_ABI_SCALAR)),
    (value,(int,scale,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)),
    (arg,(long,b,CMETA_PARAM_IN,&cmeta_type_long,CMETA_ABI_SCALAR)));

spec("CMeta generated parameter binding") {
    it("snapshots explicit value capture and preserves unbound parameter names") {
        plus_left_capture capture = {10};
        plus_right_capture right = {20};
        cmeta_invokable left_call = CMETA_INVOKABLE_INIT;
        cmeta_invokable right_call = CMETA_INVOKABLE_INIT;
        int input = 5, output = 0;
        const void *args[] = {&input};
        check_equal(plus_left_bind(&capture,&left_call),CMETA_OK);
        check_equal(plus_right_bind(&right,&right_call),CMETA_OK);
        capture.left = 90;
        check_equal(cmeta_invokable_invoke_admitted(&left_call,&output,args),CMETA_OK);
        check_equal(output,15);
        check_equal(cmeta_invokable_invoke(&right_call,&output,args),CMETA_OK);
        check_equal(output,25);
        check_equal(FunctionMeta(plus_left)->result_flags,(cmeta_result_flags)CMETA_RESULT_VALUE);
        check_true(strcmp(FunctionMeta(plus_left)->params[0].name,"right") == 0);
        check_equal(cmeta_invokable_invoke_admitted(&left_call,NULL,args),CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_invokable_invoke_admitted(&left_call,&output,NULL),CMETA_INVALID_ARGUMENT);
    }
    it("generates a receiver projection and borrows its authoritative object") {
        int receiver = 3, delta = 4, output = 0;
        bound_receiver_capture capture = {&receiver};
        cmeta_invokable call = CMETA_INVOKABLE_INIT;
        const void *args[] = {&delta};
        check_true(cmeta_function_receiver_projection_valid(
            FunctionMeta(receiver_add),FunctionMeta(bound_receiver)));
        check_equal(bound_receiver_bind(&capture,&call),CMETA_OK);
        check_equal(cmeta_invokable_invoke_admitted(&call,&output,args),CMETA_OK);
        check_equal(receiver,7);
        check_equal(output,7);
        cmeta_invokable copy = call;
        check_equal(cmeta_invokable_invoke_admitted(&copy,&output,args),CMETA_OK);
        check_equal(receiver,11);
        capture.self = NULL;
        check_equal(bound_receiver_bind(&capture,&call),CMETA_INVALID_ARGUMENT);
        check_null(call.function);
    }
    it("binds nonadjacent parameters without changing remaining order") {
        int receiver = 1; long a = 2, b = 3, output = 0;
        bound_scaled_capture capture = {&receiver,4};
        cmeta_invokable call = CMETA_INVOKABLE_INIT;
        const void *args[] = {&a,&b};
        check_equal(bound_scaled_bind(&capture,&call),CMETA_OK);
        check_equal(cmeta_invokable_invoke_admitted(&call,&output,args),CMETA_OK);
        check_equal(output,21);
    }
    it("does not import caller struct padding into immutable capture bytes") {
        int receiver = 1;
        bound_scaled_capture first,second;
        cmeta_invokable a = CMETA_INVOKABLE_INIT,b = CMETA_INVOKABLE_INIT;
        memset(&first,0xa5,sizeof(first)); memset(&second,0x5a,sizeof(second));
        first.self = &receiver; first.scale = 4;
        second.self = &receiver; second.scale = 4;
        check_equal(bound_scaled_bind(&first,&a),CMETA_OK);
        check_equal(bound_scaled_bind(&second,&b),CMETA_OK);
        check_true(cmeta_callable_same(a.callable,b.callable));
    }
    it("rejects changes to projected ownership, flags, carrier or mapping") {
        bool bound[] = {true,false};
        cmeta_function_desc function = *FunctionMeta(plus_left);
        cmeta_function_abi_desc abi = *FunctionAbi(plus_left);
        abi.function = &function;
        function.result_flags = CMETA_RESULT_OWNED;
        check_false(cmeta_function_projection_valid(FunctionAbi(bind_add),&abi,bound,2));
        function = *FunctionMeta(plus_left);
        abi.return_carrier = CMETA_ABI_OBJECT_POINTER;
        check_false(cmeta_function_projection_valid(FunctionAbi(bind_add),&abi,bound,2));
        abi = *FunctionAbi(plus_left);
        bound[0] = false; bound[1] = true;
        check_false(cmeta_function_projection_valid(FunctionAbi(bind_add),&abi,bound,2));
        check_false(cmeta_function_projection_valid(FunctionAbi(bind_add),&abi,NULL,2));
    }
    it("retains Function-owned receiver admission in generic projection") {
        bool bound[] = {true,false};
        cmeta_type_desc pointee = cmeta_type_int;
        cmeta_type_desc pointer = receiver_ptr;
        cmeta_param_desc params[] = {FunctionMeta(receiver_add)->params[0],FunctionMeta(receiver_add)->params[1]};
        cmeta_function_desc source = *FunctionMeta(receiver_add);
        cmeta_function_abi_desc abi = *FunctionAbi(receiver_add);
        pointee.name = NULL;
        pointer.pointee = &pointee;
        params[0].type = &pointer;
        source.params = params;
        abi.function = &source;
        check_false(cmeta_function_projection_valid(&abi,FunctionAbi(bound_receiver),bound,2));
    }
}
