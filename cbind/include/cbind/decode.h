#ifndef CBIND_DECODE_H
#define CBIND_DECODE_H

#include <cbind/context.h>
#include <cbind/error.h>
#include <cbind/status.h>
#include <cmeta/data.h>
#include <cserde/reader.h>

#ifdef __cplusplus
extern "C" {
#endif

cbind_status cbind_decode(
    const cbind_context *context,
    const cmeta_data_desc *shape,
    cserde_reader *reader,
    void *out,
    cbind_error *error);

#ifdef __cplusplus
}
#endif

#endif /* CBIND_DECODE_H */
