#include "compiler_core.h"

#include "mustache.h"
#include "mustache_helpers.h"
#include "schema_parser_dsl.h"
#include "tbe_error.h"
#include "data_bind.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Node *tbe_compiler_find_child(Node *parent, const char *name) {
  if (!parent || !name) return NULL;

  if (parent->type == NODE_MAP) {
    for (size_t i = 0; i < parent->data.map.count; ++i) {
      Node *child = parent->data.map.items[i];
      if (child && child->name && strcmp(child->name, name) == 0) return child;
    }
  } else if (parent->type == NODE_LIST) {
    for (size_t i = 0; i < parent->data.list.count; ++i) {
      Node *child = parent->data.list.items[i];
      if (child && child->name && strcmp(child->name, name) == 0) return child;
    }
  }

  return NULL;
}

static const char *tbe_compiler_string_value(Node *parent, const char *name) {
  Node *child = tbe_compiler_find_child(parent, name);
  if (!child || child->type != NODE_STRING) return NULL;
  return child->data.string_val;
}

static int tbe_compiler_has_child(Node *parent, const char *name) {
  return tbe_compiler_find_child(parent, name) != NULL;
}

static void tbe_compiler_remove_children(Node *map, const char *name) {
  size_t out = 0;

  if (!map || map->type != NODE_MAP || !name) return;

  for (size_t i = 0; i < map->data.map.count; ++i) {
    Node *child = map->data.map.items[i];
    if (child && child->name && strcmp(child->name, name) == 0) {
      node_free(child);
      continue;
    }
    map->data.map.items[out++] = child;
  }

  map->data.map.count = out;
}

static int tbe_compiler_set_string(Node *map, const char *name, const char *value) {
  Node *node;

  if (!map || !name || !value) return -1;

  node = create_node_string(name, value);
  if (!node) return -1;

  tbe_compiler_remove_children(map, name);
  if (map_add(map, node) != 0) {
    node_free(node);
    return -1;
  }

  return 0;
}

