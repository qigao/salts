#include "stream_typed.h"
#include "tinytest.h"

typedef struct {
    int value;
} typed_input_t;

static bool typed_positive(const typed_input_t *input)
{
    return input->value > 0;
}

static int typed_double(const typed_input_t *input)
{
    return input->value * 2;
}

STREAM_DEFINE_PREDICATE(positive_adapter, typed_input_t, typed_positive)
STREAM_DEFINE_MAPPER(double_adapter, typed_input_t, int, typed_double)

spec("typed stream adapters") {
    it("preserves typed callback values through the erased ABI") {
        typed_input_t values[] = {{-1}, {2}, {3}};
        stream_array_source_state_t source_state;
        stream_t stream;
        stream_item_t item;
        int output = 0;

        check_int_eq(STREAM_ARRAY_INIT(&stream, &source_state, values), STREAM_OK);
        STREAM_FILTER_TYPED(&stream, positive_adapter);
        STREAM_MAP_TYPED(&stream, int, double_adapter);

        item.data = &output;
        item.size = sizeof(output);
        check_int_eq(stream_next(&stream, &item), STREAM_OK);
        check_int_eq(output, 4);
        item.size = sizeof(output);
        check_int_eq(stream_next(&stream, &item), STREAM_OK);
        check_int_eq(output, 6);
        item.size = sizeof(output);
        check_int_eq(stream_next(&stream, &item), STREAM_END);
    }
}
