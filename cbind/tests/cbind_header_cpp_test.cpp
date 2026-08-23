#include <cbind/cbind.h>
#include "tinytest.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(std::is_standard_layout<cbind_context>::value,
              "context ABI must remain standard layout");
static_assert(std::is_standard_layout<cbind_error>::value,
              "error ABI must remain standard layout");
static_assert(CBIND_CONTEXT_ABI_VERSION == 1u, "context ABI version");
static_assert(CBIND_ERROR_ABI_VERSION == 1u, "error ABI version");

struct cpp_reader_context {
    bool emitted;
};

static cserde_status cpp_reader_next(void *context, cserde_token *out) {
    auto *state = static_cast<cpp_reader_context *>(context);

    if (state->emitted)
        return CSERDE_DONE;
    out->kind = CSERDE_SINT;
    out->value.sint = 7;
    state->emitted = true;
    return CSERDE_OK;
}

spec("CBind C++17 public linkage") {
  it("decodes through the installed C ABI surface") {
    cpp_reader_context reader_context{};
    cserde_reader_ops reader_ops = {
        offsetof(cserde_reader_ops, next) + sizeof(cserde_reader_next_fn),
        static_cast<std::uint32_t>(CSERDE_READER_OPS_ABI_VERSION),
        cpp_reader_next
    };
    cserde_reader reader{};
    cbind_context context = CBIND_CONTEXT_INIT(nullptr, 0u, 0u);
    cbind_error error = CBIND_ERROR_INIT;
    int out = 0;

    check_equal(cserde_reader_init(&reader, &reader_ops, &reader_context),
                CSERDE_OK);
    check_equal(cbind_decode(&context, &cmeta_data_int, &reader, &out, &error),
                CBIND_OK);
    check_equal(out, 7);
    check_equal(error.status, CBIND_OK);
    check_equal(error.source_status, CSERDE_OK);
  }
}