static void tbe_compiler_pascal_identifier(const char *input, char *out, size_t out_size) {
  int capitalize = 1;
  size_t pos = 0;

  if (!out || out_size == 0) return;

  if (!input || !input[0]) {
    snprintf(out, out_size, "Field");
    return;
  }

  for (size_t i = 0; input[i] && pos + 1 < out_size; ++i) {
    char c = input[i];
    int is_alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    int is_digit = c >= '0' && c <= '9';

    if (!is_alpha && !is_digit) {
      capitalize = 1;
      continue;
    }

    if (pos == 0 && is_digit && pos + 1 < out_size) {
      out[pos++] = 'F';
    }

    if (capitalize && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    out[pos++] = c;
    capitalize = 0;
  }

  if (pos == 0) out[pos++] = 'F';
  out[pos] = '\0';
}

static const char *tbe_compiler_cpp_scalar_type(const char *type) {
  if (!type) return "std::any";
  if (strcmp(type, "bool") == 0) return "bool";
  if (strcmp(type, "byte") == 0 || strcmp(type, "uint8") == 0 || strcmp(type, "uint8_t") == 0) return "std::uint8_t";
  if (strcmp(type, "int8") == 0 || strcmp(type, "int8_t") == 0) return "std::int8_t";
  if (strcmp(type, "uint16") == 0 || strcmp(type, "uint16_t") == 0) return "std::uint16_t";
  if (strcmp(type, "int16") == 0 || strcmp(type, "int16_t") == 0) return "std::int16_t";
  if (strcmp(type, "uint32") == 0 || strcmp(type, "uint32_t") == 0) return "std::uint32_t";
  if (strcmp(type, "int32") == 0 || strcmp(type, "int32_t") == 0) return "std::int32_t";
  if (strcmp(type, "uint64") == 0 || strcmp(type, "uint64_t") == 0) return "std::uint64_t";
  if (strcmp(type, "int64") == 0 || strcmp(type, "int64_t") == 0) return "std::int64_t";
  if (strcmp(type, "float") == 0) return "float";
  if (strcmp(type, "double") == 0) return "double";
  if (strcmp(type, "string") == 0) return "std::string";
  if (strcmp(type, "bytes") == 0) return "std::vector<std::uint8_t>";
  if (strcmp(type, "uuid") == 0) return "turbo_uuid_t";
  return type;
}

static const char *tbe_compiler_go_scalar_type(const char *type) {
  if (!type) return "any";
  if (strcmp(type, "bool") == 0) return "bool";
  if (strcmp(type, "byte") == 0 || strcmp(type, "uint8") == 0 || strcmp(type, "uint8_t") == 0) return "uint8";
  if (strcmp(type, "int8") == 0 || strcmp(type, "int8_t") == 0) return "int8";
  if (strcmp(type, "uint16") == 0 || strcmp(type, "uint16_t") == 0) return "uint16";
  if (strcmp(type, "int16") == 0 || strcmp(type, "int16_t") == 0) return "int16";
  if (strcmp(type, "uint32") == 0 || strcmp(type, "uint32_t") == 0) return "uint32";
  if (strcmp(type, "int32") == 0 || strcmp(type, "int32_t") == 0) return "int32";
  if (strcmp(type, "uint64") == 0 || strcmp(type, "uint64_t") == 0) return "uint64";
  if (strcmp(type, "int64") == 0 || strcmp(type, "int64_t") == 0) return "int64";
  if (strcmp(type, "float") == 0) return "float32";
  if (strcmp(type, "double") == 0) return "float64";
  if (strcmp(type, "string") == 0) return "string";
  if (strcmp(type, "bytes") == 0) return "[]byte";
  if (strcmp(type, "uuid") == 0) return "[16]byte";
  return type;
}

static const char *tbe_compiler_ts_scalar_type(const char *type) {
  if (!type) return "unknown";
  if (strcmp(type, "bool") == 0) return "boolean";
  if (strstr(type, "int") || strcmp(type, "byte") == 0 ||
      strcmp(type, "float") == 0 || strcmp(type, "double") == 0) {
    return "number";
  }
  if (strcmp(type, "string") == 0) return "string";
  if (strcmp(type, "bytes") == 0) return "Uint8Array";
  if (strcmp(type, "uuid") == 0) return "string";
  return type;
}

static const char *tbe_compiler_python_scalar_type(const char *type) {
  if (!type) return "Any";
  if (strcmp(type, "bool") == 0) return "bool";
  if (strstr(type, "int") || strcmp(type, "byte") == 0) return "int";
  if (strcmp(type, "float") == 0 || strcmp(type, "double") == 0) return "float";
  if (strcmp(type, "string") == 0) return "str";
  if (strcmp(type, "bytes") == 0) return "bytes";
  if (strcmp(type, "uuid") == 0) return "str";
  return type;
}

static const char *tbe_compiler_rust_scalar_type(const char *type) {
  if (!type) return "()";
  if (strcmp(type, "bool") == 0) return "bool";
  if (strcmp(type, "byte") == 0 || strcmp(type, "uint8") == 0 || strcmp(type, "uint8_t") == 0) return "u8";
  if (strcmp(type, "int8") == 0 || strcmp(type, "int8_t") == 0) return "i8";
  if (strcmp(type, "uint16") == 0 || strcmp(type, "uint16_t") == 0) return "u16";
  if (strcmp(type, "int16") == 0 || strcmp(type, "int16_t") == 0) return "i16";
  if (strcmp(type, "uint32") == 0 || strcmp(type, "uint32_t") == 0) return "u32";
  if (strcmp(type, "int32") == 0 || strcmp(type, "int32_t") == 0) return "i32";
  if (strcmp(type, "uint64") == 0 || strcmp(type, "uint64_t") == 0) return "u64";
  if (strcmp(type, "int64") == 0 || strcmp(type, "int64_t") == 0) return "i64";
  if (strcmp(type, "float") == 0) return "f32";
  if (strcmp(type, "double") == 0) return "f64";
  if (strcmp(type, "string") == 0) return "String";
  if (strcmp(type, "bytes") == 0) return "Vec<u8>";
  if (strcmp(type, "uuid") == 0) return "[u8; 16]";
  return type;
}

static const char *tbe_compiler_typed_kind(const char *type) {
  if (!type) return NULL;
  if (strcmp(type, "bool") == 0) return "TBE_TYPED_BOOL";
  if (strcmp(type, "int8") == 0 || strcmp(type, "int8_t") == 0) return "TBE_TYPED_I8";
  if (strcmp(type, "uint8") == 0 || strcmp(type, "uint8_t") == 0 ||
      strcmp(type, "byte") == 0)
    return "TBE_TYPED_U8";
  if (strcmp(type, "int16") == 0 || strcmp(type, "int16_t") == 0) return "TBE_TYPED_I16";
  if (strcmp(type, "uint16") == 0 || strcmp(type, "uint16_t") == 0)
    return "TBE_TYPED_U16";
  if (strcmp(type, "int32") == 0 || strcmp(type, "int32_t") == 0) return "TBE_TYPED_I32";
  if (strcmp(type, "uint32") == 0 || strcmp(type, "uint32_t") == 0)
    return "TBE_TYPED_U32";
  if (strcmp(type, "int64") == 0 || strcmp(type, "int64_t") == 0) return "TBE_TYPED_I64";
  if (strcmp(type, "uint64") == 0 || strcmp(type, "uint64_t") == 0)
    return "TBE_TYPED_U64";
  if (strcmp(type, "float") == 0) return "TBE_TYPED_F32";
  if (strcmp(type, "double") == 0) return "TBE_TYPED_F64";
  if (strcmp(type, "uuid") == 0) return "TBE_TYPED_UUID";
  return NULL;
}

static const char *tbe_compiler_typed_c_scalar(const char *type) {
  if (!type) return NULL;
  if (strcmp(type, "bool") == 0) return "uint8_t";
  if (strcmp(type, "int8") == 0 || strcmp(type, "int8_t") == 0) return "int8_t";
  if (strcmp(type, "uint8") == 0 || strcmp(type, "uint8_t") == 0 ||
      strcmp(type, "byte") == 0)
    return "uint8_t";
  if (strcmp(type, "int16") == 0 || strcmp(type, "int16_t") == 0) return "int16_t";
  if (strcmp(type, "uint16") == 0 || strcmp(type, "uint16_t") == 0) return "uint16_t";
  if (strcmp(type, "int32") == 0 || strcmp(type, "int32_t") == 0) return "int32_t";
  if (strcmp(type, "uint32") == 0 || strcmp(type, "uint32_t") == 0) return "uint32_t";
  if (strcmp(type, "int64") == 0 || strcmp(type, "int64_t") == 0) return "int64_t";
  if (strcmp(type, "uint64") == 0 || strcmp(type, "uint64_t") == 0) return "uint64_t";
  if (strcmp(type, "float") == 0) return "float";
  if (strcmp(type, "double") == 0) return "double";
  if (strcmp(type, "uuid") == 0) return "turbo_uuid_t";
  return NULL;
}

static Node *tbe_compiler_find_record(Node *root, const char *list_name, const char *name) {
  Node *list = tbe_compiler_find_child(root, list_name);
  size_t i;
  if (!list || list->type != NODE_LIST || !name) return NULL;
  for (i = 0; i < list->data.list.count; ++i) {
    Node *record = list->data.list.items[i];
    const char *record_name = tbe_compiler_string_value(record, "name");
    if (record_name && strcmp(record_name, name) == 0) return record;
  }
  return NULL;
}

static const char *tbe_compiler_typed_named_kind(Node *root, const char *type,
                                                 char *c_type, size_t c_type_size,
                                                 char *descriptor, size_t descriptor_size) {
  const char *kind = tbe_compiler_typed_kind(type);
  const char *scalar = tbe_compiler_typed_c_scalar(type);
  Node *record;
  if (type && strcmp(type, "string") == 0) {
    snprintf(c_type, c_type_size, "tstr_t");
    descriptor[0] = '\0';
    return "TBE_TYPED_STRING";
  }
  if (type && strcmp(type, "bytes") == 0) {
    snprintf(c_type, c_type_size, "tbe_bytes_t");
    descriptor[0] = '\0';
    return "TBE_TYPED_BYTES";
  }
  if (kind && scalar) {
    snprintf(c_type, c_type_size, "%s", scalar);
    descriptor[0] = '\0';
    return kind;
  }
  record = tbe_compiler_find_record(root, "enums", type);
  if (record) {
    snprintf(c_type, c_type_size, "%s_t", type);
    descriptor[0] = '\0';
    return "TBE_TYPED_ENUM";
  }
  record = tbe_compiler_find_record(root, "composites", type);
  if (!record) record = tbe_compiler_find_record(root, "groups", type);
  if (!record) record = tbe_compiler_find_record(root, "messages", type);
  if (record) {
    snprintf(c_type, c_type_size, "%s_t", type);
    snprintf(descriptor, descriptor_size, "&%s_TYPED_TYPE", type);
    return "TBE_TYPED_OBJECT";
  }
  c_type[0] = '\0';
  descriptor[0] = '\0';
  return NULL;
}

static const char *tbe_compiler_typed_wire_kind(Node *root, const char *type,
                                                const char *fallback) {
  Node *record = tbe_compiler_find_record(root, "enums", type);
  if (record) {
    const char *underlying = tbe_compiler_string_value(record, "underlying_type");
    const char *kind = tbe_compiler_typed_kind(underlying ? underlying : "int32");
    return kind ? kind : "TBE_TYPED_I32";
  }
  return fallback;
}

static const char *tbe_compiler_cpp_enum_underlying_type(const char *type) {
  return tbe_compiler_cpp_scalar_type((type && type[0]) ? type : "int32");
}

static const char *tbe_compiler_rust_enum_underlying_type(const char *type) {
  return tbe_compiler_rust_scalar_type((type && type[0]) ? type : "int32");
}

typedef const char *(*tbe_scalar_mapper_t)(const char *);

static void tbe_compiler_field_type(Node *field,
                                    tbe_scalar_mapper_t scalar_mapper,
                                    const char *list_prefix,
                                    const char *list_suffix,
                                    const char *set_prefix,
                                    const char *set_suffix,
                                    const char *map_prefix,
                                    const char *map_separator,
                                    const char *map_suffix,
                                    char *out,
                                    size_t out_size) {
  const char *type = tbe_compiler_string_value(field, "type");

  if (!out || out_size == 0) return;

  if (tbe_compiler_has_child(field, "is_group_field")) {
    const char *group_type = tbe_compiler_string_value(field, "group_type");
    snprintf(out, out_size, "%s%s%s", list_prefix, group_type ? group_type : "unknown", list_suffix);
    return;
  }

  if (tbe_compiler_has_child(field, "is_collection")) {
    const char *inner_type = tbe_compiler_string_value(field, "inner_type");
    const char *key_type = tbe_compiler_string_value(field, "key_type");
    const char *value_type = tbe_compiler_string_value(field, "value_type");

    if (tbe_compiler_has_child(field, "is_map")) {
      snprintf(out, out_size, "%s%s%s%s%s", map_prefix,
               scalar_mapper(key_type ? key_type : "string"),
               map_separator,
               scalar_mapper(value_type ? value_type : inner_type),
               map_suffix);
    } else if (tbe_compiler_has_child(field, "is_set")) {
      snprintf(out, out_size, "%s%s%s", set_prefix,
               scalar_mapper(inner_type ? inner_type : "unknown"),
               set_suffix);
    } else {
      snprintf(out, out_size, "%s%s%s", list_prefix,
               scalar_mapper(inner_type ? inner_type : "unknown"),
               list_suffix);
    }
    return;
  }

  if (tbe_compiler_has_child(field, "is_bytes") && tbe_compiler_has_child(field, "is_fixed_size")) {
    snprintf(out, out_size, "%s", scalar_mapper("bytes"));
    return;
  }

  snprintf(out, out_size, "%s", scalar_mapper(type));
}

static void tbe_compiler_annotate_typed_field(Node *root, Node *field) {
  const char *name = tbe_compiler_string_value(field, "name");
  const char *owner = tbe_compiler_string_value(field, "owner_name");
  const char *type = tbe_compiler_string_value(field, "type");
  char c_type[256] = {0};
  char descriptor[256] = {0};
  char declaration[512] = {0};
  char vector_type[256] = {0};
  const char *kind = NULL;

  if (!name || !owner) return;
  if (tbe_compiler_has_child(field, "is_group_field")) {
    const char *group_type = tbe_compiler_string_value(field, "group_type");
    snprintf(c_type, sizeof(c_type), "%s_t", group_type ? group_type : "unknown");
    snprintf(descriptor, sizeof(descriptor), "&%s_TYPED_TYPE", group_type ? group_type : "unknown");
    snprintf(vector_type, sizeof(vector_type), "%s_%s_vec_t", owner, name);
    snprintf(declaration, sizeof(declaration), "%s %s;", vector_type, name);
    tbe_compiler_set_string(field, "typed_kind", "TBE_TYPED_LIST");
    tbe_compiler_set_string(field, "typed_element_kind", "TBE_TYPED_OBJECT");
    tbe_compiler_set_string(field, "typed_element_wire_kind", "TBE_TYPED_OBJECT");
    tbe_compiler_set_string(field, "typed_element_c_type", c_type);
    tbe_compiler_set_string(field, "typed_object_descriptor", descriptor);
    tbe_compiler_set_string(field, "typed_vector_type", vector_type);
    tbe_compiler_set_string(field, "typed_needs_vector", "1");
    tbe_compiler_set_string(field, "typed_is_group", "1");
    tbe_compiler_set_string(field, "typed_declaration", declaration);
    return;
  }
  if (tbe_compiler_has_child(field, "is_bytes")) {
    if (tbe_compiler_has_child(field, "is_fixed_size")) {
      const char *count = tbe_compiler_string_value(field, "size_bytes");
      snprintf(declaration, sizeof(declaration), "uint8_t %s[%s];", name, count ? count : "0");
      tbe_compiler_set_string(field, "typed_kind", "TBE_TYPED_FIXED_BYTES");
      tbe_compiler_set_string(field, "typed_fixed_count", count ? count : "0");
    } else {
      snprintf(declaration, sizeof(declaration), "tbe_bytes_t %s;", name);
      tbe_compiler_set_string(field, "typed_kind", "TBE_TYPED_BYTES");
      tbe_compiler_set_string(field, "typed_is_var_data", "1");
    }
    tbe_compiler_set_string(field, "typed_declaration", declaration);
    return;
  }
  if (tbe_compiler_has_child(field, "is_collection")) {
    const char *inner = tbe_compiler_string_value(field, "inner_type");
    kind = tbe_compiler_typed_named_kind(root, inner, c_type, sizeof(c_type), descriptor,
                                         sizeof(descriptor));
    if (!kind) {
      snprintf(declaration, sizeof(declaration), "%s_t %s;", inner ? inner : "unknown", name);
      tbe_compiler_set_string(field, "typed_declaration", declaration);
      return;
    }
    tbe_compiler_set_string(field, "typed_element_kind", kind);
    tbe_compiler_set_string(field, "typed_element_wire_kind",
                            tbe_compiler_typed_wire_kind(root, inner, kind));
    tbe_compiler_set_string(field, "typed_element_c_type", c_type);
    if (descriptor[0]) tbe_compiler_set_string(field, "typed_object_descriptor", descriptor);
    if (tbe_compiler_has_child(field, "is_fixed_size")) {
      const char *count = tbe_compiler_string_value(field, "length_field");
      snprintf(declaration, sizeof(declaration), "%s %s[%s];", c_type, name, count ? count : "0");
      tbe_compiler_set_string(field, "typed_kind", "TBE_TYPED_FIXED_ARRAY");
      tbe_compiler_set_string(field, "typed_fixed_count", count ? count : "0");
    } else if (tbe_compiler_has_child(field, "is_map")) {
      const char *key_type = tbe_compiler_string_value(field, "key_type");
      const char *value_type = tbe_compiler_string_value(field, "value_type");
      char value_c_type[256] = {0};
      char value_descriptor[256] = {0};
      const char *value_kind = tbe_compiler_typed_named_kind(
          root, value_type ? value_type : inner, value_c_type, sizeof(value_c_type),
          value_descriptor, sizeof(value_descriptor));
      char entry_type[256];
      if (!value_kind || !key_type || strcmp(key_type, "string") != 0) {
        snprintf(declaration, sizeof(declaration), "/* unsupported map field %s */ uint8_t %s;",
                 name, name);
        tbe_compiler_set_string(field, "typed_declaration", declaration);
        return;
      }
      snprintf(entry_type, sizeof(entry_type), "%s_%s_entry_t", owner, name);
      snprintf(vector_type, sizeof(vector_type), "%s_%s_vec_t", owner, name);
      snprintf(declaration, sizeof(declaration), "%s %s;", vector_type, name);
      tbe_compiler_set_string(field, "typed_kind", "TBE_TYPED_MAP");
      tbe_compiler_set_string(field, "typed_map_entry_type", entry_type);
      tbe_compiler_set_string(field, "typed_map_value_kind", value_kind);
      tbe_compiler_set_string(field, "typed_map_value_wire_kind",
                              tbe_compiler_typed_wire_kind(
                                  root, value_type ? value_type : inner, value_kind));
      tbe_compiler_set_string(field, "typed_map_value_c_type", value_c_type);
      if (value_descriptor[0])
        tbe_compiler_set_string(field, "typed_map_value_descriptor", value_descriptor);
      tbe_compiler_set_string(field, "typed_vector_type", vector_type);
      tbe_compiler_set_string(field, "typed_element_c_type", entry_type);
      tbe_compiler_set_string(field, "typed_needs_map_vector", "1");
    } else {
      snprintf(vector_type, sizeof(vector_type), "%s_%s_vec_t", owner, name);
      snprintf(declaration, sizeof(declaration), "%s %s;", vector_type, name);
      tbe_compiler_set_string(field, "typed_kind",
                              tbe_compiler_has_child(field, "is_set") ? "TBE_TYPED_SET"
                                                                      : "TBE_TYPED_LIST");
      tbe_compiler_set_string(field, "typed_vector_type", vector_type);
      tbe_compiler_set_string(field, "typed_needs_vector", "1");
    }
    tbe_compiler_set_string(field, "typed_declaration", declaration);
    return;
  }
  if (tbe_compiler_has_child(field, "is_var_data") && type && strcmp(type, "string") == 0) {
    snprintf(declaration, sizeof(declaration), "tstr_t %s;", name);
    tbe_compiler_set_string(field, "typed_kind", "TBE_TYPED_STRING");
    tbe_compiler_set_string(field, "typed_is_var_data", "1");
    tbe_compiler_set_string(field, "typed_declaration", declaration);
    return;
  }
  kind = tbe_compiler_typed_named_kind(root, type, c_type, sizeof(c_type), descriptor,
                                       sizeof(descriptor));
  if (!kind) {
    snprintf(declaration, sizeof(declaration), "%s_t %s;", type ? type : "unknown", name);
    tbe_compiler_set_string(field, "typed_declaration", declaration);
    return;
  }
  snprintf(declaration, sizeof(declaration), "%s %s;", c_type, name);
  tbe_compiler_set_string(field, "typed_kind", kind);
  tbe_compiler_set_string(field, "typed_wire_kind",
                          tbe_compiler_typed_wire_kind(root, type, kind));
  if (descriptor[0]) tbe_compiler_set_string(field, "typed_object_descriptor", descriptor);
  tbe_compiler_set_string(field, "typed_declaration", declaration);
}

static void tbe_compiler_annotate_field_types(Node *root, Node *field) {
  char type_buf[256];
  char field_name[128];
  const char *name = tbe_compiler_string_value(field, "name");

  tbe_compiler_pascal_identifier(name, field_name, sizeof(field_name));
  tbe_compiler_set_string(field, "go_name", field_name);

  tbe_compiler_field_type(field, tbe_compiler_cpp_scalar_type,
                          "std::vector<", ">", "std::set<", ">",
                          "std::map<", ", ", ">", type_buf, sizeof(type_buf));
  tbe_compiler_set_string(field, "cpp_type", type_buf);

  tbe_compiler_field_type(field, tbe_compiler_go_scalar_type,
                          "[]", "", "map[", "]struct{}",
                          "map[", "]", "", type_buf, sizeof(type_buf));
  tbe_compiler_set_string(field, "go_type", type_buf);

  tbe_compiler_field_type(field, tbe_compiler_ts_scalar_type,
                          "Array<", ">", "Set<", ">",
                          "Map<", ", ", ">", type_buf, sizeof(type_buf));
  tbe_compiler_set_string(field, "ts_type", type_buf);

  tbe_compiler_field_type(field, tbe_compiler_python_scalar_type,
                          "list[", "]", "set[", "]",
                          "dict[", ", ", "]", type_buf, sizeof(type_buf));
  tbe_compiler_set_string(field, "python_type", type_buf);

  tbe_compiler_field_type(field, tbe_compiler_rust_scalar_type,
                          "Vec<", ">", "std::collections::HashSet<", ">",
                          "std::collections::HashMap<", ", ", ">", type_buf, sizeof(type_buf));
  tbe_compiler_set_string(field, "rust_type", type_buf);
  tbe_compiler_annotate_typed_field(root, field);
}

static void tbe_compiler_annotate_record_list_types(Node *root, const char *list_name) {
  Node *list = tbe_compiler_find_child(root, list_name);
  if (!list || list->type != NODE_LIST) return;

  for (size_t i = 0; i < list->data.list.count; ++i) {
    Node *record = list->data.list.items[i];
    Node *fields = tbe_compiler_find_child(record, "fields");
    if (!fields || fields->type != NODE_LIST) continue;

    for (size_t j = 0; j < fields->data.list.count; ++j) {
      tbe_compiler_annotate_field_types(root, fields->data.list.items[j]);
    }
  }
}

static void tbe_compiler_annotate_enum_types(Node *root) {
  Node *enums = tbe_compiler_find_child(root, "enums");
  if (!enums || enums->type != NODE_LIST) return;

  for (size_t i = 0; i < enums->data.list.count; ++i) {
    Node *enum_node = enums->data.list.items[i];
    const char *underlying = tbe_compiler_string_value(enum_node, "underlying_type");

    tbe_compiler_set_string(enum_node, "cpp_underlying_type",
                            tbe_compiler_cpp_enum_underlying_type(underlying));
    tbe_compiler_set_string(enum_node, "rust_underlying_type",
                            tbe_compiler_rust_enum_underlying_type(underlying));
  }
}

static void tbe_compiler_annotate_schema_types(Node *root) {
  Node *schema = tbe_compiler_find_child(root, "schema");
  const char *schema_name;
  char package_name[128];
  size_t out = 0;

  if (!schema) {
    schema = create_node_map("schema");
    if (!schema || map_add(root, schema) != 0) {
      node_free(schema);
      return;
    }
    tbe_compiler_set_string(schema, "schema_name", "GeneratedSchema");
    tbe_compiler_set_string(schema, "schema_attributes_rendered", "");
    tbe_compiler_set_string(schema, "schema_wire_big_endian_value", "0");
  }

  schema_name = tbe_compiler_string_value(schema, "schema_name");

  if (!schema_name || !schema_name[0]) {
    tbe_compiler_set_string(schema, "go_package_name", "generated");
    return;
  }

  for (size_t i = 0; schema_name[i] && out + 1 < sizeof(package_name); ++i) {
    char c = schema_name[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
      package_name[out++] = c;
    } else if (out > 0 && package_name[out - 1] != '_') {
      package_name[out++] = '_';
    }
  }

  if (out == 0 || (package_name[0] >= '0' && package_name[0] <= '9')) {
    snprintf(package_name, sizeof(package_name), "generated");
  } else {
    package_name[out] = '\0';
  }

  tbe_compiler_set_string(schema, "go_package_name", package_name);
}

void tbe_compiler_annotate_language_types(Node *root) {
  tbe_compiler_annotate_schema_types(root);
  tbe_compiler_annotate_enum_types(root);
  tbe_compiler_annotate_record_list_types(root, "composites");
  tbe_compiler_annotate_record_list_types(root, "groups");
  tbe_compiler_annotate_record_list_types(root, "messages");
  tbe_compiler_annotate_record_list_types(root, "unions");
}

static const char *tbe_compiler_path_basename(const char *path) {
  const char *base = path;
  const char *p;
  if (!path) return "generated.h";
  for (p = path; *p; ++p)
    if (*p == '/' || *p == '\\') base = p + 1;
  return base;
}

static char *tbe_compiler_escape_c_string(const char *text) {
  size_t len = text ? strlen(text) : 0;
  size_t capacity = len > (SIZE_MAX - 1) / 2 ? 0 : len * 2 + 1;
  char *out;
  size_t i;
  size_t used = 0;
  if (capacity == 0) return NULL;
  out = (char *)malloc(capacity);
  if (!out) return NULL;
  for (i = 0; i < len; ++i) {
    const char *escape = NULL;
    char ch = text[i];
    if (ch == '\\') escape = "\\\\";
    else if (ch == '"') escape = "\\\"";
    else if (ch == '\n') escape = "\\n";
    else if (ch == '\r') escape = "\\r";
    else if (ch == '\t') escape = "\\t";
    if (escape) {
      size_t n = strlen(escape);
      if (used + n + 1 > capacity) {
        size_t next = capacity * 2;
        char *grown;
        if (next < used + n + 1) next = used + n + 1;
        grown = (char *)realloc(out, next);
        if (!grown) {
          free(out);
          return NULL;
        }
        out = grown;
        capacity = next;
      }
      memcpy(out + used, escape, n);
      used += n;
    } else {
      if (used + 2 > capacity) {
        size_t next = capacity * 2;
        char *grown = (char *)realloc(out, next);
        if (!grown) {
          free(out);
          return NULL;
        }
        out = grown;
        capacity = next;
      }
      out[used++] = ch;
    }
  }
  out[used] = '\0';
  return out;
}

static int tbe_compiler_typed_list_supported(Node *root, const char *list_name) {
  Node *list = tbe_compiler_find_child(root, list_name);
  size_t i;
  if (!list || list->type != NODE_LIST) return 1;
  for (i = 0; i < list->data.list.count; ++i) {
    Node *fields = tbe_compiler_find_child(list->data.list.items[i], "fields");
    size_t j;
    if (!fields || fields->type != NODE_LIST) continue;
    for (j = 0; j < fields->data.list.count; ++j) {
      Node *field = fields->data.list.items[j];
      if (!tbe_compiler_string_value(field, "typed_kind")) {
        fprintf(stderr, "Typed C serde does not support field %s.%s of type %s\n",
                tbe_compiler_string_value(field, "owner_name"),
                tbe_compiler_string_value(field, "name"),
                tbe_compiler_string_value(field, "type"));
        return 0;
      }
    }
  }
  return 1;
}

static int tbe_compiler_typed_schema_supported(Node *root) {
  Node *unions = tbe_compiler_find_child(root, "unions");
  if (unions && unions->type == NODE_LIST && unions->data.list.count != 0) {
    fprintf(stderr, "Typed C serde does not yet support union declarations\n");
    return 0;
  }
  return tbe_compiler_typed_list_supported(root, "composites") &&
         tbe_compiler_typed_list_supported(root, "groups") &&
         tbe_compiler_typed_list_supported(root, "messages");
}

char *tbe_compiler_read_file(const char *filename) {
  FILE *f = fopen(filename, "rb");
  if (!f) return NULL;

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }

  long file_size = ftell(f);
  if (file_size < 0) {
    fclose(f);
    return NULL;
  }

  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }

  size_t size = (size_t)file_size;
  char *dat = (char *)malloc(size + 1);
  if (!dat) {
    fclose(f);
    return NULL;
  }

  size_t bytes_read = fread(dat, 1, size, f);
  fclose(f);
  if (bytes_read != size) {
    free(dat);
    return NULL;
  }

  dat[size] = '\0';
  return dat;
}

