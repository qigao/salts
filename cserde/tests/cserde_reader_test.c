#include <cserde/reader.h>
#include "tinytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct fake_reader_context {
    cserde_status next_status;
    cserde_token token;
    size_t calls;
} fake_reader_context;

static cserde_status fake_reader_next(void *context, cserde_token *out) {
    fake_reader_context *state = (fake_reader_context *)context;

    ++state->calls;
    if (state->next_status == CSERDE_OK)
        *out = state->token;
    return state->next_status;
}

static cserde_status null_context_reader_next(void *context,
                                               cserde_token *out) {
    if (context != NULL)
        return CSERDE_SOURCE_ERROR;
    *out = (cserde_token){ .kind = CSERDE_NULL };
    return CSERDE_OK;
}

#define READER_PREFIX \
    (offsetof(cserde_reader_ops, next) + \
     sizeof(((cserde_reader_ops *)0)->next))

static cserde_reader_ops reader_ops(cserde_reader_next_fn next) {
    return (cserde_reader_ops){
        .struct_size = READER_PREFIX,
        .abi_version = CSERDE_READER_OPS_ABI_VERSION,
        .next = next
    };
}

static cserde_token sentinel_token(void) {
    return (cserde_token){
        .kind = CSERDE_UINT,
        .value.uint = UINT64_C(0x1234)
    };
}

