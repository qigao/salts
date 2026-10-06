#include <cmeta/object_scope.h>
#include <cmeta/scope.h>
#ifdef __cplusplus
#include "tinytest.hpp"
#else
#include "tinytest.h"
#endif

typedef int TrivialInt;
CMETA_DEFINE_TRIVIAL_LIFECYCLE(TrivialInt,&cmeta_type_int)
static cmeta_data_desc trivial_data;
static size_t trivial_data_reads;
static const cmeta_data_desc *TrivialInt_cmeta_data(void) {
    ++trivial_data_reads;
    return &trivial_data;
}
typedef int ManagedInt;
static bool managed_fail;
static size_t managed_restores;
static cmeta_status managed_init(ManagedInt *value) {
    *value = managed_fail ? 42 : 0;
    return managed_fail ? CMETA_CALLBACK_ERROR : CMETA_OK;
}
static void managed_restore(ManagedInt *value) { ++managed_restores; *value = 0; }
static void managed_move(ManagedInt *dst,ManagedInt *src) { *dst = *src; *src = 0; }
CMETA_DEFINE_LIFECYCLE(ManagedInt,&cmeta_type_int,managed_init,managed_restore,managed_move,
    CMETA_LIFECYCLE_MOVABLE)

enum { CLEANUP_TEST_COUNT = 3 };
static int cleanup_log[CLEANUP_TEST_COUNT];
static size_t cleanup_count;
static cmeta_status cleanup_body_error(void *context) {
    (void)context;
    return CMETA_CALLBACK_ERROR;
}
static void record_cleanup(void *authority,void *resource) {
    cmeta_cleanup *obligation = (cmeta_cleanup *)authority;
    cleanup_log[cleanup_count++] = *(int *)resource;
    cmeta_cleanup_run(obligation);
}
static cmeta_status trivial_body(TrivialInt *value) {
    if (*value != 0) return CMETA_CALLBACK_ERROR;
    *value = 17;
    return CMETA_OK;
}
static size_t partial_restores;
static cmeta_status partial_init(void *value) { *(int *)value = 42; return CMETA_CALLBACK_ERROR; }
static void partial_restore(void *value) { ++partial_restores; *(int *)value = 0; }