const char *tbe_compiler_resolve_template(const char *user_template,
                                          int64_t lang_enum) {
  if (user_template) return user_template;

  switch (lang_enum) {
    default:
    case TBE_COMPILER_LANG_C:
      return "templates/c_structs.mustache";
    case TBE_COMPILER_LANG_PYTHON:
      return "templates/python_dataclass.mustache";
    case TBE_COMPILER_LANG_RUST:
      return "templates/rust_structs.mustache";
    case TBE_COMPILER_LANG_CPP:
      return "templates/cpp_types.mustache";
    case TBE_COMPILER_LANG_GO:
      return "templates/go_types.mustache";
    case TBE_COMPILER_LANG_TS:
      return "templates/ts_types.mustache";
  }
}

static int tbe_compiler_write_file_cb(const void *data, size_t len, void *user) {
  FILE *out = (FILE *)user;
  return out && fwrite(data, 1, len, out) == len ? 0 : -1;
}

static int tbe_compiler_render_mir_file(const char *schema_path, const char *output_path,
                                        int binary_output) {
  FILE *out_file = stdout;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;

  if (output_path) {
    out_file = fopen(output_path, "wb");
    if (!out_file) {
      fprintf(stderr, "Failed to open output file: %s\n", output_path);
      return 1;
    }
  }

  status = data_bind_generate_mir(schema_path, tbe_compiler_write_file_cb, out_file,
                                  binary_output, &error);
  if (out_file != stdout) fclose(out_file);
  if (status != DATA_BIND_OK) {
    fprintf(stderr, "Failed to generate MIR output: %s\n",
            error.message[0] != '\0' ? error.message : data_bind_status_name(status));
    return 1;
  }
  return 0;
}

