#ifndef CMETA_FIXED_ARRAY_FIXTURE_H
#define CMETA_FIXED_ARRAY_FIXTURE_H

#include <cmeta/fixed_array.h>

enum { CMETA_FIXED_ARRAY_TEST_COUNT = 3 };
typedef int cmeta_fixed_array_fixture[CMETA_FIXED_ARRAY_TEST_COUNT];
CMETA_DEFINE_FIXED_ARRAY(cmeta_fixed_array_value, cmeta_fixed_array_fixture,
    int, CMETA_FIXED_ARRAY_TEST_COUNT, &cmeta_data_int,
    "test.FixedIntArray3", "FixedIntArray3");

#endif
