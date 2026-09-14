#ifndef CMETA_FIXED_BYTES_FIXTURE_H
#define CMETA_FIXED_BYTES_FIXTURE_H

#include <cmeta/data.h>

typedef unsigned char cmeta_fixed_bytes_fixture[6];

CMETA_DEFINE_FIXED_BYTES(cmeta_fixed_bytes_fixture_value,
                         cmeta_fixed_bytes_fixture,
                         sizeof(cmeta_fixed_bytes_fixture),
                         "test.fixed-bytes-6",
                         "Fixed bytes 6");

#endif