int tbe_compiler_parse_schema_file(const char *schema_path, Node **out_root,
                                   char **out_schema_data) {
  tbe_error_t parse_err;
  Node *root = NULL;
  char *schema_data = tbe_compiler_read_file(schema_path);
  if (!schema_data) {
    fprintf(stderr, "Failed to read schema file: %s\n", schema_path);
    return 1;
  }

  root = create_node_map(NULL);
  if (!root) {
    fprintf(stderr, "Failed to allocate root node\n");
    free(schema_data);
    return 1;
  }

  if (parse_schema(schema_data, strlen(schema_data), root, &parse_err) != 0) {
    if (parse_err.line >= 0) {
      fprintf(stderr, "Parse error at line %d: %s\n", parse_err.line,
              parse_err.message);
    } else {
      fprintf(stderr, "Parse error: %s\n", parse_err.message);
    }
    free(schema_data);
    node_free(root);
    return 1;
  }

  tbe_compiler_annotate_language_types(root);

  *out_root = root;
  *out_schema_data = schema_data;
  return 0;
}

int tbe_compiler_render_file(Node *root, const char *template_path,
                             const char *output_path) {
  MUSTACHE_DATAPROVIDER provider = mustache_helpers_provider();
  MUSTACHE_RENDERER renderer = mustache_helpers_renderer();
  char *templ_data = tbe_compiler_read_file(template_path);
  MUSTACHE_TEMPLATE *templ = NULL;
  FILE *out_file = stdout;
  int res = 1;

  if (!templ_data) {
    fprintf(stderr, "Failed to read template file: %s\n", template_path);
    return 1;
  }

  templ = mustache_compile(templ_data, strlen(templ_data), NULL, NULL, 0);
  if (!templ) {
    fprintf(stderr, "Failed to compile mustache template\n");
    goto cleanup;
  }

  if (output_path) {
    out_file = fopen(output_path, "wb");
    if (!out_file) {
      fprintf(stderr, "Failed to open output file: %s\n", output_path);
      goto cleanup;
    }
  }

  if (mustache_process(templ, &renderer, out_file, &provider, root)
      != MUSTACHE_ERR_SUCCESS) {
    fprintf(stderr, "Failed to render mustache template: %s\n", template_path);
    goto cleanup;
  }

  res = 0;

cleanup:
  if (out_file != stdout) fclose(out_file);
  mustache_release(templ);
  free(templ_data);
  return res;
}

