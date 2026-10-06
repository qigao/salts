#include <cmeta/interface.h>
#ifdef __cplusplus
#include "tinytest.hpp"
#else
#include "tinytest.h"
#endif

enum { MATRIX_ARITIES = 5, MATRIX_LEGACY_METHODS = 10, MATRIX_ORDINARY_METHODS = 25 };
#ifdef __cplusplus
#define MATRIX_STATE(self) static_cast<int *>(self)
#else
#define MATRIX_STATE(self) ((int *)(self))
#endif

#define MATRIX_METHODS(X,I) \
    X(I,R0,int,r0,_) \
    X(I,R1,int,r1,int,a0) \
    X(I,R2,int,r2,int,a0,int,a1) \
    X(I,R3,int,r3,int,a0,int,a1,int,a2) \
    X(I,R4,int,r4,int,a0,int,a1,int,a2,int,a3) \
    X(I,V0,void,v0,_) \
    X(I,V1,void,v1,int,a0) \
    X(I,V2,void,v2,int,a0,int,a1) \
    X(I,V3,void,v3,int,a0,int,a1,int,a2) \
    X(I,V4,void,v4,int,a0,int,a1,int,a2,int,a3) \
    X(I,F0,int,f0,stateful,&cmeta_type_int,CMETA_ABI_SCALAR) \
    X(I,F1,int,f1,stateful,&cmeta_type_int,CMETA_ABI_SCALAR,(int,a0,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)) \
    X(I,F2,int,f2,stateful,&cmeta_type_int,CMETA_ABI_SCALAR,(int,a0,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a1,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)) \
    X(I,F3,int,f3,stateful,&cmeta_type_int,CMETA_ABI_SCALAR,(int,a0,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a1,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a2,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)) \
    X(I,F4,int,f4,stateful,&cmeta_type_int,CMETA_ABI_SCALAR,(int,a0,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a1,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a2,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a3,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)) \
    X(I,FR0,int,fr0,stateful,&cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE) \
    X(I,FR1,int,fr1,stateful,&cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE,(int,a0,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)) \
    X(I,FR2,int,fr2,stateful,&cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE,(int,a0,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a1,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)) \
    X(I,FR3,int,fr3,stateful,&cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE,(int,a0,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a1,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a2,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)) \
    X(I,FR4,int,fr4,stateful,&cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE,(int,a0,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a1,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a2,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a3,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)) \
    X(I,FV0,void,fv0,stateful,&cmeta_type_void,CMETA_ABI_VOID) \
    X(I,FV1,void,fv1,stateful,&cmeta_type_void,CMETA_ABI_VOID,(int,a0,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)) \
    X(I,FV2,void,fv2,stateful,&cmeta_type_void,CMETA_ABI_VOID,(int,a0,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a1,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)) \
    X(I,FV3,void,fv3,stateful,&cmeta_type_void,CMETA_ABI_VOID,(int,a0,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a1,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a2,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)) \
    X(I,FV4,void,fv4,stateful,&cmeta_type_void,CMETA_ABI_VOID,(int,a0,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a1,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a2,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR),(int,a3,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)) \
    X(I,D0,void,destroy,_) \
    X(I,FD0,void,reflected_destroy,stateful,&cmeta_type_void,CMETA_ABI_VOID)
CMETA_INTERFACE(arity_matrix,MATRIX_METHODS);

static int matrix_r0(void *self) { int *state = MATRIX_STATE(self); return *state + 1; }
static int matrix_r1(void *self,int a0) { int *state = MATRIX_STATE(self); return *state + (1 * a0); }
static int matrix_r2(void *self,int a0,int a1) { int *state = MATRIX_STATE(self); return *state + (1 * a0) + (2 * a1); }
static int matrix_r3(void *self,int a0,int a1,int a2) { int *state = MATRIX_STATE(self); return *state + (1 * a0) + (2 * a1) + (3 * a2); }
static int matrix_r4(void *self,int a0,int a1,int a2,int a3) { int *state = MATRIX_STATE(self); return *state + (1 * a0) + (2 * a1) + (3 * a2) + (4 * a3); }
static void matrix_v0(void *self) { int *state = MATRIX_STATE(self); *state = 1; }
static void matrix_v1(void *self,int a0) { int *state = MATRIX_STATE(self); *state = (1 * a0); }
static void matrix_v2(void *self,int a0,int a1) { int *state = MATRIX_STATE(self); *state = (1 * a0) + (2 * a1); }
static void matrix_v3(void *self,int a0,int a1,int a2) { int *state = MATRIX_STATE(self); *state = (1 * a0) + (2 * a1) + (3 * a2); }
static void matrix_v4(void *self,int a0,int a1,int a2,int a3) { int *state = MATRIX_STATE(self); *state = (1 * a0) + (2 * a1) + (3 * a2) + (4 * a3); }
static void matrix_destroy(void *self) { ++*MATRIX_STATE(self); }
static const arity_matrix_vtable matrix_vtable = {
    "matrix",0u,
    matrix_r0,matrix_r1,matrix_r2,matrix_r3,matrix_r4,matrix_v0,matrix_v1,matrix_v2,matrix_v3,matrix_v4,matrix_r0,matrix_r1,matrix_r2,matrix_r3,matrix_r4,matrix_r0,matrix_r1,matrix_r2,matrix_r3,matrix_r4,matrix_v0,matrix_v1,matrix_v2,matrix_v3,matrix_v4,matrix_destroy,matrix_destroy
};

