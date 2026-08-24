#include "turbo_cmeta_data.h"
#include "turbo_str.h"
#include "turbo_vstr.h"
#include "tinytest.h"

#include <stdint.h>
#include <string.h>

static const cmeta_data_buffer_shape owned_shape = {
    .ownership = CMETA_DATA_BUFFER_OWNED
};
static const cmeta_data_buffer_shape borrowed_shape = {
    .ownership = CMETA_DATA_BUFFER_BORROWED
};

static const cmeta_data_desc tstr_bytes = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.tstr.bytes",
    .display_name = "tstr bytes",
    .kind = CMETA_DATA_BYTES,
    .storage_type = &turbo_tstr_cmeta_type,
    .shape = &owned_shape,
    .buffer_ops = &turbo_tstr_cmeta_buffer_ops
};

static const cmeta_data_desc vstr_string = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.vstr.string",
    .display_name = "vstr string",
    .kind = CMETA_DATA_STRING,
    .storage_type = &turbo_vstr_cmeta_type,
    .shape = &borrowed_shape,
    .buffer_ops = &turbo_vstr_cmeta_buffer_ops
};

spec("TurboUtils CMeta buffer adapters") {
  it("copies exact owned tstr bytes including embedded NUL") {
    static const unsigned char input[] = {'a', 0, 'b'};
    tstr value = NULL;
    bool is_zero = false;

    check_equal(cmeta_data_buffer_assign(&tstr_bytes, &value, input,
                                         sizeof(input), sizeof(input)),
                CMETA_OK);
    check_not_null(value);
    check_equal(tstr_len(value), sizeof(input));
    check_equal(memcmp(value, input, sizeof(input)), 0);
    check_equal(cmeta_data_buffer_is_zero(&tstr_bytes, &value, &is_zero),
                CMETA_OK);
    check_false(is_zero);

    check_equal(cmeta_data_buffer_restore_zero(&tstr_bytes, &value), CMETA_OK);
    check_null(value);
  }

  it("keeps owned tstr zero for empty input and failed allocation") {
    static const unsigned char byte = 0;
    tstr value = NULL;

    check_equal(cmeta_data_buffer_assign(&tstr_bytes, &value, NULL, 0u, 0u),
                CMETA_OK);
    check_null(value);

    check_equal(cmeta_data_buffer_assign(&tstr_bytes, &value, &byte,
                                         SIZE_MAX, SIZE_MAX),
                CMETA_OUT_OF_MEMORY);
    check_null(value);
  }

  it("borrows vstr input without copying and restores canonical zero") {
    static const unsigned char input[] = {'u', 't', 'f', '8'};
    vstr value = {NULL, 0u};

    check_equal(cmeta_data_buffer_assign(&vstr_string, &value, input,
                                         sizeof(input), sizeof(input)),
                CMETA_OK);
    check_true(value.data == (const char *)input);
    check_equal(value.len, sizeof(input));

    check_equal(cmeta_data_buffer_restore_zero(&vstr_string, &value),
                CMETA_OK);
    check_null(value.data);
    check_equal(value.len, (size_t)0u);
  }

  it("canonicalizes an empty borrowed view") {
    static const unsigned char sentinel = 0;
    vstr value = {NULL, 0u};

    check_equal(cmeta_data_buffer_assign(&vstr_string, &value, &sentinel,
                                         0u, 0u), CMETA_OK);
    check_null(value.data);
    check_equal(value.len, (size_t)0u);
  }
}
