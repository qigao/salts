#ifndef CMETA_COMPILER_TYPE_CASES_H
#define CMETA_COMPILER_TYPE_CASES_H

#include <cmeta/compiler.h>

#if defined(__cplusplus) || defined(__GNUC__) || defined(__clang__)
CMETA_STATIC_ASSERT(CMETA_HAS_NATIVE_TYPEOF && CMETA_HAS_SAME_TYPE && CMETA_HAS_AUTO,
    "qualified toolchain must provide native type helpers");
#else
CMETA_STATIC_ASSERT(!CMETA_HAS_NATIVE_TYPEOF && !CMETA_HAS_SAME_TYPE && !CMETA_HAS_AUTO,
    "unsupported toolchain must not advertise native type helpers");
#endif

#if !CMETA_HAS_NATIVE_TYPEOF && defined(CMETA_NATIVE_TYPEOF)
#error "Unsupported native typeof must not have a substitute"
#endif
#if !CMETA_HAS_SAME_TYPE && defined(CMETA_SAME_TYPE)
#error "Unsupported type comparison must not have a substitute"
#endif
#if !CMETA_HAS_AUTO && defined(CMETA_AUTO)
#error "Unsupported auto deduction must not have a substitute"
#endif

enum { CMETA_COMPILER_ARRAY_SIZE = 3, CMETA_COMPILER_VALUE = 7 };

#if CMETA_HAS_NATIVE_TYPEOF
static int cmeta_compiler_identity(int value) { return value; }
#endif

suite("CMeta native compiler types") {
    it("reports capabilities without enabling weaker substitutes") {
        check_true(CMETA_HAS_NATIVE_TYPEOF == 0 || CMETA_HAS_NATIVE_TYPEOF == 1);
        check_true(CMETA_HAS_SAME_TYPE == 0 || CMETA_HAS_SAME_TYPE == 1);
        check_true(CMETA_HAS_AUTO == 0 || CMETA_HAS_AUTO == 1);
    }
#if CMETA_HAS_NATIVE_TYPEOF
    it("queries fixed expression types without evaluation or array decay") {
        int evaluations = 0;
        const int original = CMETA_COMPILER_VALUE;
        int array[CMETA_COMPILER_ARRAY_SIZE] = {0};
        typedef CMETA_NATIVE_TYPEOF(++evaluations) counter_type;
        CMETA_NATIVE_TYPEOF(original) copy = original;
        CMETA_NATIVE_TYPEOF(array) other = {0};
        CMETA_NATIVE_TYPEOF(cmeta_compiler_identity) *invoke = cmeta_compiler_identity;
        counter_type count = evaluations;
        CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(&copy,const int *), "preserve const");
        CMETA_STATIC_ASSERT(sizeof(other) == sizeof(array), "preserve array bound");
        check_equal(count,0);
        check_equal(evaluations,0);
        check_equal(copy,CMETA_COMPILER_VALUE);
        check_equal(invoke(copy),CMETA_COMPILER_VALUE);
    }
#endif
#if CMETA_HAS_SAME_TYPE
    it("compares qualifiers, pointer targets, array bounds and native functions") {
        int value = 0;
        const int constant = 0;
        int array[CMETA_COMPILER_ARRAY_SIZE] = {0};
        int larger[CMETA_COMPILER_ARRAY_SIZE + 1] = {0};
        int (*function_pointer)(int) = cmeta_compiler_identity;
        CMETA_STATIC_ASSERT(CMETA_SAME_TYPE(value,array[0]), "same object type");
        CMETA_STATIC_ASSERT(!CMETA_SAME_TYPE(value,constant), "top-level qualifiers matter");
        CMETA_STATIC_ASSERT(!CMETA_SAME_TYPE(&value,&constant), "pointee qualifiers matter");
        CMETA_STATIC_ASSERT(!CMETA_SAME_TYPE(array,&array[0]), "array is not pointer");
        CMETA_STATIC_ASSERT(!CMETA_SAME_TYPE(array,larger), "array bounds matter");
        CMETA_STATIC_ASSERT(CMETA_SAME_TYPE(cmeta_compiler_identity,*function_pointer),
            "function type is retained");
        check_true(CMETA_SAME_TYPE(value++,array[0]));
        check_equal(value,0);
    }
#endif
#if CMETA_HAS_AUTO
    it("initializes one local value and preserves pointer and aggregate types") {
        struct compiler_record { int value; } record = {CMETA_COMPILER_VALUE};
        int evaluations = 0;
        const int original = CMETA_COMPILER_VALUE;
        int array[CMETA_COMPILER_ARRAY_SIZE] = {0};
        CMETA_AUTO(count,++evaluations);
        CMETA_AUTO(pointer,(++evaluations,&original));
        CMETA_AUTO(copy,original);
        CMETA_AUTO(array_pointer,array);
        CMETA_AUTO(record_copy,record);
        CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(&copy,int *), "local value drops top-level const");
        CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(pointer,const int *), "preserve pointee const");
        CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(array_pointer,int *), "initializer array decays");
        check_equal(evaluations,2);
        check_equal(count,1);
        check_true(pointer == &original);
        check_true(array_pointer == array);
        check_equal(record_copy.value,CMETA_COMPILER_VALUE);
        record_copy.value = 0;
        check_equal(record.value,CMETA_COMPILER_VALUE);
        check_equal(copy,CMETA_COMPILER_VALUE);
    }
#if !defined(__cplusplus) && !defined(__STDC_NO_VLA__)
    it("evaluates a variably modified pointer initializer exactly once") {
        int size = CMETA_COMPILER_ARRAY_SIZE;
        int values[size];
        int (*cursor)[size] = &values;
        CMETA_AUTO(saved,cursor++);
        check_true(saved == &values);
        check_true(cursor == &values + 1);
    }
#endif
#endif
}

#endif