int tbe_compiler_run(const tbe_compiler_options_t *options) {
  Node *root = NULL;
  char *schema_data = NULL;
  int status = tbe_compiler_parse_schema_file(options->schema_path, &root,
                                              &schema_data);
  if (status != 0) return status;

  if (tbe_compiler_set_string(root, "generated_header",
                              tbe_compiler_path_basename(options->output_path)) != 0) {
    status = 1;
    goto cleanup;
  }
  {
    char *schema_literal = tbe_compiler_escape_c_string(schema_data);
    if (!schema_literal || tbe_compiler_set_string(root, "schema_c_literal", schema_literal) != 0) {
      free(schema_literal);
      status = 1;
      goto cleanup;
    }
    free(schema_literal);
  }

  if (options->source_output_path) {
    if (options->output_path == NULL || options->output_path[0] == '\0') {
      fprintf(stderr, "--source-output requires --output for the generated header\n");
      status = 1;
      goto cleanup;
    }
    if (strcmp(options->output_path, options->source_output_path) == 0) {
      fprintf(stderr, "--output and --source-output must name different files\n");
      status = 1;
      goto cleanup;
    }
    if (options->lang_enum != TBE_COMPILER_LANG_C || options->template_path != NULL) {
      fprintf(stderr, "--source-output is supported only for the built-in C generator\n");
      status = 1;
      goto cleanup;
    }
    if (!tbe_compiler_typed_schema_supported(root)) {
      status = 1;
      goto cleanup;
    }
    if (tbe_compiler_set_string(root, "typed_source_enabled", "1") != 0) {
      status = 1;
      goto cleanup;
    }
  }

  if (options->guest_output_path) {
    if (options->output_path == NULL || options->output_path[0] == '\0') {
      fprintf(stderr, "--guest-output requires --output for the generated header\n");
      status = 1;
      goto cleanup;
    }
    if (strcmp(options->output_path, options->guest_output_path) == 0) {
      fprintf(stderr, "--output and --guest-output must name different files\n");
      status = 1;
      goto cleanup;
    }
    if (options->source_output_path != NULL &&
        strcmp(options->source_output_path, options->guest_output_path) == 0) {
      fprintf(stderr, "--source-output and --guest-output must name different files\n");
      status = 1;
      goto cleanup;
    }
    if (options->lang_enum != TBE_COMPILER_LANG_C || options->template_path != NULL) {
      fprintf(stderr, "--guest-output is supported only for the built-in C generator\n");
      status = 1;
      goto cleanup;
    }
    if (tbe_compiler_set_string(root, "guest_adapter_enabled", "1") != 0) {
      status = 1;
      goto cleanup;
    }
    if (options->source_output_path == NULL &&
        tbe_compiler_set_string(root, "guest_adapter_only", "1") != 0) {
      status = 1;
      goto cleanup;
    }
  }

  if (options->template_path == NULL &&
      (options->lang_enum == TBE_COMPILER_LANG_MIR ||
       options->lang_enum == TBE_COMPILER_LANG_BMIR)) {
    status = tbe_compiler_render_mir_file(
        options->schema_path, options->output_path,
        options->lang_enum == TBE_COMPILER_LANG_BMIR);
  } else {
    status = tbe_compiler_render_file(
        root,
        tbe_compiler_resolve_template(options->template_path, options->lang_enum),
        options->output_path);
  }

  if (status == 0 && options->source_output_path) {
    status = tbe_compiler_render_file(root, "templates/c_typed_source.mustache",
                                      options->source_output_path);
  }

  if (status == 0 && options->guest_output_path) {
    status = tbe_compiler_render_file(root, "templates/c_guest_adapter.mustache",
                                      options->guest_output_path);
  }

  if (status == 0 && options->dsl_output_path) {
    if (tbe_compiler_render_file(root, "templates/rfl_types.mustache",
                                 options->dsl_output_path)
        != 0) {
      status = 1;
    }
  }

cleanup:
  free(schema_data);
  node_free(root);
  return status;
}
