#ifndef TBE_SCHEMA_BUILTIN_TYPE_H
#define TBE_SCHEMA_BUILTIN_TYPE_H

#include <stddef.h>
#include <string.h>

typedef struct schema_builtin_type_info {
    const char *name;
    size_t size;
    const char *wire_reader;
    const char *host_type;
    unsigned char is_integer;
    unsigned char is_unsigned;
    unsigned char is_float;
} schema_builtin_type_info_t;

static const schema_builtin_type_info_t SCHEMA_BUILTIN_TYPES[] = {
    {"bool", 1, "u8", "uint8_t", 0, 0, 0},
    {"uint8_t", 1, "u8", "uint8_t", 1, 1, 0},
    {"uint8", 1, "u8", "uint8_t", 1, 1, 0},
    {"u8", 1, "u8", "uint8_t", 1, 1, 0},
    {"byte", 1, "u8", "uint8_t", 1, 1, 0},
    {"int8_t", 1, "i8", "int8_t", 1, 0, 0},
    {"int8", 1, "i8", "int8_t", 1, 0, 0},
    {"i8", 1, "i8", "int8_t", 1, 0, 0},
    {"uint16_t", 2, "u16", "uint16_t", 1, 1, 0},
    {"uint16", 2, "u16", "uint16_t", 1, 1, 0},
    {"u16", 2, "u16", "uint16_t", 1, 1, 0},
    {"int16_t", 2, "i16", "int16_t", 1, 0, 0},
    {"int16", 2, "i16", "int16_t", 1, 0, 0},
    {"i16", 2, "i16", "int16_t", 1, 0, 0},
    {"uint32_t", 4, "u32", "uint32_t", 1, 1, 0},
    {"uint32", 4, "u32", "uint32_t", 1, 1, 0},
    {"u32", 4, "u32", "uint32_t", 1, 1, 0},
    {"int32_t", 4, "i32", "int32_t", 1, 0, 0},
    {"int32", 4, "i32", "int32_t", 1, 0, 0},
    {"i32", 4, "i32", "int32_t", 1, 0, 0},
    {"uint64_t", 8, "u64", "uint64_t", 1, 1, 0},
    {"uint64", 8, "u64", "uint64_t", 1, 1, 0},
    {"u64", 8, "u64", "uint64_t", 1, 1, 0},
    {"int64_t", 8, "i64", "int64_t", 1, 0, 0},
    {"int64", 8, "i64", "int64_t", 1, 0, 0},
    {"i64", 8, "i64", "int64_t", 1, 0, 0},
    {"float", 4, "f32", "float", 0, 0, 1},
    {"double", 8, "f64", "double", 0, 0, 1},
};

static inline const schema_builtin_type_info_t *schema_builtin_type_find(const char *name) {
    size_t i;
    if (name == NULL) return NULL;
    for (i = 0; i < sizeof(SCHEMA_BUILTIN_TYPES) / sizeof(SCHEMA_BUILTIN_TYPES[0]); ++i) {
        if (strcmp(name, SCHEMA_BUILTIN_TYPES[i].name) == 0) return &SCHEMA_BUILTIN_TYPES[i];
    }
    return NULL;
}

#endif