suite("Interface canonical arity lowering") {
    it("preserves dispatch for every supported source row and arity") {
        int state = 0;
        arity_matrix handle = arity_matrix_bind(&state,&matrix_vtable);
        check_true(arity_matrix_valid(&handle));
        state = 0;
        check_equal(arity_matrix_r0(&handle),1);
        state = 0;
        check_equal(arity_matrix_r1(&handle,2),2);
        state = 0;
        check_equal(arity_matrix_r2(&handle,2,3),8);
        state = 0;
        check_equal(arity_matrix_r3(&handle,2,3,4),20);
        state = 0;
        check_equal(arity_matrix_r4(&handle,2,3,4,5),40);
        state = 0;
        arity_matrix_v0(&handle);
        check_equal(state,1);
        state = 0;
        arity_matrix_v1(&handle,2);
        check_equal(state,2);
        state = 0;
        arity_matrix_v2(&handle,2,3);
        check_equal(state,8);
        state = 0;
        arity_matrix_v3(&handle,2,3,4);
        check_equal(state,20);
        state = 0;
        arity_matrix_v4(&handle,2,3,4,5);
        check_equal(state,40);
        state = 0;
        check_equal(arity_matrix_f0(&handle),1);
        state = 0;
        check_equal(arity_matrix_f1(&handle,2),2);
        state = 0;
        check_equal(arity_matrix_f2(&handle,2,3),8);
        state = 0;
        check_equal(arity_matrix_f3(&handle,2,3,4),20);
        state = 0;
        check_equal(arity_matrix_f4(&handle,2,3,4,5),40);
        state = 0;
        check_equal(arity_matrix_fr0(&handle),1);
        state = 0;
        check_equal(arity_matrix_fr1(&handle,2),2);
        state = 0;
        check_equal(arity_matrix_fr2(&handle,2,3),8);
        state = 0;
        check_equal(arity_matrix_fr3(&handle,2,3,4),20);
        state = 0;
        check_equal(arity_matrix_fr4(&handle,2,3,4,5),40);
        state = 0;
        arity_matrix_fv0(&handle);
        check_equal(state,1);
        state = 0;
        arity_matrix_fv1(&handle,2);
        check_equal(state,2);
        state = 0;
        arity_matrix_fv2(&handle,2,3);
        check_equal(state,8);
        state = 0;
        arity_matrix_fv3(&handle,2,3,4);
        check_equal(state,20);
        state = 0;
        arity_matrix_fv4(&handle,2,3,4,5);
        check_equal(state,40);
    }
    it("preserves reflection, parameter order and required-method validation") {
        const cmeta_interface_desc *meta = arity_matrix_interface();
        int state = 0;
        arity_matrix_vtable incomplete = matrix_vtable;
        arity_matrix handle = arity_matrix_bind(&state,&incomplete);
        check_true(cmeta_interface_desc_valid(meta));
        for (size_t i = 0; i < MATRIX_ORDINARY_METHODS; ++i) {
            check_equal(cmeta_interface_method_arity(&meta->methods[i]),i % MATRIX_ARITIES);
            if (i >= MATRIX_LEGACY_METHODS) {
                check_true(cmeta_interface_method_reflection_valid(&meta->methods[i]));
                if (i % MATRIX_ARITIES != 0) check_equal(meta->methods[i].function->params[0].name,"a0");
            } else check_null(meta->methods[i].function);
        }
        incomplete.r4 = NULL;
        check_false(arity_matrix_valid(&handle));
    }
    it("discharges both destructor spellings exactly once and invalidates") {
        int state = 0;
        arity_matrix handle = arity_matrix_bind(&state,&matrix_vtable);
        arity_matrix_destroy(&handle);
        arity_matrix_destroy(&handle);
        check_equal(state,1);
        check_null(handle.self);
        check_null(handle.vtable);
        handle = arity_matrix_bind(&state,&matrix_vtable);
        arity_matrix_reflected_destroy(&handle);
        arity_matrix_reflected_destroy(&handle);
        check_equal(state,2);
        check_null(handle.self);
        check_null(handle.vtable);
    }
}
