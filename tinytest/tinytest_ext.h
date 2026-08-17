#ifndef TINYTEST_EXT_H
#define TINYTEST_EXT_H

#include "tinytest.h"

/*
 * Lightweight parameterized test helper.
 *
 * Example:
 *   #define CHECK_POSITIVE(value) check((value) > 0)
 *   static const int values[] = {1, 2, 3};
 *   TINYTEST_PARAM_TEST("positive values", values,
 *                       sizeof(values) / sizeof(values[0]), CHECK_POSITIVE)
 */
#define TINYTEST_PARAM_TEST(name, values, count, body)                           \
  spec(name) {                                                                  \
    size_t __tinytest_param_index;                                              \
    for (__tinytest_param_index = 0;                                            \
         __tinytest_param_index < (count);                                      \
         ++__tinytest_param_index) {                                            \
      body((values)[__tinytest_param_index]);                                   \
    }                                                                           \
  }

#endif