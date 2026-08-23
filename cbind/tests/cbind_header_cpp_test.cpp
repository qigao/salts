#include <cbind/cbind.h>
#include "recording.h"
#include "tinytest.hpp"

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout<cbind_context>::value,
              "context ABI must remain standard layout");
static_assert(std::is_standard_layout<cbind_error>::value,
              "error ABI must remain standard layout");
static_assert(CBIND_CONTEXT_ABI_VERSION == 1u, "context ABI version");
static_assert(CBIND_ERROR_ABI_VERSION == 1u, "error ABI version");

spec("CBind C++17 public linkage") {
  it("decodes through the C ABI surface") {
    cserde_token tokens[1]{};
    cserde_recording_reader_context source{};
    cserde_reader reader{};
    cbind_context context = CBIND_CONTEXT_INIT(nullptr, 0u, 0u);
    cbind_error error = CBIND_ERROR_INIT;
    int out = 0;

    tokens[0].kind = CSERDE_SINT;
    tokens[0].value.sint = 7;
    source.tokens = tokens;
    source.count = 1u;
    source.index = 0u;

    check_equal(cserde_reader_init(&reader, &cserde_recording_reader_ops, &source),
                CSERDE_OK);
    check_equal(cbind_decode(&context, &cmeta_data_int, &reader, &out, &error),
                CBIND_OK);
    check_equal(out, 7);
    check_equal(error.status, CBIND_OK);
    check_equal(error.source_status, CSERDE_OK);
  }
}