spec("CMeta admitted lifecycle and cleanup obligations") {
    it("admits canonical layout once and uses explicit lifecycle capability") {
        cmeta_data_desc data = cmeta_data_int;
        cmeta_lifecycle_binding binding = CMETA_LIFECYCLE_BINDING_INIT;
        int value = 9, destination = 0;
        data.construct_ops = &TrivialInt_construct_ops;
        data.struct_size = sizeof(data);
        check_equal(cmeta_lifecycle_admit(&data,sizeof(int),CMETA_ALIGNOF(int),&binding),CMETA_OK);
        check_true(binding.ops == &TrivialInt_construct_ops);
        check_equal(cmeta_lifecycle_init(&binding,&value),CMETA_OK);
        check_equal(value,0);
        value = 7;
        check_equal(cmeta_lifecycle_move(&binding,&destination,&value),CMETA_OK);
        check_equal(destination,7); check_equal(value,0);
        check_equal(cmeta_lifecycle_move(&binding,&value,&value),CMETA_INVALID_ARGUMENT);
        cmeta_cleanup obligation = CMETA_CLEANUP_INIT;
        check_equal(cmeta_cleanup_data(&obligation,&binding,&destination),CMETA_OK);
        cmeta_cleanup_run(&obligation);
        check_equal(destination,0);
        check_equal(cmeta_lifecycle_admit(&data,sizeof(short),CMETA_ALIGNOF(short),&binding),CMETA_TYPE_MISMATCH);
        check_null(binding.ops);
    }
    it("restores failed partial construction once and rejects unknown lifecycle facts") {
        cmeta_data_desc data = cmeta_data_int;
        cmeta_data_construct_ops ops = TrivialInt_construct_ops;
        cmeta_lifecycle_binding binding = CMETA_LIFECYCLE_BINDING_INIT;
        int value = 0;
        ops.init_zero = partial_init; ops.restore_zero = partial_restore; ops.flags = 0;
        data.construct_ops = &ops;
        data.struct_size = sizeof(data);
        partial_restores = 0;
        check_equal(cmeta_lifecycle_admit(&data,sizeof(int),CMETA_ALIGNOF(int),&binding),CMETA_OK);
        check_equal(cmeta_lifecycle_init(&binding,&value),CMETA_CALLBACK_ERROR);
        check_equal(value,0); check_equal(partial_restores,(size_t)1);
        ops.flags = CMETA_LIFECYCLE_FLAG_MASK + 1u;
        check_equal(cmeta_lifecycle_admit(&data,sizeof(int),CMETA_ALIGNOF(int),&binding),CMETA_INVALID_ARGUMENT);
        check_null(binding.ops);
        ops.struct_size = offsetof(cmeta_data_construct_ops,flags);
        check_equal(cmeta_lifecycle_flags_of(&ops),(cmeta_lifecycle_flags)0);
        check_equal(cmeta_lifecycle_admit(&data,sizeof(int),CMETA_ALIGNOF(int),&binding),CMETA_OK);
    }
    it("discharges in reverse order and transfers one obligation without copying ownership") {
        cmeta_cleanup obligations[CLEANUP_TEST_COUNT] = {CMETA_CLEANUP_INIT};
        cmeta_cleanup moved = CMETA_CLEANUP_INIT;
        int values[CLEANUP_TEST_COUNT] = {1,2,3};
        size_t i;
        cleanup_count = 0;
        for (i=0;i<CLEANUP_TEST_COUNT;++i)
            check_equal(cmeta_cleanup_arm(&obligations[i],record_cleanup,&obligations[i],&values[i]),CMETA_OK);
        check_equal(cmeta_cleanup_arm(&obligations[0],record_cleanup,NULL,&values[0]),CMETA_BUSY);
        check_equal(cmeta_cleanup_transfer(&moved,&obligations[1]),CMETA_OK);
        check_equal(cmeta_with_cleanups(obligations,CLEANUP_TEST_COUNT,cleanup_body_error,NULL),
            CMETA_CALLBACK_ERROR);
        check_equal(cleanup_count,(size_t)2);
        check_equal(cleanup_log[0],3); check_equal(cleanup_log[1],1);
        cmeta_cleanup_run(&moved); cmeta_cleanup_run(&moved);
        check_equal(cleanup_count,(size_t)3); check_equal(cleanup_log[2],2);
    }
    it("lowers declared trivial storage without a lifecycle callback or live slot") {
        cmeta_status status;
        cmeta_scope(status,cmeta_autos((TrivialInt,value,trivial)),cmeta_body(trivial_body(&value)));
        check_equal(status,CMETA_OK);
        check_equal(cmeta_lifecycle_flags_of(&TrivialInt_construct_ops),
            (cmeta_lifecycle_flags)TrivialInt_cmeta_lifecycle_flags);
    }
    it("still admits checked trivial metadata and rejects a runtime classification mismatch") {
        cmeta_status status;
        cmeta_data_construct_ops ops = TrivialInt_construct_ops;
        trivial_data = cmeta_data_int;
        trivial_data.struct_size = sizeof(trivial_data);
        trivial_data.construct_ops = &ops;
        trivial_data_reads = 0;
        cmeta_scope_checked(status,cmeta_autos((TrivialInt,value,trivial)),cmeta_body(trivial_body(&value)));
        check_equal(status,CMETA_OK);
        check_equal(trivial_data_reads,(size_t)1);
        ops.flags = 0;
        cmeta_scope_checked(status,cmeta_autos((TrivialInt,value,trivial)),cmeta_body(trivial_body(&value)));
        check_equal(status,CMETA_TYPE_MISMATCH);
        check_equal(trivial_data_reads,(size_t)2);
    }
    it("preserves managed rollback between trivial declarations") {
        cmeta_status status;
        managed_restores = 0;
        managed_fail = false;
        cmeta_scope(status,cmeta_autos((TrivialInt,first,trivial),(ManagedInt,middle),
            (TrivialInt,last,trivial)),cmeta_body(
                first == 0 && middle == 0 && last == 0 ? CMETA_OK : CMETA_INVALID_ARGUMENT));
        check_equal(status,CMETA_OK);
        check_equal(managed_restores,(size_t)1);
        managed_fail = true;
        cmeta_scope(status,cmeta_autos((TrivialInt,first,trivial),(ManagedInt,middle),
            (TrivialInt,last,trivial)),cmeta_body(CMETA_INVALID_ARGUMENT));
        check_equal(status,CMETA_CALLBACK_ERROR);
        check_equal(managed_restores,(size_t)2);
    }
    it("maps only explicit canonical result ownership into cleanup obligations") {
        cmeta_result_cleanup kind;
        check_equal(cmeta_result_cleanup_classify(CMETA_RESULT_OWNED,&kind),CMETA_OK);
        check_equal(kind,CMETA_RESULT_CLEANUP_DESTROY);
        check_equal(cmeta_result_cleanup_classify(CMETA_RESULT_SHARED | CMETA_RESULT_NULLABLE,&kind),CMETA_OK);
        check_equal(kind,CMETA_RESULT_CLEANUP_RELEASE);
        check_equal(cmeta_result_cleanup_classify(CMETA_RESULT_BORROWED,&kind),CMETA_OK);
        check_equal(kind,CMETA_RESULT_CLEANUP_BORROW);
        check_equal(cmeta_result_cleanup_classify(CMETA_RESULT_VALUE,&kind),CMETA_OK);
        check_equal(kind,CMETA_RESULT_CLEANUP_VALUE);
        check_equal(cmeta_result_cleanup_classify(CMETA_RESULT_UNKNOWN,&kind),CMETA_TRAIT_MISSING);
        check_equal(cmeta_result_cleanup_classify(CMETA_RESULT_SHARED | CMETA_RESULT_OWNED,&kind),CMETA_INVALID_ARGUMENT);
    }
}
