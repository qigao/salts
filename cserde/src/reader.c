#include <cserde/reader.h>

#include <stdbool.h>
#include <stddef.h>

#define CSERDE_FIELD_END(type, field) \
    (offsetof(type, field) + sizeof(((type *)0)->field))

static bool cserde_reader_ops_valid(const cserde_reader_ops *ops) {
    return ops != NULL &&
           ops->struct_size >= CSERDE_FIELD_END(cserde_reader_ops, next) &&
           ops->abi_version == CSERDE_READER_OPS_ABI_VERSION &&
           ops->next != NULL;
}

static bool cserde_reader_callback_status_valid(cserde_status status) {
    switch (status) {
        case CSERDE_OK:
        case CSERDE_DONE:
        case CSERDE_VALUE_OUT_OF_RANGE:
        case CSERDE_LIMIT_EXCEEDED:
        case CSERDE_UNSUPPORTED:
        case CSERDE_SOURCE_ERROR:
            return true;
        default:
            return false;
    }
}

static cserde_status cserde_reader_fail(cserde_reader *reader,
                                        cserde_status status) {
    reader->state = CSERDE_READER_FAILED;
    reader->status = status;
    return status;
}

cserde_status cserde_reader_init(cserde_reader *reader,
                                  const cserde_reader_ops *ops,
                                  void *context) {
    cserde_reader initialized;

    if (reader == NULL || ops == NULL)
        return CSERDE_INVALID_ARGUMENT;
    if (reader->ops != NULL || reader->context != NULL ||
        reader->state != CSERDE_READER_ZERO || reader->status != CSERDE_OK)
        return CSERDE_INVALID_STATE;
    if (!cserde_reader_ops_valid(ops))
        return CSERDE_INVALID_ARGUMENT;

    initialized.ops = ops;
    initialized.context = context;
    initialized.state = CSERDE_READER_READY;
    initialized.status = CSERDE_OK;
    *reader = initialized;
    return CSERDE_OK;
}

cserde_status cserde_reader_next(cserde_reader *reader, cserde_token *out) {
    cserde_token token = { 0 };
    cserde_status status;

    if (reader == NULL || out == NULL)
        return CSERDE_INVALID_ARGUMENT;
    if (reader->state == CSERDE_READER_DONE)
        return CSERDE_DONE;
    if (reader->state == CSERDE_READER_FAILED)
        return reader->status;
    if (reader->state != CSERDE_READER_READY)
        return CSERDE_INVALID_STATE;

    status = reader->ops->next(reader->context, &token);
    if (!cserde_reader_callback_status_valid(status))
        return cserde_reader_fail(reader, CSERDE_CALLBACK_ERROR);
    if (status == CSERDE_OK) {
        if (!cserde_token_valid(&token))
            return cserde_reader_fail(reader, CSERDE_INVALID_TOKEN);
        *out = token;
        return CSERDE_OK;
    }
    if (status == CSERDE_DONE) {
        reader->state = CSERDE_READER_DONE;
        reader->status = CSERDE_DONE;
        return CSERDE_DONE;
    }
    return cserde_reader_fail(reader, status);
}
