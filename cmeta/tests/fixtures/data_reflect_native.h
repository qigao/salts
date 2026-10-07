#ifndef CMETA_TEST_DATA_REFLECT_NATIVE_H
#define CMETA_TEST_DATA_REFLECT_NATIVE_H

#include <stdbool.h>

/* Ordinary application types; reflection lives outside the business header. */
typedef int NativeCount;
typedef struct NativeLeaf {
    NativeCount count;
    double score;
} NativeLeaf;

typedef struct NativeRecord {
    long serial;
    NativeLeaf leaf;
    int renamed;
    bool active;
    float weight;
} NativeRecord;

typedef struct NativeWideRecord {
    int f01, f02, f03, f04, f05, f06, f07, f08;
    int f09, f10, f11, f12, f13, f14, f15, f16;
} NativeWideRecord;

#endif
