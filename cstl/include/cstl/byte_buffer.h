/**
 * @file byte_buffer.h
 * @brief Canonical owning byte-buffer storage and CMeta lifecycle provider.
 */
#ifndef CSTL_BYTE_BUFFER_H
#define CSTL_BYTE_BUFFER_H

#include <cstl/vec.h>
#include <cmeta/data.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stl_byte_buffer {
  vec_t raw;
} stl_byte_buffer;

stl_status stl_byte_buffer_init(stl_byte_buffer *buffer, size_t byte_limit);
stl_status stl_byte_buffer_from(stl_byte_buffer *buffer,
                                const unsigned char *bytes,
                                size_t size,
                                size_t byte_limit);
void stl_byte_buffer_destroy(stl_byte_buffer *buffer);
stl_status stl_byte_buffer_clear(stl_byte_buffer *buffer);
stl_status stl_byte_buffer_reserve(stl_byte_buffer *buffer, size_t capacity);
stl_status stl_byte_buffer_resize(stl_byte_buffer *buffer, size_t size);
stl_status stl_byte_buffer_push(stl_byte_buffer *buffer, unsigned char value);
stl_status stl_byte_buffer_pop(stl_byte_buffer *buffer, unsigned char *out_value);
unsigned char *stl_byte_buffer_at(stl_byte_buffer *buffer, size_t index);
const unsigned char *stl_byte_buffer_at_const(const stl_byte_buffer *buffer,
                                              size_t index);
unsigned char *stl_byte_buffer_data(stl_byte_buffer *buffer);
const unsigned char *stl_byte_buffer_data_const(const stl_byte_buffer *buffer);
size_t stl_byte_buffer_size(const stl_byte_buffer *buffer);
size_t stl_byte_buffer_capacity(const stl_byte_buffer *buffer);
bool stl_byte_buffer_empty(const stl_byte_buffer *buffer);

/** Canonical semantic metadata for generated/native dynamic byte storage. */
extern const cmeta_type_desc stl_byte_buffer_cmeta_type;
extern const cmeta_data_buffer_shape stl_byte_buffer_cmeta_shape;
extern const cmeta_data_buffer_ops stl_byte_buffer_cmeta_buffer_ops;
extern const cmeta_data_desc stl_byte_buffer_cmeta_data;

#ifdef __cplusplus
}
#endif

#endif /* CSTL_BYTE_BUFFER_H */
