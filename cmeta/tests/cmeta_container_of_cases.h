#ifndef CMETA_CONTAINER_OF_CASES_H
#define CMETA_CONTAINER_OF_CASES_H

#include <cmeta/container_of.h>

enum { CONTAINER_ARRAY_COUNT = 3, CONTAINER_VALUE = 17 };
typedef int container_array[CONTAINER_ARRAY_COUNT];
typedef struct container_record {
    char prefix;
    int value;
    container_array array;
} container_record;

#if !CMETA_HAS_CONTAINER_OF && defined(cmeta_container_of)
#error "Inferred container_of must not have an unchecked substitute"
#endif

suite("CMeta fixed-layout enclosing object borrow") {
    it("recovers a nonfirst member and evaluates the pointer expression once") {
        container_record records[CONTAINER_ARRAY_COUNT] = {{0}};
        unsigned index = 0;
        container_record *owner = cmeta_container_of_as(&records[index++].value,container_record,int,value);
        check_true(owner == &records[0]);
        check_equal(index,1u);
        owner->value = CONTAINER_VALUE;
        check_equal(records[0].value,CONTAINER_VALUE);
    }
    it("preserves explicit const and volatile owner contracts") {
        const container_record constant = {0};
        volatile container_record changing = {0};
        const container_record *read = cmeta_container_of_as(&constant.value,const container_record,const int,value);
        volatile container_record *write = cmeta_container_of_as(&changing.value,volatile container_record,volatile int,value);
        CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES(
            cmeta_container_of_as(&constant.value,const container_record,const int,value),const container_record *),
            "const owner must remain const");
        check_true(read == &constant);
        check_true(write == &changing);
        write->value = CONTAINER_VALUE;
        check_equal(changing.value,CONTAINER_VALUE);
    }
    it("requires the complete array member address") {
        container_record record = {0};
        container_record *owner = cmeta_container_of_as(&record.array,container_record,container_array,array);
        check_true(owner == &record);
    }
#if CMETA_HAS_CONTAINER_OF
    it("infers the same exact member type without evaluating it") {
        container_record records[CONTAINER_ARRAY_COUNT] = {{0}};
        unsigned index = 0;
        container_record *owner = cmeta_container_of(&records[index++].value,container_record,value);
        check_equal(index,1u);
        check_true(owner == &records[0]);
        const container_record *read = &records[0];
        check_true(cmeta_container_of(&read->array,const container_record,array) == read);
    }
#endif
}

#endif