spec("CSerde pull reader") {
    it("accepts exact v1 ops prefix") {
        fake_reader_context context = { .next_status = CSERDE_DONE };
        cserde_reader_ops ops = reader_ops(fake_reader_next);
        cserde_reader reader = { 0 };

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_true(reader.ops == &ops);
        check_true(reader.context == &context);
        check_true(reader.state == CSERDE_READER_READY);
        check_true(reader.status == CSERDE_OK);
    }

    it("rejects short prefix without mutating zero reader") {
        fake_reader_context context = { .next_status = CSERDE_DONE };
        cserde_reader_ops ops = reader_ops(fake_reader_next);
        cserde_reader reader = { 0 };
        const cserde_reader zero = { 0 };

        ops.struct_size = READER_PREFIX - 1u;
        check_equal(cserde_reader_init(&reader, &ops, &context),
                    CSERDE_INVALID_ARGUMENT);
        check_equal(memcmp(&reader, &zero, sizeof(reader)), 0);
    }

    it("rejects wrong ABI without mutation") {
        fake_reader_context context = { .next_status = CSERDE_DONE };
        cserde_reader_ops ops = reader_ops(fake_reader_next);
        cserde_reader reader = { 0 };
        const cserde_reader zero = { 0 };

        ops.abi_version = CSERDE_READER_OPS_ABI_VERSION + 1u;
        check_equal(cserde_reader_init(&reader, &ops, &context),
                    CSERDE_INVALID_ARGUMENT);
        check_equal(memcmp(&reader, &zero, sizeof(reader)), 0);
    }

    it("rejects null next callback without mutation") {
        cserde_reader_ops ops = reader_ops(NULL);
        cserde_reader reader = { 0 };
        const cserde_reader zero = { 0 };

        check_equal(cserde_reader_init(&reader, &ops, NULL),
                    CSERDE_INVALID_ARGUMENT);
        check_equal(memcmp(&reader, &zero, sizeof(reader)), 0);
    }

    it("accepts null provider context") {
        cserde_reader_ops ops = reader_ops(null_context_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();

        check_equal(cserde_reader_init(&reader, &ops, NULL), CSERDE_OK);
        check_equal(cserde_reader_next(&reader, &out), CSERDE_OK);
        check_true(out.kind == CSERDE_NULL);
    }

    it("commits only a valid callback token") {
        fake_reader_context context = {
            .next_status = CSERDE_OK,
            .token = { .kind = CSERDE_SINT, .value.sint = INT64_C(-7) }
        };
        cserde_reader_ops ops = reader_ops(fake_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_equal(cserde_reader_next(&reader, &out), CSERDE_OK);
        check_true(out.kind == CSERDE_SINT);
        check_equal(out.value.sint, INT64_C(-7));
        check_true(reader.state == CSERDE_READER_READY);
        check_true(reader.status == CSERDE_OK);
        check_equal(context.calls, (size_t)1u);
    }

    it("rejects invalid callback token and preserves output") {
        fake_reader_context context = {
            .next_status = CSERDE_OK,
            .token = {
                .kind = CSERDE_STRING,
                .value.slice = { NULL, 1u, CSERDE_VIEW_STABLE }
            }
        };
        cserde_reader_ops ops = reader_ops(fake_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_equal(cserde_reader_next(&reader, &out), CSERDE_INVALID_TOKEN);
        check_true(out.kind == CSERDE_UINT);
        check_equal(out.value.uint, UINT64_C(0x1234));
        check_true(reader.state == CSERDE_READER_FAILED);
        check_true(reader.status == CSERDE_INVALID_TOKEN);
        check_equal(cserde_reader_next(&reader, &out), CSERDE_INVALID_TOKEN);
        check_equal(context.calls, (size_t)1u);
    }

    it("makes done terminal without reinvoking provider") {
        fake_reader_context context = { .next_status = CSERDE_DONE };
        cserde_reader_ops ops = reader_ops(fake_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_equal(cserde_reader_next(&reader, &out), CSERDE_DONE);
        check_true(reader.state == CSERDE_READER_DONE);
        check_true(reader.status == CSERDE_DONE);
        check_equal(cserde_reader_next(&reader, &out), CSERDE_DONE);
        check_equal(context.calls, (size_t)1u);
        check_true(out.kind == CSERDE_UINT);
        check_equal(out.value.uint, UINT64_C(0x1234));
    }

    it("keeps allowed provider errors sticky without touching output") {
        const cserde_status errors[] = {
            CSERDE_VALUE_OUT_OF_RANGE,
            CSERDE_LIMIT_EXCEEDED,
            CSERDE_UNSUPPORTED,
            CSERDE_SOURCE_ERROR
        };
        size_t i;

        for (i = 0u; i < sizeof(errors) / sizeof(errors[0]); ++i) {
            fake_reader_context context = { .next_status = errors[i] };
            cserde_reader_ops ops = reader_ops(fake_reader_next);
            cserde_reader reader = { 0 };
            cserde_token out = sentinel_token();

            check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
            check_equal(cserde_reader_next(&reader, &out), errors[i]);
            check_true(reader.state == CSERDE_READER_FAILED);
            check_true(reader.status == errors[i]);
            check_true(out.kind == CSERDE_UINT);
            check_equal(out.value.uint, UINT64_C(0x1234));
            check_equal(cserde_reader_next(&reader, &out), errors[i]);
            check_equal(context.calls, (size_t)1u);
        }
    }

    it("normalizes disallowed provider status to callback error") {
        const cserde_status statuses[] = {
            CSERDE_SINK_ERROR,
            (cserde_status)999
        };
        size_t i;

        for (i = 0u; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
            fake_reader_context context = { .next_status = statuses[i] };
            cserde_reader_ops ops = reader_ops(fake_reader_next);
            cserde_reader reader = { 0 };
            cserde_token out = sentinel_token();

            check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
            check_equal(cserde_reader_next(&reader, &out), CSERDE_CALLBACK_ERROR);
            check_true(reader.state == CSERDE_READER_FAILED);
            check_true(reader.status == CSERDE_CALLBACK_ERROR);
            check_equal(context.calls, (size_t)1u);
        }
    }

    it("does not poison ready reader on caller precondition errors") {
        fake_reader_context context = { .next_status = CSERDE_DONE };
        cserde_reader_ops ops = reader_ops(fake_reader_next);
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();

        check_equal(cserde_reader_init(&reader, &ops, &context), CSERDE_OK);
        check_equal(cserde_reader_next(NULL, &out), CSERDE_INVALID_ARGUMENT);
        check_equal(cserde_reader_next(&reader, NULL), CSERDE_INVALID_ARGUMENT);
        check_true(reader.state == CSERDE_READER_READY);
        check_true(reader.status == CSERDE_OK);
        check_equal(context.calls, (size_t)0u);
    }

    it("rejects next on zero reader without callback") {
        cserde_reader reader = { 0 };
        cserde_token out = sentinel_token();

        check_equal(cserde_reader_next(&reader, &out), CSERDE_INVALID_STATE);
        check_true(reader.state == CSERDE_READER_ZERO);
        check_true(reader.status == CSERDE_OK);
    }
}
