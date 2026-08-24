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
static_assert(CBIND_SOURCE_ERROR + 1 == CBIND_TARGET_ERROR,
              "new statuses append without renumbering D2 values");

spec("CBind C++17 public linkage") {
  it("decodes through the C ABI surface") {
    cserde_token tokens[1]{};
    cserde_recording_reader_context source{};
    cserde_reader reader{};
    cbind_context context = CBIND_CONTEXT_INIT(nullptr, 0u, 0u);
    cbind_context container_context =
        CBIND_CONTEXT_WITH_CONTAINERS_INIT(nullptr, 0u, 0u, 8u);
    cbind_context buffer_context =
        CBIND_CONTEXT_WITH_BUFFERS_INIT(nullptr, 0u, 0u, 8u, 64u);
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
    check_equal(error.target_status, CMETA_OK);
    check_equal(context.max_container_items, static_cast<std::size_t>(0u));
    check_equal(context.max_buffer_bytes, static_cast<std::size_t>(0u));
    check_equal(container_context.max_container_items,
                static_cast<std::size_t>(8u));
    check_equal(container_context.max_buffer_bytes,
                static_cast<std::size_t>(0u));
    check_equal(buffer_context.max_container_items,
                static_cast<std::size_t>(8u));
    check_equal(buffer_context.max_buffer_bytes,
                static_cast<std::size_t>(64u));
  }
}
