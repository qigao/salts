#include <cmeta/object_scope.h>
#include "tinytest.hpp"
#include <utility>
#include <type_traits>

static_assert(!std::is_copy_constructible_v<cmeta::object_scope>);
static_assert(std::is_nothrow_move_constructible_v<cmeta::object_scope>);
static int releases, destroys, retains;
static cmeta_status retain(void *,void *) { ++retains; return CMETA_OK; }
static void release(void *,void *) { ++releases; }
static void destroy(void *,void *) { ++destroys; }
static const cmeta_object_lifecycle lifecycle = {
    sizeof(cmeta_object_lifecycle),nullptr,retain,release,destroy
};
suite("CMeta ObjectRef lexical ownership") {
    before_each() { releases = destroys = retains = 0; }
    it("moves shared ownership without retain and releases during exception unwind") {
        int value = 1;
        cmeta_object_ref ref = CMETA_OBJECT_REF_INIT;
        check_equal(cmeta_object_borrow(&ref,&value,&cmeta_data_int,nullptr),CMETA_OK);
        check_equal(cmeta_object_share(&ref,&lifecycle),CMETA_OK);
        try {
            cmeta::object_scope owner;
            check_equal(owner.take(ref),CMETA_OK);
            check_equal(ref.lifetime,CMETA_OBJECT_LIFETIME_NONE);
            cmeta::object_scope moved(std::move(owner));
            check_equal(moved.get()->lifetime,CMETA_OBJECT_LIFETIME_SHARED);
            throw 1;
        } catch (int) {}
        check_equal(retains,1); check_equal(releases,1); check_equal(destroys,0);
    }
    it("destroys an owned object once and borrowed cleanup only clears its view") {
        int value = 1;
        cmeta_object_ref ref = CMETA_OBJECT_REF_INIT;
        check_equal(cmeta_object_borrow(&ref,&value,&cmeta_data_int,nullptr),CMETA_OK);
        check_equal(cmeta_object_take(&ref,&lifecycle),CMETA_OK);
        cmeta::object_scope owner;
        check_equal(owner.take(ref),CMETA_OK);
        owner.close(); owner.close();
        check_equal(destroys,1);
        check_equal(cmeta_object_borrow(&ref,&value,&cmeta_data_int,nullptr),CMETA_OK);
        cmeta_cleanup obligation = CMETA_CLEANUP_INIT;
        check_equal(cmeta_cleanup_object(&obligation,&ref),CMETA_OK);
        { cmeta::cleanup_scope guard(obligation); }
        check_equal(ref.lifetime,CMETA_OBJECT_LIFETIME_NONE);
        check_equal(destroys,1); check_equal(releases,0); check_equal(value,1);
    }
    it("preserves ownership on a busy transfer and detach transfers release duty") {
        int a = 1,b = 2;
        cmeta_object_ref first = CMETA_OBJECT_REF_INIT,second = CMETA_OBJECT_REF_INIT;
        check_equal(cmeta_object_borrow(&first,&a,&cmeta_data_int,nullptr),CMETA_OK);
        check_equal(cmeta_object_borrow(&second,&b,&cmeta_data_int,nullptr),CMETA_OK);
        check_equal(cmeta_object_take(&first,&lifecycle),CMETA_OK);
        check_equal(cmeta_object_take(&second,&lifecycle),CMETA_OK);
        cmeta::object_scope owner;
        check_equal(owner.take(first),CMETA_OK);
        check_equal(owner.take(second),CMETA_BUSY);
        check_equal(second.lifetime,CMETA_OBJECT_LIFETIME_OWNED);
        auto detached = owner.detach();
        cmeta_object_release(&detached); cmeta_object_release(&second);
        check_equal(destroys,2);
    }
    it("generates result obligations only for the matching canonical ownership") {
        int value = 1;
        cmeta_object_ref ref = CMETA_OBJECT_REF_INIT;
        cmeta_cleanup obligation = CMETA_CLEANUP_INIT;
        check_equal(cmeta_object_borrow(&ref,&value,&cmeta_data_int,nullptr),CMETA_OK);
        check_equal(cmeta_cleanup_object_result(&obligation,CMETA_RESULT_BORROWED,&ref),CMETA_OK);
        check_null(obligation.release);
        check_equal(cmeta_object_take(&ref,&lifecycle),CMETA_OK);
        check_equal(cmeta_cleanup_object_result(&obligation,CMETA_RESULT_SHARED,&ref),CMETA_TYPE_MISMATCH);
        check_equal(destroys,0);
        check_equal(cmeta_cleanup_object_result(&obligation,CMETA_RESULT_OWNED,&ref),CMETA_OK);
        cmeta_cleanup_run(&obligation); cmeta_cleanup_run(&obligation);
        check_equal(destroys,1);
    }
}
