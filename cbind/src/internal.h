#ifndef CBIND_INTERNAL_H
#define CBIND_INTERNAL_H

#include <cbind/cbind.h>
#include <cmeta/entry.h>
#include <cmeta/range.h>

#include <stdbool.h>
#include <stddef.h>

#define CBIND_FIELD_END(type, member) \
    (offsetof(type, member) + sizeof(((type *)0)->member))

typedef struct cbind_decode_state {
    const cbind_context *context;
    cserde_reader *reader;
    cbind_error *error;
    unsigned char *scratch;
    size_t scratch_used;
} cbind_decode_state;

typedef struct cbind_validation_frame {
    const cmeta_data_desc *shape;
    const struct cbind_validation_frame *parent;
} cbind_validation_frame;

bool cbind_validation_cycle_contains(
    const cbind_validation_frame *parent,
    const cmeta_data_desc *shape);

bool cbind_context_valid(const cbind_context *context);
bool cbind_error_valid(const cbind_error *error);
void cbind_error_clear(cbind_error *error);
void cbind_error_set(cbind_error *error,
                     cbind_status status,
                     cserde_status source_status,
                     const cmeta_data_desc *shape,
                     const cmeta_data_field_desc *field,
                     size_t depth);
void cbind_error_set_target(cbind_error *error, cmeta_status target_status);

cbind_status cbind_validate_graph(
    const cbind_context *context,
    const cmeta_data_desc *shape,
    size_t depth,
    const cbind_validation_frame *parent,
    size_t active_scratch,
    size_t *max_scratch,
    cbind_error *error);

cbind_status cbind_validate_struct_graph(
    const cbind_context *context,
    const cmeta_data_desc *shape,
    size_t depth,
    const cbind_validation_frame *parent,
    size_t active_scratch,
    size_t *max_scratch,
    cbind_error *error);

cbind_status cbind_validate_variant_graph(
    const cbind_context *context,
    const cmeta_data_desc *shape,
    size_t depth,
    const cbind_validation_frame *parent,
    size_t active_scratch,
    size_t *max_scratch,
    cbind_error *error);

cbind_status cbind_measure_variant_resources(
    const cbind_context *context,
    const cmeta_data_desc *shape,
    size_t depth,
    size_t active_scratch,
    size_t *max_scratch,
    cbind_error *error);

size_t cbind_bitmap_bytes(size_t field_count);
bool cbind_struct_is_empty(const cmeta_data_desc *shape, const void *value);
void cbind_struct_reset(const cmeta_data_desc *shape, void *value);
cbind_status cbind_prepare_struct_containers(
    const cmeta_data_desc *shape, void *value, cbind_error *error,
    size_t depth);

bool cbind_data_kind_is_container(cmeta_data_kind kind);
bool cbind_data_kind_is_buffer(cmeta_data_kind kind);
const cmeta_data_desc *cbind_scalar_shape_for_type(
    const cmeta_type_desc *type);
cbind_status cbind_validate_container_field(
    const cbind_context *context,
    const cmeta_data_desc *shape,
    const cmeta_field_desc *reflected,
    const cmeta_data_field_desc *field,
    size_t depth,
    cbind_error *error);
bool cbind_container_is_zero(const cmeta_field_desc *reflected,
                             const void *value);
void cbind_container_reset(const cmeta_field_desc *reflected, void *value);

cbind_status cbind_validate_buffer(
    const cbind_context *context,
    const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field,
    size_t depth,
    cbind_error *error);
cbind_status cbind_decode_buffer(
    cbind_decode_state *state,
    const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field,
    size_t depth,
    void *out);

const cmeta_data_field_desc *cbind_find_field_slice(
    const cmeta_data_struct_shape *shape,
    const cserde_slice *key,
    size_t *index);

bool cbind_value_is_empty(const cmeta_data_desc *shape, const void *value);
void cbind_value_reset(const cmeta_data_desc *shape, void *value);

cbind_status cbind_read_required(
    cbind_decode_state *state,
    cserde_token *token,
    const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field,
    size_t depth);

cbind_status cbind_decode_value(
    cbind_decode_state *state,
    const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field,
    const cmeta_field_desc *reflected,
    size_t depth,
    void *out);

cbind_status cbind_decode_container(
    cbind_decode_state *state,
    const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field,
    const cmeta_field_desc *reflected,
    size_t depth,
    void *out);

cbind_status cbind_decode_scalar(
    cbind_decode_state *state,
    const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field,
    size_t depth,
    void *out);

cbind_status cbind_decode_scalar_token(
    cbind_decode_state *state,
    const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field,
    size_t depth,
    const cserde_token *token,
    void *out);

cbind_status cbind_enum_value_from_token(
    cbind_decode_state *state,
    const cmeta_data_desc *shape,
    const cmeta_data_field_desc *field,
    size_t depth,
    const cserde_token *token,
    int64_t *out);

cbind_status cbind_decode_struct(
    cbind_decode_state *state,
    const cmeta_data_desc *shape,
    const cmeta_data_field_desc *parent_field,
    size_t depth,
    void *out);

cbind_status cbind_decode_variant(
    cbind_decode_state *state,
    const cmeta_data_desc *shape,
    const cmeta_data_field_desc *parent_field,
    size_t depth,
    void *out);

#endif /* CBIND_INTERNAL_H */
