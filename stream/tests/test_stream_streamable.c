#include "stream_streamable.h"
#include "tinytest.h"

typedef struct {
    const int *values;
    size_t count;
    uint64_t version;
} test_container_t;

typedef struct {
    size_t index;
    uint64_t version;
} test_cursor_t;

static stream_result_t test_container_next(
    const test_container_t *container,
    test_cursor_t *cursor,
    const int **out)
{
    if (cursor->index == 0) {
        cursor->version = container->version;
    }
    if (cursor->version != container->version) {
        return STREAM_MODIFIED;
    }
    if (cursor->index == container->count) {
        return STREAM_END;
    }
    *out = &container->values[cursor->index++];
    return STREAM_OK;
}

STREAMABLE_REF(test_container, test_container_t, test_cursor_t, int, test_container_next)

spec("streamable adapter") {
    it("keeps independent cursor state per stream") {
        const int values[] = {10, 20};
        test_container_t container = {values, 2, 0};
        stream_t first;
        stream_t second;
        stream_item_t first_item;
        stream_item_t second_item;

        check_equal(test_container_stream(&first, &container), STREAM_OK);
        check_equal(test_container_stream(&second, &container), STREAM_OK);
        check_equal(stream_next_view(&first, &first_item), STREAM_OK);
        check_equal(stream_next_view(&second, &second_item), STREAM_OK);
        check_equal(*(const int *)first_item.data, 10);
        check_equal(*(const int *)second_item.data, 10);

        ++container.version;
        check_equal(stream_next_view(&first, &first_item), STREAM_MODIFIED);
    }
}
