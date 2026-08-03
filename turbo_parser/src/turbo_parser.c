#include "turbo_parser.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Parsers headers
#include "cmd_arger.h"
#include "csv_parser.h"
#include "csv_stream_processor.h"
#include "cxml/cxml.h"
#include "cyaml.h"
#include "cyaml_json_adapter.h"
#include "datetime_parser.h"
#include "dotenv.h"
#include "dotenv_environment_internal.h"
#include "dsv_filter.h"
#include "frame_parser.h" // for TLV
#include "ini_parser.h"
#include "json_parser.h"
#include "ltv_parser.h"
#include "modbus_parser.h"
#include "soa_parser.h"
#include "toml.h"
#include "toon_json_adapter.h"
#include "toonc.h"
#include "uri_parser.h"
#include <fmt.h>
#include <turbo_str.h>

// XML (cxml) - internal only
#include "core/cxdefs.h"
#include "xml/cxparser.h"
#ifdef CXML_USE_XPATH_MOD
  #include "xpath/cxxpeval.h"
#endif

static void turbo_cxml_set_destroy(cxml_set *nodeset) {
  if (!nodeset) return;
  cxml_set_free(nodeset);
  free(nodeset);
}

static const char *turbo_cxml_string_raw(const cxml_string *str) {
  return str ? cxml_string_as_raw((cxml_string *)str) : NULL;
}

/* JSON */
int turbo_parse_json(const uint8_t *data, size_t len, turbo_json_doc_t **out) {
  if (!data || !out) return -1;
  *out = NULL;
  json_value_t *val = json_parse((const char *)data, len);
  if (!val) return -1;
  *out = val;
  return 0;
}

static json_sax_handler_t turbo_json_sax_handler_to_raw(const turbo_json_sax_handler_t *handler) {
  json_sax_handler_t raw = {0};
  if (!handler) return raw;
  raw.on_null = handler->on_null;
  raw.on_bool = handler->on_bool;
  raw.on_number = handler->on_number;
  raw.on_string = handler->on_string;
  raw.on_object_start = handler->on_object_start;
  raw.on_object_key = handler->on_object_key;
  raw.on_object_end = handler->on_object_end;
  raw.on_array_start = handler->on_array_start;
  raw.on_array_end = handler->on_array_end;
  return raw;
}

static json_sax_handler_raw_t
turbo_json_sax_raw_handler_to_raw(const turbo_json_sax_handler_raw_t *handler) {
  json_sax_handler_raw_t raw = {0};
  if (!handler) return raw;
  raw.on_null = handler->on_null;
  raw.on_bool = handler->on_bool;
  raw.on_number = handler->on_number;
  raw.on_string = handler->on_string;
  raw.on_object_start = handler->on_object_start;
  raw.on_object_key = handler->on_object_key;
  raw.on_object_end = handler->on_object_end;
  raw.on_array_start = handler->on_array_start;
  raw.on_array_end = handler->on_array_end;
  return raw;
}

int turbo_parse_json_sax(const uint8_t *data, size_t len, const turbo_json_sax_handler_t *handler,
                         void *ctx) {
  if (!handler) return json_parse_sax((const char *)data, len, NULL, ctx);
  json_sax_handler_t raw = turbo_json_sax_handler_to_raw(handler);
  return json_parse_sax((const char *)data, len, &raw, ctx);
}

int turbo_parse_json_sax_raw(const uint8_t *data, size_t len,
                             const turbo_json_sax_handler_raw_t *handler, void *ctx) {
  if (!handler) return json_parse_sax_raw((const char *)data, len, NULL, ctx);
  json_sax_handler_raw_t raw = turbo_json_sax_raw_handler_to_raw(handler);
  return json_parse_sax_raw((const char *)data, len, &raw, ctx);
}

turbo_json_sax_parser_t *turbo_json_sax_parser_create(const turbo_json_sax_handler_t *handler,
                                                      void *ctx) {
  if (!handler) return (turbo_json_sax_parser_t *)json_sax_parser_create(NULL, ctx);
  json_sax_handler_t raw = turbo_json_sax_handler_to_raw(handler);
  return (turbo_json_sax_parser_t *)json_sax_parser_create(&raw, ctx);
}

turbo_json_sax_parser_t *
turbo_json_sax_parser_create_raw(const turbo_json_sax_handler_raw_t *handler, void *ctx) {
  if (!handler) return (turbo_json_sax_parser_t *)json_sax_parser_create_raw(NULL, ctx);
  json_sax_handler_raw_t raw = turbo_json_sax_raw_handler_to_raw(handler);
  return (turbo_json_sax_parser_t *)json_sax_parser_create_raw(&raw, ctx);
}

int turbo_json_sax_parser_feed(turbo_json_sax_parser_t *parser, const char *data, size_t len) {
  return json_sax_parser_feed((json_sax_parser_t *)parser, data, len);
}

int turbo_json_sax_parser_finish(turbo_json_sax_parser_t *parser) {
  return json_sax_parser_finish((json_sax_parser_t *)parser);
}

const char *turbo_json_sax_parser_error(const turbo_json_sax_parser_t *parser) {
  return json_sax_parser_error((const json_sax_parser_t *)parser);
}

void turbo_json_sax_parser_destroy(turbo_json_sax_parser_t *parser) {
  json_sax_parser_destroy((json_sax_parser_t *)parser);
}

void turbo_free_json(turbo_json_doc_t **out) {
  if (!out || !*out) return;
  json_free(*out);
  *out = NULL;
}

json_value_t *turbo_json_path_get(const json_value_t *root, const char *expr) {
  return json_path_get(root, expr);
}

turbo_json_path_result_t *turbo_json_path_query(const json_value_t *root, const char *expr) {
  return (turbo_json_path_result_t *)json_path_query(root, expr);
}

size_t turbo_json_path_result_size(const turbo_json_path_result_t *result) {
  return json_path_result_size((const json_path_result_t *)result);
}

json_value_t *turbo_json_path_result_get(const turbo_json_path_result_t *result, size_t index) {
  return json_path_result_get((const json_path_result_t *)result, index);
}

void turbo_json_path_result_free(turbo_json_path_result_t *result) {
  json_path_result_free((json_path_result_t *)result);
}

const char *turbo_json_path_error(void) { return json_path_get_error(); }

/* YAML */
struct turbo_yaml_doc_s {
  cyaml_doc_t *doc;
  char *source;
};

struct turbo_yaml_path_result_s {
  cyaml_path_result_t result;
};

struct turbo_yaml_sax_parser_s {
  turbo_yaml_sax_handler_t handler;
  void *ctx;
  cyaml_sax_parser_t *parser;
  char error[256];
};

static void turbo_yaml_copy_location(turbo_yaml_location_t *destination,
                                     const cyaml_span_t *source) {
  if (!destination) return;
  memset(destination, 0, sizeof(*destination));
  if (!source) return;
  destination->offset = source->off;
  destination->length = source->len;
  destination->start_line = source->start_line;
  destination->start_column = source->start_col;
  destination->end_line = source->end_line;
  destination->end_column = source->end_col;
}

static turbo_yaml_error_code_t turbo_yaml_error_code(cyaml_err_t code) {
  switch (code) {
  case CYAML_OK:
    return TURBO_YAML_ERROR_NONE;
  case CYAML_ERR_NOMEM:
    return TURBO_YAML_ERROR_OUT_OF_MEMORY;
  case CYAML_ERR_SYNTAX:
    return TURBO_YAML_ERROR_SYNTAX;
  case CYAML_ERR_EOF:
    return TURBO_YAML_ERROR_UNEXPECTED_END;
  case CYAML_ERR_INDENT:
    return TURBO_YAML_ERROR_INDENTATION;
  case CYAML_ERR_ESCAPE:
    return TURBO_YAML_ERROR_ESCAPE;
  case CYAML_ERR_ANCHOR:
    return TURBO_YAML_ERROR_ANCHOR;
  case CYAML_ERR_ALIAS:
    return TURBO_YAML_ERROR_ALIAS;
  case CYAML_ERR_TAG:
    return TURBO_YAML_ERROR_TAG;
  case CYAML_ERR_DUP_KEY:
    return TURBO_YAML_ERROR_DUPLICATE_KEY;
  case CYAML_ERR_IO:
    return TURBO_YAML_ERROR_IO;
  default:
    return TURBO_YAML_ERROR_SYNTAX;
  }
}

static void turbo_yaml_set_error(turbo_yaml_error_t *error, turbo_yaml_error_code_t code,
                                 const cyaml_span_t *location, const char *message) {
  if (!error) return;
  memset(error, 0, sizeof(*error));
  error->code = code;
  turbo_yaml_copy_location(&error->location, location);
  if (message) snprintf(error->message, sizeof(error->message), "%s", message);
}

int turbo_parse_yaml_ex(const uint8_t *data, size_t len, turbo_yaml_doc_t **out,
                        turbo_yaml_error_t *error) {
  turbo_yaml_doc_t *yaml;
  cyaml_error_t parse_error = {0};
  turbo_yaml_set_error(error, TURBO_YAML_ERROR_NONE, NULL, NULL);
  if (out) *out = NULL;
  if (!data || !out) {
    turbo_yaml_set_error(error, TURBO_YAML_ERROR_INVALID_ARGUMENT, NULL,
                         "YAML input and output are required");
    return -1;
  }
  if (len > UINT32_MAX) {
    turbo_yaml_set_error(error, TURBO_YAML_ERROR_INPUT_TOO_LARGE, NULL,
                         "YAML input exceeds the supported size");
    return -1;
  }
  *out = NULL;
  yaml = (turbo_yaml_doc_t *)calloc(1, sizeof(*yaml));
  if (!yaml) {
    turbo_yaml_set_error(error, TURBO_YAML_ERROR_OUT_OF_MEMORY, NULL, "Out of memory");
    return -1;
  }
  yaml->source = (char *)malloc(len + 1);
  if (!yaml->source) {
    free(yaml);
    turbo_yaml_set_error(error, TURBO_YAML_ERROR_OUT_OF_MEMORY, NULL, "Out of memory");
    return -1;
  }
  memcpy(yaml->source, data, len);
  yaml->source[len] = '\0';
  yaml->doc = cyaml_parse(yaml->source, len, NULL, &parse_error);
  if (!yaml->doc) {
    const char *message = parse_error.msg[0] ? parse_error.msg : cyaml_strerror(parse_error.code);
    turbo_yaml_set_error(error, turbo_yaml_error_code(parse_error.code), &parse_error.span,
                         message);
    free(yaml->source);
    free(yaml);
    return -1;
  }
  *out = yaml;
  return 0;
}

int turbo_parse_yaml(const uint8_t *data, size_t len, turbo_yaml_doc_t **out) {
  return turbo_parse_yaml_ex(data, len, out, NULL);
}

static turbo_yaml_scalar_kind_t turbo_yaml_sax_kind(cyaml_scalar_kind_t kind) {
  switch (kind) {
  case CYAML_KIND_NULL:
    return TURBO_YAML_SCALAR_NULL;
  case CYAML_KIND_BOOL:
    return TURBO_YAML_SCALAR_BOOL;
  case CYAML_KIND_INT:
    return TURBO_YAML_SCALAR_INT;
  case CYAML_KIND_FLOAT:
    return TURBO_YAML_SCALAR_FLOAT;
  default:
    return TURBO_YAML_SCALAR_STRING;
  }
}

static int turbo_yaml_sax_document_start(void *ctx) {
  turbo_yaml_sax_parser_t *parser = (turbo_yaml_sax_parser_t *)ctx;
  return !parser->handler.on_document_start || parser->handler.on_document_start(parser->ctx) == 0
             ? 0
             : -1;
}

static int turbo_yaml_sax_document_end(void *ctx) {
  turbo_yaml_sax_parser_t *parser = (turbo_yaml_sax_parser_t *)ctx;
  return !parser->handler.on_document_end || parser->handler.on_document_end(parser->ctx) == 0 ? 0
                                                                                               : -1;
}

static int turbo_yaml_sax_null(void *ctx, bool is_key) {
  turbo_yaml_sax_parser_t *parser = (turbo_yaml_sax_parser_t *)ctx;
  return !parser->handler.on_null || parser->handler.on_null(parser->ctx, is_key) == 0 ? 0 : -1;
}

static int turbo_yaml_sax_scalar(void *ctx, cyaml_scalar_kind_t kind, const char *value,
                                 size_t value_len, bool is_key) {
  turbo_yaml_sax_parser_t *parser = (turbo_yaml_sax_parser_t *)ctx;
  return !parser->handler.on_scalar ||
                 parser->handler.on_scalar(parser->ctx, turbo_yaml_sax_kind(kind), value, value_len,
                                           is_key) == 0
             ? 0
             : -1;
}

#define TURBO_YAML_SAX_COLLECTION_CALLBACK(name)                                                   \
  static int turbo_yaml_sax_##name(void *ctx, bool is_key) {                                       \
    turbo_yaml_sax_parser_t *parser = (turbo_yaml_sax_parser_t *)ctx;                              \
    return !parser->handler.on_##name || parser->handler.on_##name(parser->ctx, is_key) == 0 ? 0   \
                                                                                             : -1; \
  }

TURBO_YAML_SAX_COLLECTION_CALLBACK(sequence_start)
TURBO_YAML_SAX_COLLECTION_CALLBACK(sequence_end)
TURBO_YAML_SAX_COLLECTION_CALLBACK(mapping_start)
TURBO_YAML_SAX_COLLECTION_CALLBACK(mapping_end)

#undef TURBO_YAML_SAX_COLLECTION_CALLBACK

static int turbo_yaml_sax_alias(void *ctx, const char *value, size_t value_len, bool is_key) {
  turbo_yaml_sax_parser_t *parser = (turbo_yaml_sax_parser_t *)ctx;
  return !parser->handler.on_alias ||
                 parser->handler.on_alias(parser->ctx, value, value_len, is_key) == 0
             ? 0
             : -1;
}

static const cyaml_sax_handler_t turbo_yaml_cyaml_handler = {
    .on_document_start = turbo_yaml_sax_document_start,
    .on_document_end = turbo_yaml_sax_document_end,
    .on_null = turbo_yaml_sax_null,
    .on_scalar = turbo_yaml_sax_scalar,
    .on_sequence_start = turbo_yaml_sax_sequence_start,
    .on_sequence_end = turbo_yaml_sax_sequence_end,
    .on_mapping_start = turbo_yaml_sax_mapping_start,
    .on_mapping_end = turbo_yaml_sax_mapping_end,
    .on_alias = turbo_yaml_sax_alias,
};

int turbo_parse_yaml_sax(const uint8_t *data, size_t len, const turbo_yaml_sax_handler_t *handler,
                         void *ctx) {
  turbo_yaml_sax_parser_t *parser;
  int rc = -1;
  if (!data || !handler) return -1;
  parser = turbo_yaml_sax_parser_create(handler, ctx);
  if (!parser) return -1;
  if (turbo_yaml_sax_parser_feed(parser, (const char *)data, len) == 0)
    rc = turbo_yaml_sax_parser_finish(parser);
  turbo_yaml_sax_parser_destroy(parser);
  return rc;
}

turbo_yaml_sax_parser_t *turbo_yaml_sax_parser_create(const turbo_yaml_sax_handler_t *handler,
                                                      void *ctx) {
  turbo_yaml_sax_parser_t *parser;
  if (!handler) return NULL;
  parser = (turbo_yaml_sax_parser_t *)calloc(1, sizeof(*parser));
  if (!parser) return NULL;
  parser->handler = *handler;
  parser->ctx = ctx;
  parser->parser = cyaml_sax_parser_create(&turbo_yaml_cyaml_handler, parser, NULL);
  if (!parser->parser) {
    free(parser);
    return NULL;
  }
  return parser;
}

int turbo_yaml_sax_parser_feed(turbo_yaml_sax_parser_t *parser, const char *data, size_t len) {
  const cyaml_error_t *error;
  if (!parser || cyaml_sax_parser_feed(parser->parser, data, len) != 0) {
    error = parser ? cyaml_sax_parser_error(parser->parser) : NULL;
    if (parser && error) snprintf(parser->error, sizeof(parser->error), "%s", error->msg);
    return -1;
  }
  return 0;
}

int turbo_yaml_sax_parser_finish(turbo_yaml_sax_parser_t *parser) {
  const cyaml_error_t *error;
  if (!parser || cyaml_sax_parser_finish(parser->parser) != 0) {
    error = parser ? cyaml_sax_parser_error(parser->parser) : NULL;
    if (parser && error) snprintf(parser->error, sizeof(parser->error), "%s", error->msg);
    return -1;
  }
  return 0;
}

const char *turbo_yaml_sax_parser_error(const turbo_yaml_sax_parser_t *parser) {
  return parser && parser->error[0] ? parser->error : NULL;
}

void turbo_yaml_sax_parser_destroy(turbo_yaml_sax_parser_t *parser) {
  if (!parser) return;
  cyaml_sax_parser_destroy(parser->parser);
  free(parser);
}

void turbo_free_yaml(turbo_yaml_doc_t **doc) {
  turbo_yaml_doc_t *yaml;
  if (!doc || !*doc) return;
  yaml = *doc;
  cyaml_free(yaml->doc);
  free(yaml->source);
  free(yaml);
  *doc = NULL;
}

turbo_yaml_node_t *turbo_yaml_root(const turbo_yaml_doc_t *doc) {
  return doc ? (turbo_yaml_node_t *)cyaml_root(doc->doc) : NULL;
}

turbo_yaml_node_type_t turbo_yaml_node_type(const turbo_yaml_node_t *node) {
  const cyaml_node_t *raw = (const cyaml_node_t *)node;
  if (!raw) return TURBO_YAML_NODE_NONE;
  switch (raw->type) {
  case CYAML_NULL:
    return TURBO_YAML_NODE_NULL;
  case CYAML_SCALAR:
    return TURBO_YAML_NODE_SCALAR;
  case CYAML_SEQ:
    return TURBO_YAML_NODE_SEQUENCE;
  case CYAML_MAP:
    return TURBO_YAML_NODE_MAPPING;
  case CYAML_ALIAS:
    return TURBO_YAML_NODE_ALIAS;
  default:
    return TURBO_YAML_NODE_NONE;
  }
}

turbo_yaml_scalar_kind_t turbo_yaml_scalar_kind(const turbo_yaml_doc_t *doc,
                                                const turbo_yaml_node_t *node) {
  cyaml_scalar_kind_t kind;
  if (!doc || !node) return TURBO_YAML_SCALAR_NULL;
  kind = cyaml_scalar_kind(doc->doc, (const cyaml_node_t *)node);
  switch (kind) {
  case CYAML_KIND_BOOL:
    return TURBO_YAML_SCALAR_BOOL;
  case CYAML_KIND_INT:
    return TURBO_YAML_SCALAR_INT;
  case CYAML_KIND_FLOAT:
    return TURBO_YAML_SCALAR_FLOAT;
  case CYAML_KIND_STRING:
    return TURBO_YAML_SCALAR_STRING;
  default:
    return TURBO_YAML_SCALAR_NULL;
  }
}

char *turbo_yaml_scalar_dup(const turbo_yaml_doc_t *doc, const turbo_yaml_node_t *node) {
  return (doc && node) ? cyaml_scalar_str(doc->doc, (const cyaml_node_t *)node) : NULL;
}

size_t turbo_yaml_sequence_size(const turbo_yaml_node_t *node) {
  return cyaml_seq_len((const cyaml_node_t *)node);
}

turbo_yaml_node_t *turbo_yaml_sequence_get(const turbo_yaml_node_t *node, size_t index) {
  if (index > UINT32_MAX) return NULL;
  return (turbo_yaml_node_t *)cyaml_seq_get((const cyaml_node_t *)node, (uint32_t)index);
}

size_t turbo_yaml_mapping_size(const turbo_yaml_node_t *node) {
  return cyaml_map_len((const cyaml_node_t *)node);
}

turbo_yaml_node_t *turbo_yaml_mapping_key(const turbo_yaml_node_t *node, size_t index) {
  cyaml_pair_t *pair;
  if (index > UINT32_MAX) return NULL;
  pair = cyaml_map_at((const cyaml_node_t *)node, (uint32_t)index);
  return pair ? (turbo_yaml_node_t *)pair->key : NULL;
}

turbo_yaml_node_t *turbo_yaml_mapping_value(const turbo_yaml_node_t *node, size_t index) {
  cyaml_pair_t *pair;
  if (index > UINT32_MAX) return NULL;
  pair = cyaml_map_at((const cyaml_node_t *)node, (uint32_t)index);
  return pair ? (turbo_yaml_node_t *)pair->val : NULL;
}

turbo_yaml_node_t *turbo_yaml_mapping_get(const turbo_yaml_doc_t *doc,
                                          const turbo_yaml_node_t *node, const char *key) {
  return (doc && node && key)
             ? (turbo_yaml_node_t *)cyaml_get(doc->doc, (const cyaml_node_t *)node, key)
             : NULL;
}

bool turbo_yaml_mapping_contains(const turbo_yaml_doc_t *doc, const turbo_yaml_node_t *node,
                                 const char *key) {
  return turbo_yaml_mapping_get(doc, node, key) != NULL;
}

bool turbo_yaml_node_location(const turbo_yaml_node_t *node, turbo_yaml_location_t *location) {
  const cyaml_node_t *raw = (const cyaml_node_t *)node;
  if (!raw || !location) return false;
  turbo_yaml_copy_location(location, &raw->span);
  return true;
}

turbo_yaml_node_t *turbo_yaml_alias_target(const turbo_yaml_node_t *node) {
  const cyaml_node_t *raw = (const cyaml_node_t *)node;
  return raw && raw->type == CYAML_ALIAS ? (turbo_yaml_node_t *)raw->alias.target : NULL;
}

turbo_yaml_path_result_t *turbo_yaml_path_query(const turbo_yaml_doc_t *doc,
                                                const turbo_yaml_node_t *context,
                                                const char *expr) {
  turbo_yaml_path_result_t *result;
  if (!doc || !expr) return NULL;
  result = (turbo_yaml_path_result_t *)calloc(1, sizeof(*result));
  if (!result) return NULL;
  result->result = cyaml_path_query(doc->doc, (const cyaml_node_t *)context, expr);
  return result;
}

size_t turbo_yaml_path_result_size(const turbo_yaml_path_result_t *result) {
  return result ? cyaml_path_count(&result->result) : 0;
}

turbo_yaml_node_t *turbo_yaml_path_result_get(const turbo_yaml_path_result_t *result,
                                              size_t index) {
  if (!result || index > UINT32_MAX) return NULL;
  return (turbo_yaml_node_t *)cyaml_path_get(&result->result, (uint32_t)index);
}

const char *turbo_yaml_path_result_error(const turbo_yaml_path_result_t *result) {
  return result ? result->result.error : "Invalid YAML path result";
}

size_t turbo_yaml_path_result_error_pos(const turbo_yaml_path_result_t *result) {
  return result ? result->result.error_pos : 0;
}

void turbo_yaml_path_result_free(turbo_yaml_path_result_t *result) {
  if (!result) return;
  cyaml_path_result_free(&result->result);
  free(result);
}

char *turbo_yaml_emit(const turbo_yaml_doc_t *doc, size_t *out_len) {
  return doc ? cyaml_emit(doc->doc, NULL, out_len) : NULL;
}

char *turbo_yaml_serialize(const turbo_yaml_doc_t *doc, size_t *out_len) {
  return turbo_yaml_emit(doc, out_len);
}

char *turbo_yaml_emit_node(const turbo_yaml_doc_t *doc, const turbo_yaml_node_t *node,
                           size_t *out_len) {
  return (doc && node) ? cyaml_emit_node(doc->doc, (const cyaml_node_t *)node, NULL, out_len)
                       : NULL;
}

void turbo_yaml_string_free(char *str) { free(str); }

void turbo_yaml_serialize_free(char *str) { free(str); }

int turbo_yaml_write(const turbo_yaml_doc_t *doc, turbo_write_fn write, void *user) {
  size_t len = 0;
  char *text;
  int rc;
  if (!doc || !write) return -1;
  text = turbo_yaml_serialize(doc, &len);
  if (!text) return -1;
  rc = write(text, len, user);
  free(text);
  return rc == 0 ? 0 : -1;
}

turbo_yaml_doc_t *turbo_yaml_from_json(const json_value_t *value) {
  turbo_yaml_doc_t *yaml;
  if (!value) return NULL;
  yaml = (turbo_yaml_doc_t *)calloc(1, sizeof(*yaml));
  if (!yaml) return NULL;
  yaml->doc = cyaml_doc_from_json_value(value);
  if (!yaml->doc) {
    free(yaml);
    return NULL;
  }
  return yaml;
}

json_value_t *turbo_yaml_to_json(const turbo_yaml_doc_t *doc) {
  return doc ? json_value_from_cyaml(doc->doc) : NULL;
}

json_value_t *turbo_yaml_node_to_json(const turbo_yaml_doc_t *doc, const turbo_yaml_node_t *node) {
  return (doc && node) ? json_value_from_cyaml_node(doc->doc, (const cyaml_node_t *)node) : NULL;
}

/* XML (cxml) */
int turbo_parse_xml(const uint8_t *data, size_t len, turbo_xml_doc_t **out) {
  if (!data || !out) return -1;
  *out = NULL;

  // Ensure data is null-terminated for cxml_parse_xml
  char *temp = (char *)malloc(len + 1);
  if (!temp) return -1;
  memcpy(temp, data, len);
  temp[len] = '\0';

  cxml_root_node *root = cxml_parse_xml(temp);
  free(temp);

  if (!root) return -1;

  *out = (turbo_xml_doc_t *)root;
  return 0;
}

#define TURBO_XML_SAX_MAX_DEPTH 256
#define TURBO_XML_SAX_ERROR_CAP 256

struct turbo_xml_sax_parser_s {
  turbo_xml_sax_handler_t handler;
  void *ctx;
  tstr_t buffer;
  size_t pos;
  tstr_t stack[TURBO_XML_SAX_MAX_DEPTH];
  size_t depth;
  bool started;
  bool finished;
  bool failed;
  bool root_seen;
  bool root_closed;
  char error[TURBO_XML_SAX_ERROR_CAP];
};

static char g_xml_sax_error[TURBO_XML_SAX_ERROR_CAP] = {0};

static void turbo_xml_sax_set_error(turbo_xml_sax_parser_t *parser, const char *fmt_str, ...) {
  va_list ap;
  va_start(ap, fmt_str);
  vsnprintf(g_xml_sax_error, sizeof(g_xml_sax_error), fmt_str, ap);
  va_end(ap);

  if (parser) {
    va_start(ap, fmt_str);
    vsnprintf(parser->error, sizeof(parser->error), fmt_str, ap);
    va_end(ap);
    parser->failed = true;
  }
}

static bool turbo_xml_sax_is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static void turbo_xml_sax_skip_ws(const char *data, size_t len, size_t *pos) {
  while (*pos < len && turbo_xml_sax_is_ws(data[*pos]))
    ++*pos;
}

static bool turbo_xml_sax_is_name_start(char c) {
  unsigned char u = (unsigned char)c;
  return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || c == '_' || c == ':';
}

static bool turbo_xml_sax_is_name_char(char c) {
  unsigned char u = (unsigned char)c;
  return turbo_xml_sax_is_name_start(c) || (u >= '0' && u <= '9') || c == '-' || c == '.';
}

static int turbo_xml_sax_call_failed(turbo_xml_sax_parser_t *parser) {
  turbo_xml_sax_set_error(parser, "SAX callback failed");
  return -1;
}

static int turbo_xml_sax_start_document(turbo_xml_sax_parser_t *parser) {
  if (parser->started) return 0;
  parser->started = true;
  if (parser->handler.on_start_document && parser->handler.on_start_document(parser->ctx) != 0)
    return turbo_xml_sax_call_failed(parser);
  return 0;
}

static int turbo_xml_sax_emit_text(turbo_xml_sax_parser_t *parser, const char *text, size_t len) {
  if (len == 0) return 0;
  if (parser->depth == 0) {
    for (size_t i = 0; i < len; ++i) {
      if (!turbo_xml_sax_is_ws(text[i])) {
        turbo_xml_sax_set_error(parser, "Text outside root element");
        return -1;
      }
    }
    return 0;
  }
  if (parser->handler.on_text && parser->handler.on_text(parser->ctx, text, len) != 0)
    return turbo_xml_sax_call_failed(parser);
  return 0;
}

static int turbo_xml_sax_emit_comment(turbo_xml_sax_parser_t *parser, const char *text,
                                      size_t len) {
  if (parser->handler.on_comment && parser->handler.on_comment(parser->ctx, text, len) != 0)
    return turbo_xml_sax_call_failed(parser);
  return 0;
}

static int turbo_xml_sax_emit_cdata(turbo_xml_sax_parser_t *parser, const char *text, size_t len) {
  if (parser->handler.on_cdata && parser->handler.on_cdata(parser->ctx, text, len) != 0)
    return turbo_xml_sax_call_failed(parser);
  return 0;
}

static int turbo_xml_sax_emit_doctype(turbo_xml_sax_parser_t *parser, const char *text,
                                      size_t len) {
  if (parser->root_seen) {
    turbo_xml_sax_set_error(parser, "DOCTYPE after root element");
    return -1;
  }
  if (parser->handler.on_doctype && parser->handler.on_doctype(parser->ctx, text, len) != 0)
    return turbo_xml_sax_call_failed(parser);
  return 0;
}

static int turbo_xml_sax_emit_pi(turbo_xml_sax_parser_t *parser, const char *target,
                                 size_t target_len, const char *data, size_t data_len) {
  if (parser->handler.on_processing_instruction &&
      parser->handler.on_processing_instruction(parser->ctx, target, target_len, data, data_len) !=
          0)
    return turbo_xml_sax_call_failed(parser);
  return 0;
}

static int turbo_xml_sax_push(turbo_xml_sax_parser_t *parser, const char *name, size_t name_len) {
  if (parser->depth >= TURBO_XML_SAX_MAX_DEPTH) {
    turbo_xml_sax_set_error(parser, "Max XML depth exceeded");
    return -1;
  }
  tstr_t owned = tstr_dup_len(name, name_len);
  if (!owned) {
    turbo_xml_sax_set_error(parser, "Out of memory");
    return -1;
  }
  parser->stack[parser->depth++] = owned;
  return 0;
}

static int turbo_xml_sax_pop(turbo_xml_sax_parser_t *parser, const char *name, size_t name_len) {
  if (parser->depth == 0) {
    turbo_xml_sax_set_error(parser, "Unexpected closing element");
    return -1;
  }

  tstr_t top = parser->stack[parser->depth - 1];
  if (tstr_len(top) != name_len || memcmp(top, name, name_len) != 0) {
    turbo_xml_sax_set_error(parser, "Mismatched closing element");
    return -1;
  }

  tstr_free(top);
  parser->stack[--parser->depth] = NULL;
  if (parser->depth == 0) parser->root_closed = true;
  return 0;
}

static int turbo_xml_sax_parse_name(turbo_xml_sax_parser_t *parser, const char *data, size_t len,
                                    size_t *pos, size_t *name_start, size_t *name_len, bool final) {
  if (*pos >= len) return final ? (turbo_xml_sax_set_error(parser, "Expected XML name"), -1) : 0;
  if (!turbo_xml_sax_is_name_start(data[*pos])) {
    turbo_xml_sax_set_error(parser, "Expected XML name");
    return -1;
  }

  *name_start = *pos;
  ++*pos;
  while (*pos < len && turbo_xml_sax_is_name_char(data[*pos]))
    ++*pos;
  *name_len = *pos - *name_start;
  return 1;
}

static int turbo_xml_sax_find_until(turbo_xml_sax_parser_t *parser, const char *data, size_t len,
                                    size_t start, const char *needle, size_t needle_len,
                                    size_t *end, bool final, const char *err) {
  for (size_t i = start; i + needle_len <= len; ++i) {
    if (memcmp(data + i, needle, needle_len) == 0) {
      *end = i;
      return 1;
    }
  }
  if (final) {
    turbo_xml_sax_set_error(parser, "%s", err);
    return -1;
  }
  return 0;
}

static int turbo_xml_sax_scan_markup_end(turbo_xml_sax_parser_t *parser, const char *data,
                                         size_t len, size_t start, size_t *end, bool final,
                                         const char *err) {
  char quote = '\0';
  int bracket_depth = 0;
  for (size_t i = start; i < len; ++i) {
    char c = data[i];
    if (quote) {
      if (c == quote) quote = '\0';
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '[') {
      ++bracket_depth;
    } else if (c == ']' && bracket_depth > 0) {
      --bracket_depth;
    } else if (c == '>' && bracket_depth == 0) {
      *end = i;
      return 1;
    }
  }
  if (final) {
    turbo_xml_sax_set_error(parser, "%s", err);
    return -1;
  }
  return 0;
}

static int turbo_xml_sax_parse_pi(turbo_xml_sax_parser_t *parser, const char *data, size_t len,
                                  bool final) {
  size_t end = 0;
  int found = turbo_xml_sax_find_until(parser, data, len, parser->pos + 2, "?>", 2, &end, final,
                                       "Unterminated processing instruction");
  if (found <= 0) return found;

  size_t target_start = parser->pos + 2;
  size_t target_pos = target_start;
  size_t target_len = 0;
  int name_rc =
      turbo_xml_sax_parse_name(parser, data, end, &target_pos, &target_start, &target_len, true);
  if (name_rc <= 0) return -1;

  size_t body_start = target_pos;
  while (body_start < end && turbo_xml_sax_is_ws(data[body_start]))
    ++body_start;
  if (turbo_xml_sax_emit_pi(parser, data + target_start, target_len, data + body_start,
                            end - body_start) != 0)
    return -1;
  parser->pos = end + 2;
  return 1;
}

static int turbo_xml_sax_parse_comment(turbo_xml_sax_parser_t *parser, const char *data, size_t len,
                                       bool final) {
  size_t end = 0;
  int found = turbo_xml_sax_find_until(parser, data, len, parser->pos + 4, "-->", 3, &end, final,
                                       "Unterminated comment");
  if (found <= 0) return found;
  if (turbo_xml_sax_emit_comment(parser, data + parser->pos + 4, end - parser->pos - 4) != 0)
    return -1;
  parser->pos = end + 3;
  return 1;
}

static int turbo_xml_sax_parse_cdata(turbo_xml_sax_parser_t *parser, const char *data, size_t len,
                                     bool final) {
  size_t end = 0;
  int found = turbo_xml_sax_find_until(parser, data, len, parser->pos + 9, "]]>", 3, &end, final,
                                       "Unterminated CDATA");
  if (found <= 0) return found;
  if (turbo_xml_sax_emit_cdata(parser, data + parser->pos + 9, end - parser->pos - 9) != 0)
    return -1;
  parser->pos = end + 3;
  return 1;
}

static int turbo_xml_sax_parse_doctype(turbo_xml_sax_parser_t *parser, const char *data, size_t len,
                                       bool final) {
  size_t end = 0;
  int found = turbo_xml_sax_scan_markup_end(parser, data, len, parser->pos + 2, &end, final,
                                            "Unterminated DOCTYPE");
  if (found <= 0) return found;
  if (turbo_xml_sax_emit_doctype(parser, data + parser->pos + 2, end - parser->pos - 2) != 0)
    return -1;
  parser->pos = end + 1;
  return 1;
}

static int turbo_xml_sax_emit_start_tag(turbo_xml_sax_parser_t *parser, const char *data,
                                        size_t tag_start, size_t tag_end, size_t name_start,
                                        size_t name_len, bool self_closing) {
  if (parser->root_closed) {
    turbo_xml_sax_set_error(parser, "Multiple root elements");
    return -1;
  }
  if (parser->depth == 0) parser->root_seen = true;

  if (parser->handler.on_element_start &&
      parser->handler.on_element_start(parser->ctx, data + name_start, name_len) != 0)
    return turbo_xml_sax_call_failed(parser);

  size_t attr_pos = name_start + name_len;
  while (attr_pos < tag_end) {
    turbo_xml_sax_skip_ws(data, tag_end, &attr_pos);
    if (attr_pos >= tag_end) break;

    size_t attr_name_start = 0;
    size_t attr_name_len = 0;
    int name_rc = turbo_xml_sax_parse_name(parser, data, tag_end, &attr_pos, &attr_name_start,
                                           &attr_name_len, true);
    if (name_rc <= 0) return -1;
    turbo_xml_sax_skip_ws(data, tag_end, &attr_pos);
    if (attr_pos >= tag_end || data[attr_pos] != '=') {
      turbo_xml_sax_set_error(parser, "Expected = after XML attribute");
      return -1;
    }
    ++attr_pos;
    turbo_xml_sax_skip_ws(data, tag_end, &attr_pos);
    if (attr_pos >= tag_end || (data[attr_pos] != '"' && data[attr_pos] != '\'')) {
      turbo_xml_sax_set_error(parser, "Expected quoted XML attribute value");
      return -1;
    }

    char quote = data[attr_pos++];
    size_t value_start = attr_pos;
    while (attr_pos < tag_end && data[attr_pos] != quote) {
      if (data[attr_pos] == '<') {
        turbo_xml_sax_set_error(parser, "Invalid < in XML attribute value");
        return -1;
      }
      ++attr_pos;
    }
    if (attr_pos >= tag_end) {
      turbo_xml_sax_set_error(parser, "Unterminated XML attribute value");
      return -1;
    }
    if (parser->handler.on_attribute &&
        parser->handler.on_attribute(parser->ctx, data + attr_name_start, attr_name_len,
                                     data + value_start, attr_pos - value_start) != 0)
      return turbo_xml_sax_call_failed(parser);
    ++attr_pos;
  }

  if (!self_closing) {
    return turbo_xml_sax_push(parser, data + name_start, name_len);
  }

  if (parser->handler.on_element_end &&
      parser->handler.on_element_end(parser->ctx, data + name_start, name_len) != 0)
    return turbo_xml_sax_call_failed(parser);
  if (parser->depth == 0) parser->root_closed = true;
  (void)tag_start;
  return 0;
}

static int turbo_xml_sax_parse_start_tag(turbo_xml_sax_parser_t *parser, const char *data,
                                         size_t len, bool final) {
  size_t pos = parser->pos + 1;
  size_t name_start = 0;
  size_t name_len = 0;
  int name_rc = turbo_xml_sax_parse_name(parser, data, len, &pos, &name_start, &name_len, final);
  if (name_rc <= 0) return name_rc;

  while (true) {
    turbo_xml_sax_skip_ws(data, len, &pos);
    if (pos >= len)
      return final ? (turbo_xml_sax_set_error(parser, "Unterminated start tag"), -1) : 0;
    if (data[pos] == '>') {
      if (turbo_xml_sax_emit_start_tag(parser, data, parser->pos, pos, name_start, name_len,
                                       false) != 0)
        return -1;
      parser->pos = pos + 1;
      return 1;
    }
    if (data[pos] == '/' && pos + 1 < len && data[pos + 1] == '>') {
      if (turbo_xml_sax_emit_start_tag(parser, data, parser->pos, pos, name_start, name_len,
                                       true) != 0)
        return -1;
      parser->pos = pos + 2;
      return 1;
    }
    if (data[pos] == '/' && pos + 1 >= len)
      return final ? (turbo_xml_sax_set_error(parser, "Unterminated start tag"), -1) : 0;

    size_t attr_name_start = 0;
    size_t attr_name_len = 0;
    name_rc =
        turbo_xml_sax_parse_name(parser, data, len, &pos, &attr_name_start, &attr_name_len, final);
    if (name_rc <= 0) return name_rc;
    turbo_xml_sax_skip_ws(data, len, &pos);
    if (pos >= len)
      return final ? (turbo_xml_sax_set_error(parser, "Expected = after XML attribute"), -1) : 0;
    if (data[pos++] != '=') {
      turbo_xml_sax_set_error(parser, "Expected = after XML attribute");
      return -1;
    }
    turbo_xml_sax_skip_ws(data, len, &pos);
    if (pos >= len)
      return final ? (turbo_xml_sax_set_error(parser, "Expected quoted XML attribute value"), -1)
                   : 0;
    if (data[pos] != '"' && data[pos] != '\'') {
      turbo_xml_sax_set_error(parser, "Expected quoted XML attribute value");
      return -1;
    }
    char quote = data[pos++];
    while (pos < len && data[pos] != quote) {
      if (data[pos] == '<') {
        turbo_xml_sax_set_error(parser, "Invalid < in XML attribute value");
        return -1;
      }
      ++pos;
    }
    if (pos >= len)
      return final ? (turbo_xml_sax_set_error(parser, "Unterminated XML attribute value"), -1) : 0;
    ++pos;
  }
}

static int turbo_xml_sax_parse_end_tag(turbo_xml_sax_parser_t *parser, const char *data, size_t len,
                                       bool final) {
  size_t pos = parser->pos + 2;
  size_t name_start = 0;
  size_t name_len = 0;
  int name_rc = turbo_xml_sax_parse_name(parser, data, len, &pos, &name_start, &name_len, final);
  if (name_rc <= 0) return name_rc;
  turbo_xml_sax_skip_ws(data, len, &pos);
  if (pos >= len)
    return final ? (turbo_xml_sax_set_error(parser, "Unterminated closing tag"), -1) : 0;
  if (data[pos] != '>') {
    turbo_xml_sax_set_error(parser, "Expected > after closing element");
    return -1;
  }
  if (turbo_xml_sax_pop(parser, data + name_start, name_len) != 0) return -1;
  if (parser->handler.on_element_end &&
      parser->handler.on_element_end(parser->ctx, data + name_start, name_len) != 0)
    return turbo_xml_sax_call_failed(parser);
  parser->pos = pos + 1;
  return 1;
}

static int turbo_xml_sax_parse_markup(turbo_xml_sax_parser_t *parser, const char *data, size_t len,
                                      bool final) {
  if (parser->pos + 1 >= len)
    return final ? (turbo_xml_sax_set_error(parser, "Unterminated markup"), -1) : 0;

  if (memcmp(data + parser->pos, "<!--", (len - parser->pos >= 4) ? 4 : len - parser->pos) == 0) {
    if (len - parser->pos < 4)
      return final ? (turbo_xml_sax_set_error(parser, "Unterminated comment"), -1) : 0;
    return turbo_xml_sax_parse_comment(parser, data, len, final);
  }
  if (len - parser->pos >= 9 && memcmp(data + parser->pos, "<![CDATA[", 9) == 0)
    return turbo_xml_sax_parse_cdata(parser, data, len, final);
  if (len - parser->pos < 9 && memcmp(data + parser->pos, "<![CDATA[", len - parser->pos) == 0)
    return final ? (turbo_xml_sax_set_error(parser, "Unterminated CDATA"), -1) : 0;
  if (len - parser->pos >= 2 && data[parser->pos + 1] == '!')
    return turbo_xml_sax_parse_doctype(parser, data, len, final);
  if (len - parser->pos >= 2 && data[parser->pos + 1] == '?')
    return turbo_xml_sax_parse_pi(parser, data, len, final);
  if (len - parser->pos >= 2 && data[parser->pos + 1] == '/')
    return turbo_xml_sax_parse_end_tag(parser, data, len, final);
  return turbo_xml_sax_parse_start_tag(parser, data, len, final);
}

static void turbo_xml_sax_compact(turbo_xml_sax_parser_t *parser) {
  size_t len = tstr_len(parser->buffer);
  if (!parser->buffer || parser->pos == 0) return;
  if (parser->pos >= len) {
    tstr_clear(parser->buffer);
    parser->pos = 0;
    return;
  }

  size_t remaining = len - parser->pos;
  memmove(parser->buffer, parser->buffer + parser->pos, remaining);
  (void)tstr_set_len_checked(parser->buffer, remaining);
  parser->pos = 0;
}

static int turbo_xml_sax_run(turbo_xml_sax_parser_t *parser, bool final) {
  if (turbo_xml_sax_start_document(parser) != 0) return -1;

  const char *data = parser->buffer ? parser->buffer : "";
  size_t len = tstr_len(parser->buffer);

  while (parser->pos < len) {
    char *lt = memchr(data + parser->pos, '<', len - parser->pos);
    if (!lt) {
      if (parser->depth == 0 && !final) break;
      if (turbo_xml_sax_emit_text(parser, data + parser->pos, len - parser->pos) != 0) return -1;
      parser->pos = len;
      break;
    }

    size_t lt_pos = (size_t)(lt - data);
    if (lt_pos > parser->pos) {
      if (turbo_xml_sax_emit_text(parser, data + parser->pos, lt_pos - parser->pos) != 0) return -1;
      parser->pos = lt_pos;
    }

    int rc = turbo_xml_sax_parse_markup(parser, data, len, final);
    if (rc <= 0) {
      if (rc < 0) return -1;
      break;
    }
  }

  turbo_xml_sax_compact(parser);
  return 0;
}

turbo_xml_sax_parser_t *turbo_xml_sax_parser_create(const turbo_xml_sax_handler_t *handler,
                                                    void *ctx) {
  if (!handler) {
    fmt(g_xml_sax_error, sizeof(g_xml_sax_error), "Invalid arguments");
    return NULL;
  }

  turbo_xml_sax_parser_t *parser = (turbo_xml_sax_parser_t *)calloc(1, sizeof(*parser));
  if (!parser) {
    fmt(g_xml_sax_error, sizeof(g_xml_sax_error), "Out of memory");
    return NULL;
  }
  parser->handler = *handler;
  parser->ctx = ctx;
  parser->buffer = tstr_new();
  if (!parser->buffer) {
    free(parser);
    fmt(g_xml_sax_error, sizeof(g_xml_sax_error), "Out of memory");
    return NULL;
  }
  return parser;
}

int turbo_xml_sax_parser_feed(turbo_xml_sax_parser_t *parser, const char *data, size_t len) {
  if (!parser || (!data && len > 0)) {
    fmt(g_xml_sax_error, sizeof(g_xml_sax_error), "Invalid arguments");
    return -1;
  }
  if (parser->failed) return -1;
  if (parser->finished) {
    turbo_xml_sax_set_error(parser, "Parser already finished");
    return -1;
  }
  if (len == 0) return 0;

  tstr_t next = tstr_cat_len(parser->buffer, data, len);
  if (!next) {
    turbo_xml_sax_set_error(parser, "Out of memory");
    return -1;
  }
  parser->buffer = next;
  return turbo_xml_sax_run(parser, false);
}

int turbo_xml_sax_parser_finish(turbo_xml_sax_parser_t *parser) {
  if (!parser) {
    fmt(g_xml_sax_error, sizeof(g_xml_sax_error), "Invalid arguments");
    return -1;
  }
  if (parser->failed) return -1;
  if (parser->finished) {
    turbo_xml_sax_set_error(parser, "Parser already finished");
    return -1;
  }

  parser->finished = true;
  if (turbo_xml_sax_run(parser, true) != 0) return -1;
  if (parser->depth != 0) {
    turbo_xml_sax_set_error(parser, "Unclosed XML element");
    return -1;
  }
  if (!parser->root_seen) {
    turbo_xml_sax_set_error(parser, "Expected XML root element");
    return -1;
  }
  if (parser->handler.on_end_document && parser->handler.on_end_document(parser->ctx) != 0)
    return turbo_xml_sax_call_failed(parser);
  return 0;
}

const char *turbo_xml_sax_parser_error(const turbo_xml_sax_parser_t *parser) {
  if (!parser) return g_xml_sax_error;
  return parser->error[0] ? parser->error : g_xml_sax_error;
}

void turbo_xml_sax_parser_destroy(turbo_xml_sax_parser_t *parser) {
  if (!parser) return;
  for (size_t i = 0; i < parser->depth; ++i)
    tstr_free(parser->stack[i]);
  tstr_free(parser->buffer);
  free(parser);
}

int turbo_parse_xml_sax(const uint8_t *data, size_t len, const turbo_xml_sax_handler_t *handler,
                        void *ctx) {
  if (!data || len == 0 || !handler) {
    fmt(g_xml_sax_error, sizeof(g_xml_sax_error), "Invalid arguments");
    return -1;
  }

  turbo_xml_sax_parser_t *parser = turbo_xml_sax_parser_create(handler, ctx);
  if (!parser) return -1;

  int rc = turbo_xml_sax_parser_feed(parser, (const char *)data, len);
  if (rc == 0) rc = turbo_xml_sax_parser_finish(parser);
  turbo_xml_sax_parser_destroy(parser);
  return rc;
}

void turbo_free_xml(turbo_xml_doc_t **out) {
  if (!out || !*out) return;
  cxml_root_node_free((cxml_root_node *)*out);
  *out = NULL;
}

char *turbo_xml_serialize(const turbo_xml_doc_t *doc, size_t *out_len) {
  char *text;
  if (out_len) *out_len = 0;
  if (!doc) return NULL;
  text = cxml_document_to_xml_rstring((cxml_root_node *)doc);
  if (text && out_len) *out_len = strlen(text);
  return text;
}

int turbo_xml_write(const turbo_xml_doc_t *doc, turbo_write_fn write, void *user) {
  size_t len = 0;
  char *text;
  int rc;
  if (!doc || !write) return -1;
  text = turbo_xml_serialize(doc, &len);
  if (!text) return -1;
  rc = write(text, len, user);
  free(text);
  return rc == 0 ? 0 : -1;
}

void turbo_xml_serialize_free(char *str) { free(str); }

turbo_xml_doc_t *turbo_xml_create_document(const char *root_name) {
  cxml_root_node *doc;
  cxml_elem_node *root;
  if (!root_name || !root_name[0]) return NULL;
  doc = (cxml_root_node *)cxml_create_node(CXML_ROOT_NODE);
  root = (cxml_elem_node *)cxml_create_node(CXML_ELEM_NODE);
  if (!doc || !root || !cxml_set_name(root, NULL, root_name) || !cxml_add_child(doc, root)) {
    if (root && (!doc || root->parent != doc)) cxml_free_element_node(root);
    if (doc) cxml_free_root_node(doc);
    return NULL;
  }
  return (turbo_xml_doc_t *)doc;
}

turbo_xml_node_t *turbo_xml_add_element(void *parent, const char *name) {
  cxml_elem_node *element;
  if (!parent || !name || !name[0]) return NULL;
  element = (cxml_elem_node *)cxml_create_node(CXML_ELEM_NODE);
  if (!element || !cxml_set_name(element, NULL, name) || !cxml_add_child(parent, element)) {
    if (element) cxml_free_element_node(element);
    return NULL;
  }
  return (turbo_xml_node_t *)element;
}

int turbo_xml_set_text(turbo_xml_node_t *node, const char *text) {
  cxml_text_node *child;
  if (!node || !text) return -1;
  child = (cxml_text_node *)cxml_create_node(CXML_TEXT_NODE);
  if (!child || !cxml_set_text_value(child, text, false)) {
    if (child) cxml_free_text_node(child);
    return -1;
  }
  child->has_entity = true;
  if (!cxml_add_child(node, child)) {
    if (child) cxml_free_text_node(child);
    return -1;
  }
  return 0;
}

turbo_xml_node_t *turbo_xml_root_element(const turbo_xml_doc_t *doc) {
  return doc ? (turbo_xml_node_t *)doc->root_element : NULL;
}

const char *turbo_xml_node_name(const turbo_xml_node_t *node) {
  return node ? turbo_cxml_string_raw(&node->name.qname) : NULL;
}

void turbo_xml_list_init(turbo_xml_list_t *list) {
  if (!list) return;
  cxml_list_init((cxml_list *)list);
}

void turbo_xml_list_free(turbo_xml_list_t *list) {
  if (!list) return;
  cxml_list_free((cxml_list *)list);
}

turbo_xml_node_t *turbo_xml_find(turbo_xml_node_t *root, const char *query) {
  if (!root || !query) return NULL;
  return (turbo_xml_node_t *)cxml_find((cxml_elem_node *)root, query);
}

void turbo_xml_find_all(turbo_xml_node_t *root, const char *query, turbo_xml_list_t *out) {
  if (!out) return;

  if (!root || !query) {
    turbo_xml_list_init(out);
    return;
  }

  cxml_find_all((cxml_elem_node *)root, query, (cxml_list *)out);
}

char *turbo_xml_text_dup(turbo_xml_node_t *node) {
  if (!node) return NULL;
  return cxml_text((cxml_elem_node *)node, NULL);
}

char *turbo_xml_child_text_dup(turbo_xml_node_t *parent, const char *name) {
  if (!parent || !name) return strdup("");

  cxml_elem_node *elem_parent = (cxml_elem_node *)parent;
  cxml_for(node, &elem_parent->children) {
    if (cxml_get_node_type(node) == CXML_ELEM_NODE) {
      cxml_elem_node *elem = (cxml_elem_node *)node;
      if (elem->name.lname && strcmp(elem->name.lname, name) == 0) {
        char *text = cxml_text(elem, NULL);
        return text ? text : strdup("");
      }
    }
  }

  return strdup("");
}

const char *turbo_xml_get_text(const turbo_xml_doc_t *doc, const char *xpath) {
  if (!doc || !xpath) return NULL;

#ifdef CXML_USE_XPATH_MOD
  cxml_set *nodeset = cxml_xpath((void *)doc, xpath);
  if (!nodeset || cxml_set_is_empty(nodeset)) {
    turbo_cxml_set_destroy(nodeset);
    return NULL;
  }

  cxml_node_t *node = (cxml_node_t *)cxml_set_get(nodeset, 0);
  if (!node) {
    turbo_cxml_set_destroy(nodeset);
    return NULL;
  }

  const char *text = NULL;
  switch (*node) {
  case CXML_ELEM_NODE: {
    cxml_elem_node *elem = (cxml_elem_node *)node;
    if (elem->has_text && !cxml_list_is_empty(&elem->children)) {
      cxml_text_node *txt = (cxml_text_node *)cxml_list_get(&elem->children, 0);
      if (txt && txt->_type == CXML_TEXT_NODE) {
        text = cxml_string_as_raw(&txt->value);
      }
    }
    break;
  }
  case CXML_TEXT_NODE: {
    cxml_text_node *txt = (cxml_text_node *)node;
    text = cxml_string_as_raw(&txt->value);
    break;
  }
  default:
    break;
  }

  turbo_cxml_set_destroy(nodeset);
  return text;
#else
  (void)doc;
  (void>xpath;
  return NULL;
#endif
}

size_t turbo_xml_count(const turbo_xml_doc_t *doc, const char *xpath) {
  if (!doc || !xpath) return 0;

#ifdef CXML_USE_XPATH_MOD
  cxml_set *nodeset = cxml_xpath((void *)doc, xpath);
  if (!nodeset) return 0;

  size_t count = (size_t)cxml_set_size(nodeset);
  turbo_cxml_set_destroy(nodeset);
  return count;
#else
  (void)doc;
  (void>xpath;
  return 0;
#endif
}

turbo_xml_xpath_node_t *turbo_xml_xpath_get(const turbo_xml_doc_t *doc, const char *xpath) {
  if (!doc || !xpath) return NULL;

#ifdef CXML_USE_XPATH_MOD
  cxml_set *nodeset = cxml_xpath((void *)doc, xpath);
  if (!nodeset || cxml_set_is_empty(nodeset)) {
    turbo_cxml_set_destroy(nodeset);
    return NULL;
  }

  turbo_xml_xpath_node_t *node = (turbo_xml_xpath_node_t *)cxml_set_get(nodeset, 0);
  turbo_cxml_set_destroy(nodeset);
  return node;
#else
  (void)doc;
  (void)xpath;
  return NULL;
#endif
}

void turbo_xml_xpath_query(const turbo_xml_doc_t *doc, const char *xpath, turbo_xml_list_t *out) {
  if (!out) return;

  turbo_xml_list_init(out);

  if (!doc || !xpath) return;

#ifdef CXML_USE_XPATH_MOD
  cxml_set *nodeset = cxml_xpath((void *)doc, xpath);
  if (!nodeset) return;

  int count = cxml_set_size(nodeset);
  for (int i = 0; i < count; ++i) {
    void *node = cxml_set_get(nodeset, i);
    if (node) cxml_list_append((cxml_list *)out, node);
  }

  turbo_cxml_set_destroy(nodeset);
#else
  (void)doc;
  (void)xpath;
#endif
}

size_t turbo_xml_xpath_count(const turbo_xml_doc_t *doc, const char *xpath) {
  return turbo_xml_count(doc, xpath);
}

const char *turbo_xml_xpath_text(const turbo_xml_doc_t *doc, const char *xpath) {
  return turbo_xml_get_text(doc, xpath);
}

static cxml_node_t turbo_cxml_node_type(const turbo_xml_xpath_node_t *node) {
  if (!node) return (cxml_node_t)-1;
  return *(const cxml_node_t *)node;
}

turbo_xml_node_type_t turbo_xml_xpath_node_type(const turbo_xml_xpath_node_t *node) {
  switch (turbo_cxml_node_type(node)) {
  case CXML_TEXT_NODE:
    return TURBO_XML_NODE_TEXT;
  case CXML_ELEM_NODE:
    return TURBO_XML_NODE_ELEMENT;
  case CXML_COMM_NODE:
    return TURBO_XML_NODE_COMMENT;
  case CXML_ATTR_NODE:
    return TURBO_XML_NODE_ATTRIBUTE;
  case CXML_ROOT_NODE:
    return TURBO_XML_NODE_ROOT;
  case CXML_PI_NODE:
    return TURBO_XML_NODE_PI;
  case CXML_NS_NODE:
    return TURBO_XML_NODE_NAMESPACE;
  case CXML_XHDR_NODE:
    return TURBO_XML_NODE_XML_HEADER;
  case CXML_DTD_NODE:
    return TURBO_XML_NODE_DTD;
  default:
    return TURBO_XML_NODE_UNKNOWN;
  }
}

const char *turbo_xml_xpath_node_type_name(const turbo_xml_xpath_node_t *node) {
  switch (turbo_xml_xpath_node_type(node)) {
  case TURBO_XML_NODE_TEXT:
    return "text";
  case TURBO_XML_NODE_ELEMENT:
    return "element";
  case TURBO_XML_NODE_COMMENT:
    return "comment";
  case TURBO_XML_NODE_ATTRIBUTE:
    return "attribute";
  case TURBO_XML_NODE_ROOT:
    return "root";
  case TURBO_XML_NODE_PI:
    return "pi";
  case TURBO_XML_NODE_NAMESPACE:
    return "namespace";
  case TURBO_XML_NODE_XML_HEADER:
    return "xml_header";
  case TURBO_XML_NODE_DTD:
    return "dtd";
  default:
    return "unknown";
  }
}

const char *turbo_xml_xpath_node_name(const turbo_xml_xpath_node_t *node) {
  if (!node) return NULL;

  switch (turbo_cxml_node_type(node)) {
  case CXML_ELEM_NODE:
    return turbo_cxml_string_raw(&((const cxml_elem_node *)node)->name.qname);
  case CXML_ATTR_NODE:
    return turbo_cxml_string_raw(&((const cxml_attr_node *)node)->name.qname);
  case CXML_ROOT_NODE:
    return turbo_cxml_string_raw(&((const cxml_root_node *)node)->name);
  case CXML_PI_NODE:
    return turbo_cxml_string_raw(&((const cxml_pi_node *)node)->target);
  case CXML_NS_NODE: {
    const cxml_ns_node *ns = (const cxml_ns_node *)node;
    return ns->is_default ? "xmlns" : turbo_cxml_string_raw(&ns->prefix);
  }
  default:
    return NULL;
  }
}

const char *turbo_xml_xpath_node_text(const turbo_xml_xpath_node_t *node) {
  if (!node) return NULL;

  switch (turbo_cxml_node_type(node)) {
  case CXML_ELEM_NODE: {
    const cxml_elem_node *elem = (const cxml_elem_node *)node;
    if (elem->has_text && !cxml_list_is_empty((cxml_list *)&elem->children)) {
      cxml_text_node *txt = (cxml_text_node *)cxml_list_get((cxml_list *)&elem->children, 0);
      if (txt && txt->_type == CXML_TEXT_NODE) return cxml_string_as_raw(&txt->value);
    }
    return NULL;
  }
  case CXML_TEXT_NODE:
    return turbo_cxml_string_raw(&((const cxml_text_node *)node)->value);
  case CXML_ATTR_NODE:
    return turbo_cxml_string_raw(&((const cxml_attr_node *)node)->value);
  case CXML_COMM_NODE:
    return turbo_cxml_string_raw(&((const cxml_comm_node *)node)->value);
  case CXML_PI_NODE:
    return turbo_cxml_string_raw(&((const cxml_pi_node *)node)->value);
  case CXML_NS_NODE:
    return turbo_cxml_string_raw(&((const cxml_ns_node *)node)->uri);
  case CXML_DTD_NODE:
    return turbo_cxml_string_raw(&((const cxml_dtd_node *)node)->value);
  default:
    return NULL;
  }
}

char *turbo_xml_xpath_node_xml_dup(const turbo_xml_xpath_node_t *node) {
  if (!node) return NULL;
  return cxml_node_to_rstring((void *)node);
}

void turbo_xml_string_free(char *str) { free(str); }

turbo_json_type_t turbo_json_type(const json_value_t *value) {
  return (turbo_json_type_t)json_type(value);
}

bool turbo_json_is_null(const json_value_t *value) { return json_is_null(value); }

bool turbo_json_bool(const json_value_t *value) { return json_bool(value); }

double turbo_json_number(const json_value_t *value) { return json_number(value); }

const char *turbo_json_number_text(const json_value_t *value, size_t *len) {
  return json_number_text(value, len);
}

const char *turbo_json_string(const json_value_t *value) { return json_string(value); }

size_t turbo_json_string_len(const json_value_t *value) { return json_string_len(value); }

size_t turbo_json_object_size(const json_value_t *obj) { return json_object_size(obj); }

const char *turbo_json_object_key(const json_value_t *obj, size_t index) {
  return json_object_key(obj, index);
}

json_value_t *turbo_json_object_value(const json_value_t *obj, size_t index) {
  return json_object_value(obj, index);
}

json_value_t *turbo_json_object_get(const json_value_t *obj, const char *key) {
  return json_object_get(obj, key);
}

size_t turbo_json_array_size(const json_value_t *arr) { return json_array_size(arr); }

json_value_t *turbo_json_array_get(const json_value_t *arr, size_t index) {
  return json_array_get(arr, index);
}

int turbo_json_get_int(const json_value_t *obj, const char *key, int def) {
  return json_get_int(obj, key, def);
}

bool turbo_json_get_bool(const json_value_t *obj, const char *key, bool def) {
  return json_get_bool(obj, key, def);
}

double turbo_json_get_double(const json_value_t *obj, const char *key, double def) {
  return json_get_double(obj, key, def);
}

const char *turbo_json_get_string(const json_value_t *obj, const char *key) {
  return json_get_string(obj, key);
}

char *turbo_json_serialize(const json_value_t *value, size_t *out_len) {
  return json_serialize(value, out_len);
}

char *turbo_json_serialize_pretty(const json_value_t *value, size_t *out_len) {
  return json_serialize_pretty(value, out_len);
}

char *turbo_json_serialize_pretty_crlf(const json_value_t *value, size_t *out_len) {
  return json_serialize_pretty_crlf(value, out_len);
}

void turbo_json_serialize_free(char *str) { json_serialize_free(str); }

int turbo_json_write(const json_value_t *value, turbo_write_fn write, void *user) {
  size_t len = 0;
  char *text;
  int rc;
  if (!value || !write) return -1;
  text = json_serialize(value, &len);
  if (!text) return -1;
  rc = write(text, len, user);
  json_serialize_free(text);
  return rc == 0 ? 0 : -1;
}

json_value_t *turbo_json_clone(const json_value_t *value) { return json_clone(value); }

/* JSON Builder/Modifier */
json_value_t *turbo_json_create_object(void) { return json_create_object(); }
json_value_t *turbo_json_create_array(void) { return json_create_array(); }
json_value_t *turbo_json_create_string(const char *str) { return json_create_string(str); }
json_value_t *turbo_json_create_string_n(const char *str, size_t len) {
  return json_create_string_n(str, len);
}
json_value_t *turbo_json_create_number(double num) { return json_create_number(num); }
json_value_t *turbo_json_create_int64(int64_t num) { return json_create_int64(num); }
json_value_t *turbo_json_create_uint64(uint64_t num) { return json_create_uint64(num); }
json_value_t *turbo_json_create_bool(bool val) { return json_create_bool(val); }
json_value_t *turbo_json_create_null(void) { return json_create_null(); }

void turbo_json_object_add(json_value_t *obj, const char *key, json_value_t *val) {
  json_object_add(obj, key, val);
}

bool turbo_json_object_add_checked(json_value_t *obj, const char *key, json_value_t *val) {
  return json_object_add_checked(obj, key, val);
}

void turbo_json_array_add(json_value_t *arr, json_value_t *val) { json_array_add(arr, val); }

bool turbo_json_array_add_checked(json_value_t *arr, json_value_t *val) {
  return json_array_add_checked(arr, val);
}

void turbo_json_object_set_string(json_value_t *obj, const char *key, const char *val) {
  json_object_set_string(obj, key, val);
}

void turbo_json_object_set_number(json_value_t *obj, const char *key, double val) {
  json_object_set_number(obj, key, val);
}

void turbo_json_object_set_bool(json_value_t *obj, const char *key, bool val) {
  json_object_set_bool(obj, key, val);
}

void turbo_json_object_set_null(json_value_t *obj, const char *key) {
  json_object_set_null(obj, key);
}

/* CSV */
int turbo_parse_csv(const uint8_t *data, size_t len, turbo_csv_doc_t **out) {
  if (!data || !out) return -1;
  *out = NULL;
  csv_doc_t *doc = csv_parse((const char *)data, len);
  if (!doc) return -1;
  *out = (turbo_csv_doc_t *)doc;
  return 0;
}

int turbo_parse_csv_opts(const uint8_t *data, size_t len, const turbo_csv_options_t *opts,
                         turbo_csv_doc_t **out) {
  if (!data || !out) return -1;
  *out = NULL;

  if (!opts) {
    return turbo_parse_csv(data, len, out);
  }

  csv_options_t native_opts = {
      .has_header = opts->has_header,
      .delimiter = opts->delimiter,
      .quote = opts->quote,
      .skip_empty_rows = opts->skip_empty_rows,
  };

  csv_doc_t *doc = csv_parse_opts((const char *)data, len, &native_opts);
  if (!doc) return -1;

  *out = (turbo_csv_doc_t *)doc;
  return 0;
}

static csv_options_t turbo_csv_options_to_raw(const turbo_csv_options_t *opts) {
  csv_options_t raw = CSV_OPTIONS_DEFAULT;
  if (!opts) return raw;
  raw.has_header = opts->has_header;
  raw.delimiter = opts->delimiter;
  raw.quote = opts->quote;
  raw.skip_empty_rows = opts->skip_empty_rows;
  return raw;
}

static csv_stream_handler_t turbo_csv_sax_handler_to_raw(
    const turbo_csv_sax_handler_t *handler) {
  csv_stream_handler_t raw = {0};
  if (!handler) return raw;
  raw.on_row_start = handler->on_row_start;
  raw.on_field = handler->on_field;
  raw.on_row_end = handler->on_row_end;
  return raw;
}

int turbo_parse_csv_sax(const uint8_t *data, size_t len,
                        const turbo_csv_sax_handler_t *handler, void *ctx,
                        const turbo_csv_options_t *opts) {
  turbo_csv_sax_parser_t *parser;
  int rc;
  if (!data || !handler) return -1;
  parser = turbo_csv_sax_parser_create(handler, ctx, opts);
  if (!parser) return -1;
  rc = turbo_csv_sax_parser_feed(parser, (const char *)data, len);
  if (rc == 0) rc = turbo_csv_sax_parser_finish(parser);
  turbo_csv_sax_parser_destroy(parser);
  return rc;
}

turbo_csv_sax_parser_t *turbo_csv_sax_parser_create(
    const turbo_csv_sax_handler_t *handler, void *ctx, const turbo_csv_options_t *opts) {
  csv_stream_handler_t raw_handler;
  csv_options_t raw_opts;
  if (!handler) return NULL;
  raw_handler = turbo_csv_sax_handler_to_raw(handler);
  raw_opts = turbo_csv_options_to_raw(opts);
  return csv_sax_parser_create(&raw_handler, ctx, &raw_opts);
}

int turbo_csv_sax_parser_feed(turbo_csv_sax_parser_t *parser, const char *data, size_t len) {
  return csv_sax_parser_feed(parser, data, len);
}

int turbo_csv_sax_parser_finish(turbo_csv_sax_parser_t *parser) {
  return csv_sax_parser_finish(parser);
}

const char *turbo_csv_sax_parser_error(const turbo_csv_sax_parser_t *parser) {
  return csv_sax_parser_error(parser);
}

void turbo_csv_sax_parser_destroy(turbo_csv_sax_parser_t *parser) {
  csv_sax_parser_destroy(parser);
}

void turbo_free_csv(turbo_csv_doc_t **out) {
  if (!out || !*out) return;
  csv_free((csv_doc_t *)*out);
  *out = NULL;
}

size_t turbo_csv_row_count(const turbo_csv_doc_t *doc) {
  return csv_row_count((const csv_doc_t *)doc);
}

size_t turbo_csv_column_count(const turbo_csv_doc_t *doc) {
  return csv_column_count((const csv_doc_t *)doc);
}

const char *turbo_csv_get(const turbo_csv_doc_t *doc, size_t row, size_t col) {
  return csv_get((const csv_doc_t *)doc, row, col);
}

int turbo_csv_get_int(const turbo_csv_doc_t *doc, size_t row, size_t col, int def) {
  return csv_get_int((const csv_doc_t *)doc, row, col, def);
}

double turbo_csv_get_double(const turbo_csv_doc_t *doc, size_t row, size_t col, double def) {
  return csv_get_double((const csv_doc_t *)doc, row, col, def);
}

bool turbo_csv_get_bool(const turbo_csv_doc_t *doc, size_t row, size_t col, bool def) {
  return csv_get_bool((const csv_doc_t *)doc, row, col, def);
}

size_t turbo_csv_find_column(const turbo_csv_doc_t *doc, const char *header_name) {
  return csv_find_column((const csv_doc_t *)doc, header_name);
}

char *turbo_csv_serialize(const turbo_csv_doc_t *doc, size_t *out_len) {
  return csv_to_string_n((const csv_doc_t *)doc, out_len);
}

void turbo_csv_string_free(char *str) { free(str); }

void turbo_csv_serialize_free(char *str) { free(str); }

int turbo_csv_write(const turbo_csv_doc_t *doc, turbo_write_fn write, void *user) {
  return csv_write((const csv_doc_t *)doc, write, user);
}

int turbo_csv_write_records(const turbo_csv_doc_t *doc, turbo_write_fn write, void *user) {
  return csv_write_records((const csv_doc_t *)doc, write, user);
}

int turbo_csv_write_file(const turbo_csv_doc_t *doc, const char *filename) {
  return csv_write_file((const csv_doc_t *)doc, filename);
}

turbo_dsv_filter_t *turbo_dsv_filter_create(const turbo_csv_doc_t *doc, size_t header_row_index) {
  return (turbo_dsv_filter_t *)dsv_filter_create((const csv_doc_t *)doc, header_row_index);
}

void turbo_dsv_filter_destroy(turbo_dsv_filter_t *filter) {
  dsv_filter_destroy((dsv_filter_t *)filter);
}

const char *turbo_dsv_filter_error(turbo_dsv_filter_t *filter) {
  return dsv_filter_error((dsv_filter_t *)filter);
}

bool turbo_dsv_filter_compile(turbo_dsv_filter_t *filter, const char *expression) {
  return dsv_filter_compile((dsv_filter_t *)filter, expression);
}

void turbo_dsv_filter_set_output_delimiter(turbo_dsv_filter_t *filter, char delimiter) {
  dsv_filter_set_output_delimiter((dsv_filter_t *)filter, delimiter);
}

int turbo_dsv_filter_check_row(turbo_dsv_filter_t *filter, size_t row_index) {
  return dsv_filter_check_row((dsv_filter_t *)filter, row_index);
}

int turbo_dsv_filter_check_values(turbo_dsv_filter_t *filter, const tstr_v *fields,
                                  size_t field_count) {
  return dsv_filter_check_values((dsv_filter_t *)filter, fields, field_count);
}

void turbo_dsv_filter_run(turbo_dsv_filter_t *filter, turbo_dsv_row_callback_t callback,
                          void *user_data) {
  dsv_filter_run((dsv_filter_t *)filter, (dsv_row_callback_t)callback, user_data);
}

turbo_csv_stream_processor_t *turbo_csv_stream_processor_create(const turbo_csv_options_t *opts) {
  if (!opts) {
    return (turbo_csv_stream_processor_t *)csv_stream_processor_create(NULL);
  }

  csv_options_t native_opts = {
      .has_header = opts->has_header,
      .delimiter = opts->delimiter,
      .quote = opts->quote,
      .skip_empty_rows = opts->skip_empty_rows,
  };
  return (turbo_csv_stream_processor_t *)csv_stream_processor_create(&native_opts);
}

void turbo_csv_stream_processor_destroy(turbo_csv_stream_processor_t *p) {
  csv_stream_processor_destroy((csv_stream_processor_t *)p);
}

bool turbo_csv_stream_processor_set_filter(turbo_csv_stream_processor_t *p, const char *expr) {
  return csv_stream_processor_set_filter((csv_stream_processor_t *)p, expr);
}

void turbo_csv_stream_processor_set_columns(turbo_csv_stream_processor_t *p, const char *names) {
  csv_stream_processor_set_columns((csv_stream_processor_t *)p, names);
}

void turbo_csv_stream_processor_feed(const char *data, size_t len, void *user_data) {
  csv_stream_processor_feed(data, len, user_data);
}

void turbo_csv_stream_processor_finish(turbo_csv_stream_processor_t *p) {
  csv_stream_processor_finish((csv_stream_processor_t *)p);
}

size_t turbo_csv_stream_processor_row_count(const turbo_csv_stream_processor_t *p) {
  return csv_stream_processor_row_count((const csv_stream_processor_t *)p);
}

size_t turbo_csv_stream_processor_col_count(const turbo_csv_stream_processor_t *p) {
  return csv_stream_processor_col_count((const csv_stream_processor_t *)p);
}

const char *turbo_csv_stream_processor_col_name(const turbo_csv_stream_processor_t *p, size_t idx) {
  return csv_stream_processor_col_name((const csv_stream_processor_t *)p, idx);
}

size_t turbo_csv_stream_processor_col_index(const turbo_csv_stream_processor_t *p,
                                            const char *name) {
  return csv_stream_processor_col_index((const csv_stream_processor_t *)p, name);
}

const double *turbo_csv_stream_processor_col_data(const turbo_csv_stream_processor_t *p, size_t col,
                                                  size_t *out_len) {
  return csv_stream_processor_col_data((const csv_stream_processor_t *)p, col, out_len);
}

const char *turbo_csv_stream_processor_get_str(const turbo_csv_stream_processor_t *p, size_t row,
                                               size_t col) {
  return csv_stream_processor_get_str((const csv_stream_processor_t *)p, row, col);
}

const char *turbo_csv_stream_processor_error(const turbo_csv_stream_processor_t *p) {
  return csv_stream_processor_error((const csv_stream_processor_t *)p);
}

/* INI */
int turbo_parse_ini(const uint8_t *data, size_t len, void *out) {
  if (!data || !out) return -1;
  ini_t *ini = ini_parse((const char *)data, len);
  if (!ini) return -1;
  *(ini_t **)out = ini;
  return 0;
}

void turbo_free_ini(void *out) {
  if (!out) return;
  void *ptr = *(void **)out;
  if (ptr) ini_free((ini_t *)ptr);
  *(void **)out = NULL;
}

const char *turbo_ini_get(const turbo_ini_t *ini, const char *section, const char *key) {
  return ini_get((ini_t *)ini, section, key);
}

int turbo_ini_get_int(const turbo_ini_t *ini, const char *section, const char *key, int def) {
  return ini_get_int((ini_t *)ini, section, key, def);
}

bool turbo_ini_get_bool(const turbo_ini_t *ini, const char *section, const char *key, bool def) {
  return ini_get_bool((ini_t *)ini, section, key, def);
}

double turbo_ini_get_double(const turbo_ini_t *ini, const char *section, const char *key,
                            double def) {
  return ini_get_double((ini_t *)ini, section, key, def);
}

/* URI */
int turbo_parse_uri(const uint8_t *data, size_t len, void *out) {
  if (!data || !out) return -1;

  // Ensure data is null-terminated for uri_parse
  char *temp = (char *)malloc(len + 1);
  if (!temp) return -1;
  memcpy(temp, data, len);
  temp[len] = '\0';

  uri_t *uri = (uri_t *)malloc(sizeof(uri_t));
  if (!uri) {
    free(temp);
    return -1;
  }

  if (!uri_parse(temp, uri)) {
    free(temp);
    free(uri);
    return -1;
  }

  free(temp);
  *(uri_t **)out = uri;
  return 0;
}

void turbo_free_uri(void *out) {
  if (!out) return;
  void *ptr = *(void **)out;
  if (ptr) free(ptr);
  *(void **)out = NULL;
}

const char *turbo_uri_scheme(const uri_t *uri) { return uri ? uri->scheme : NULL; }
const char *turbo_uri_userinfo(const uri_t *uri) { return uri ? uri->userinfo : NULL; }
const char *turbo_uri_host(const uri_t *uri) { return uri ? uri->host : NULL; }
int turbo_uri_port(const uri_t *uri) { return uri ? uri->port : -1; }
const char *turbo_uri_path(const uri_t *uri) { return uri ? uri->path : NULL; }
const char *turbo_uri_query(const uri_t *uri) { return uri ? uri->query : NULL; }
const char *turbo_uri_fragment(const uri_t *uri) { return uri ? uri->fragment : NULL; }
turbo_uri_host_type_t turbo_uri_host_type(const uri_t *uri) {
  return uri ? (turbo_uri_host_type_t)uri->host_type : TURBO_URI_HOST_UNKNOWN;
}
bool turbo_uri_is_valid(const uri_t *uri) { return uri ? uri->valid : false; }

/* TLV */
int turbo_parse_tlv(const uint8_t *data, size_t len, void *out) {
  if (!data || !out) return -1;
  frame_t *frame = (frame_t *)calloc(1, sizeof(frame_t));
  if (!frame) return -1;
  int rc = frame_parse(data, len, frame, FRAME_PARSE_FLAG_NONE);
  if (rc != FRAME_PARSE_OK) {
    free(frame);
    return rc;
  }
  *(frame_t **)out = frame;
  return 0;
}

void turbo_free_tlv(void *out) {
  if (!out) return;
  void *ptr = *(void **)out;
  if (ptr) free(ptr);
  *(void **)out = NULL;
}

uint32_t turbo_tlv_msg_id(const turbo_tlv_frame_t *frame) {
  return frame ? ((const frame_t *)frame)->msg_id : 0;
}

uint8_t turbo_tlv_version(const turbo_tlv_frame_t *frame) {
  return frame ? ((const frame_t *)frame)->version : 0;
}

uint8_t turbo_tlv_type(const turbo_tlv_frame_t *frame) {
  return frame ? ((const frame_t *)frame)->payload_type : 0;
}

size_t turbo_tlv_payload_size(const turbo_tlv_frame_t *frame) {
  return frame ? ((const frame_t *)frame)->payload_size : 0;
}

const char *turbo_tlv_payload(const turbo_tlv_frame_t *frame) {
  return frame ? ((const frame_t *)frame)->payload : NULL;
}

uint32_t turbo_tlv_crc32(const turbo_tlv_frame_t *frame) {
  return frame ? ((const frame_t *)frame)->crc32 : 0;
}

int turbo_tlv_peek_size(const uint8_t *data, size_t len, uint32_t *out_size) {
  return (int)frame_peek_size(data, len, out_size);
}

/* LTV */
int turbo_parse_ltv(const uint8_t *data, size_t len, void *out) {
  if (!data || !out) return -1;
  ltv_message_t *ltv = (ltv_message_t *)calloc(1, sizeof(ltv_message_t));
  if (!ltv) return -1;
  int rc = ltv_parse(data, len, ltv);
  if (rc != LTV_PARSE_OK) {
    free(ltv);
    return rc;
  }
  *(ltv_message_t **)out = ltv;
  return 0;
}

void turbo_free_ltv(void *out) {
  if (!out) return;
  void *ptr = *(void **)out;
  if (ptr) free(ptr);
  *(void **)out = NULL;
}

uint8_t turbo_ltv_type(const turbo_ltv_message_t *msg) {
  return msg ? ((const ltv_message_t *)msg)->type : 0;
}

const uint8_t *turbo_ltv_value(const turbo_ltv_message_t *msg) {
  return msg ? ((const ltv_message_t *)msg)->value : NULL;
}

size_t turbo_ltv_value_len(const turbo_ltv_message_t *msg) {
  return msg ? ((const ltv_message_t *)msg)->value_size : 0;
}

size_t turbo_ltv_wire_size(size_t value_size) { return ltv_wire_size(value_size); }

size_t turbo_ltv_build(uint8_t type, const uint8_t *value, size_t value_size, uint8_t *out,
                       size_t out_len) {
  return ltv_build(type, value, value_size, out, out_len);
}

int turbo_ltv_peek_size(const uint8_t *data, size_t len, uint32_t *out_length, size_t *out_header) {
  return (int)ltv_peek_size(data, len, out_length, out_header);
}

turbo_ltv_stream_t *turbo_ltv_stream_create(size_t buffer_size) {
  return (turbo_ltv_stream_t *)ltv_stream_create(buffer_size);
}

void turbo_ltv_stream_destroy(turbo_ltv_stream_t *stream) {
  ltv_stream_destroy((ltv_stream_t *)stream);
}

int turbo_ltv_stream_feed(turbo_ltv_stream_t *stream, const uint8_t *data, size_t len, void **out) {
  if (!stream || !out) return -1;

  ltv_message_t *msg = (ltv_message_t *)calloc(1, sizeof(ltv_message_t));
  if (!msg) return -1;

  LtvParseResult rc = ltv_stream_feed((ltv_stream_t *)stream, data, len, msg);

  if (rc == LTV_PARSE_OK) {
    *(ltv_message_t **)out = msg;
    return 0; /* Complete */
  } else if (rc == LTV_PARSE_NEED_MORE) {
    free(msg);
    return 1; /* Need more */
  } else {
    free(msg);
    return -1; /* Error */
  }
}

void turbo_ltv_stream_reset(turbo_ltv_stream_t *stream) {
  ltv_stream_reset((ltv_stream_t *)stream);
}

/* Modbus */
static void turbo_modbus_pdu_from_native(turbo_modbus_pdu_t *dst, const modbus_pdu_t *src) {
  dst->function_code = src->function_code;
  dst->data = src->data;
  dst->data_size = src->data_size;
}

static void turbo_modbus_pdu_to_native(modbus_pdu_t *dst, const turbo_modbus_pdu_t *src) {
  dst->function_code = src->function_code;
  dst->data = src->data;
  dst->data_size = src->data_size;
}

static void turbo_modbus_tcp_from_native(turbo_modbus_tcp_adu_t *dst, const modbus_tcp_adu_t *src) {
  dst->transaction_id = src->transaction_id;
  dst->protocol_id = src->protocol_id;
  dst->length = src->length;
  dst->unit_id = src->unit_id;
  turbo_modbus_pdu_from_native(&dst->pdu, &src->pdu);
  dst->consumed = src->consumed;
}

static void turbo_modbus_tcp_to_native(modbus_tcp_adu_t *dst, const turbo_modbus_tcp_adu_t *src) {
  dst->transaction_id = src->transaction_id;
  dst->protocol_id = src->protocol_id;
  dst->length = src->length;
  dst->unit_id = src->unit_id;
  turbo_modbus_pdu_to_native(&dst->pdu, &src->pdu);
  dst->consumed = src->consumed;
}

static void turbo_modbus_rtu_from_native(turbo_modbus_rtu_adu_t *dst, const modbus_rtu_adu_t *src) {
  dst->address = src->address;
  turbo_modbus_pdu_from_native(&dst->pdu, &src->pdu);
  dst->crc = src->crc;
  dst->consumed = src->consumed;
}

static void turbo_modbus_rtu_to_native(modbus_rtu_adu_t *dst, const turbo_modbus_rtu_adu_t *src) {
  dst->address = src->address;
  turbo_modbus_pdu_to_native(&dst->pdu, &src->pdu);
  dst->crc = src->crc;
  dst->consumed = src->consumed;
}

int turbo_modbus_tcp_peek_size(const uint8_t *data, size_t len, size_t *out_size) {
  return (int)modbus_tcp_peek_size(data, len, out_size);
}

int turbo_modbus_tcp_read(const uint8_t *data, size_t len, turbo_modbus_tcp_adu_t *out) {
  if (!out) return (int)MODBUS_PARSE_INVALID_INPUT;

  modbus_tcp_adu_t native;
  ModbusParseResult rc = modbus_tcp_read(data, len, &native);
  if (rc == MODBUS_PARSE_OK) turbo_modbus_tcp_from_native(out, &native);
  return (int)rc;
}

size_t turbo_modbus_tcp_write(const turbo_modbus_tcp_adu_t *adu, uint8_t *out, size_t out_len) {
  if (!adu) return 0;

  modbus_tcp_adu_t native;
  turbo_modbus_tcp_to_native(&native, adu);
  return modbus_tcp_write(&native, out, out_len);
}

uint16_t turbo_modbus_rtu_crc16(const uint8_t *data, size_t len) {
  return modbus_rtu_crc16(data, len);
}

int turbo_modbus_rtu_read(const uint8_t *data, size_t len, turbo_modbus_rtu_adu_t *out) {
  if (!out) return (int)MODBUS_PARSE_INVALID_INPUT;

  modbus_rtu_adu_t native;
  ModbusParseResult rc = modbus_rtu_read(data, len, &native);
  if (rc == MODBUS_PARSE_OK) turbo_modbus_rtu_from_native(out, &native);
  return (int)rc;
}

size_t turbo_modbus_rtu_write(const turbo_modbus_rtu_adu_t *adu, uint8_t *out, size_t out_len) {
  if (!adu) return 0;

  modbus_rtu_adu_t native;
  turbo_modbus_rtu_to_native(&native, adu);
  return modbus_rtu_write(&native, out, out_len);
}

int turbo_modbus_read(turbo_modbus_transport_t transport, const uint8_t *data, size_t len,
                      turbo_modbus_adu_t *out) {
  if (!out) return (int)MODBUS_PARSE_INVALID_INPUT;

  if (transport == TURBO_MODBUS_TRANSPORT_TCP) {
    int rc = turbo_modbus_tcp_read(data, len, &out->frame.tcp);
    if (rc == TURBO_MODBUS_PARSE_OK) out->transport = TURBO_MODBUS_TRANSPORT_TCP;
    return rc;
  }

  if (transport == TURBO_MODBUS_TRANSPORT_RTU) {
    int rc = turbo_modbus_rtu_read(data, len, &out->frame.rtu);
    if (rc == TURBO_MODBUS_PARSE_OK) out->transport = TURBO_MODBUS_TRANSPORT_RTU;
    return rc;
  }

  return (int)MODBUS_PARSE_INVALID_INPUT;
}

size_t turbo_modbus_write(const turbo_modbus_adu_t *adu, uint8_t *out, size_t out_len) {
  if (!adu) return 0;

  if (adu->transport == TURBO_MODBUS_TRANSPORT_TCP)
    return turbo_modbus_tcp_write(&adu->frame.tcp, out, out_len);

  if (adu->transport == TURBO_MODBUS_TRANSPORT_RTU)
    return turbo_modbus_rtu_write(&adu->frame.rtu, out, out_len);

  return 0;
}

/* SOA */
int turbo_parse_soa(const uint8_t *data, size_t len, void *out) {
  if (!data || !out) return -1;
  soa_batch_t *batch = (soa_batch_t *)calloc(1, sizeof(soa_batch_t));
  if (!batch) return -1;
  int rc = soa_parse(data, len, batch);
  if (rc != SOA_PARSE_OK) {
    free(batch);
    return rc;
  }
  *(soa_batch_t **)out = batch;
  return 0;
}

void turbo_free_soa(void *out) {
  if (!out) return;
  void *ptr = *(void **)out;
  if (ptr) free(ptr);
  *(void **)out = NULL;
}

uint32_t turbo_soa_count(const turbo_soa_batch_t *batch) {
  return batch ? ((const soa_batch_t *)batch)->count : 0;
}

uint16_t turbo_soa_schema_id(const turbo_soa_batch_t *batch) {
  return batch ? ((const soa_batch_t *)batch)->schema_id : 0;
}

uint16_t turbo_soa_present_mask(const turbo_soa_batch_t *batch) {
  return batch ? ((const soa_batch_t *)batch)->present_mask : 0;
}

int8_t turbo_soa_get_i8(const turbo_soa_batch_t *b, int col, uint32_t row) {
  return soa_get_i8((const soa_batch_t *)b, col, row);
}

uint8_t turbo_soa_get_u8(const turbo_soa_batch_t *b, int col, uint32_t row) {
  return soa_get_u8((const soa_batch_t *)b, col, row);
}

int16_t turbo_soa_get_i16(const turbo_soa_batch_t *b, int col, uint32_t row) {
  return soa_get_i16((const soa_batch_t *)b, col, row);
}

uint16_t turbo_soa_get_u16(const turbo_soa_batch_t *b, int col, uint32_t row) {
  return soa_get_u16((const soa_batch_t *)b, col, row);
}

int32_t turbo_soa_get_i32(const turbo_soa_batch_t *b, int col, uint32_t row) {
  return soa_get_i32((const soa_batch_t *)b, col, row);
}

uint32_t turbo_soa_get_u32(const turbo_soa_batch_t *b, int col, uint32_t row) {
  return soa_get_u32((const soa_batch_t *)b, col, row);
}

int64_t turbo_soa_get_i64(const turbo_soa_batch_t *b, int col, uint32_t row) {
  return soa_get_i64((const soa_batch_t *)b, col, row);
}

uint64_t turbo_soa_get_u64(const turbo_soa_batch_t *b, int col, uint32_t row) {
  return soa_get_u64((const soa_batch_t *)b, col, row);
}

double turbo_soa_get_f64(const turbo_soa_batch_t *b, int col, uint32_t row) {
  return soa_get_f64((const soa_batch_t *)b, col, row);
}

size_t turbo_soa_wire_size(const turbo_soa_schema_t *schema, uint32_t count,
                           uint16_t present_mask) {
  return soa_wire_size((const soa_schema_t *)schema, count, present_mask);
}

size_t turbo_soa_build_header(const turbo_soa_schema_t *schema, uint32_t count,
                              uint16_t present_mask, uint8_t *out, size_t out_len) {
  return soa_build_header((const soa_schema_t *)schema, count, present_mask, out, out_len);
}

uint8_t turbo_soa_type_width(int type) { return soa_type_width((SoaColumnType)type); }

int turbo_soa_peek_header(const uint8_t *data, size_t len, uint32_t *out_count,
                          uint16_t *out_schema) {
  return (int)soa_peek_header(data, len, out_count, out_schema);
}

int turbo_soa_schema_count(const turbo_soa_schema_t *schema) {
  return schema ? ((const soa_schema_t *)schema)->column_count : 0;
}

int turbo_soa_schema_column_type(const turbo_soa_schema_t *schema, int idx) {
  const soa_schema_t *s = (const soa_schema_t *)schema;
  if (!s || idx < 0 || idx >= s->column_count) return 0; /* UNKNOWN */
  return s->columns[idx].type;
}

/* CMD Parser */
struct turbo_cmd_subcommand_s {
  char *name;
  char *info;
  CmdArgerDesc *optional_args;
  uint32_t optional_count;
  uint32_t optional_capacity;
  CmdArgerDesc *required_args;
  uint32_t required_count;
  uint32_t required_capacity;
};

struct turbo_cmd_parser_s {
  char *app_name;
  char *version;

  CmdArgerDesc *optional_args;
  uint32_t optional_count;
  uint32_t optional_capacity;

  CmdArgerDesc *required_args;
  uint32_t required_count;
  uint32_t required_capacity;

  turbo_cmd_subcommand_t *subcommands;
  uint32_t subcommand_count;
  uint32_t subcommand_capacity;
};

static void turbo_cmd_sync_environment(turbo_cmd_parser_t *parser) {
#if defined(_WIN32)
  if (!parser) return;
  for (uint32_t index = 0; index < parser->optional_count; ++index) {
    if (parser->optional_args[index].env_var)
      (void)dotenv_environment_sync_crt(parser->optional_args[index].env_var);
  }
  for (uint32_t sub_index = 0; sub_index < parser->subcommand_count; ++sub_index) {
    turbo_cmd_subcommand_t *subcommand = &parser->subcommands[sub_index];
    for (uint32_t index = 0; index < subcommand->optional_count; ++index) {
      if (subcommand->optional_args[index].env_var)
        (void)dotenv_environment_sync_crt(subcommand->optional_args[index].env_var);
    }
  }
#else
  (void)parser;
#endif
}

turbo_cmd_parser_t *turbo_cmd_create(const char *app_name, const char *version) {
  turbo_cmd_parser_t *parser = (turbo_cmd_parser_t *)calloc(1, sizeof(turbo_cmd_parser_t));
  if (!parser) return NULL;

  if (app_name) parser->app_name = strdup(app_name);
  if (version) parser->version = strdup(version);

  parser->optional_capacity = 8;
  parser->optional_args = (CmdArgerDesc *)calloc(parser->optional_capacity, sizeof(CmdArgerDesc));

  parser->required_capacity = 8;
  parser->required_args = (CmdArgerDesc *)calloc(parser->required_capacity, sizeof(CmdArgerDesc));

  parser->subcommand_capacity = 4;
  parser->subcommands =
      (turbo_cmd_subcommand_t *)calloc(parser->subcommand_capacity, sizeof(turbo_cmd_subcommand_t));

  if (!parser->optional_args || !parser->required_args || !parser->subcommands) {
    turbo_cmd_destroy(parser);
    return NULL;
  }

  return parser;
}

void turbo_cmd_destroy(turbo_cmd_parser_t *parser) {
  if (!parser) return;
  if (parser->app_name) free(parser->app_name);
  if (parser->version) free(parser->version);
  if (parser->optional_args) free(parser->optional_args);
  if (parser->required_args) free(parser->required_args);
  for (uint32_t i = 0; i < parser->subcommand_count; i++) {
    turbo_cmd_subcommand_t *sub = &parser->subcommands[i];
    if (sub->name) free(sub->name);
    if (sub->info) free(sub->info);
    if (sub->optional_args) free(sub->optional_args);
    if (sub->required_args) free(sub->required_args);
  }
  if (parser->subcommands) free(parser->subcommands);
  free(parser);
}

static void ensure_optional_capacity(turbo_cmd_parser_t *parser) {
  if (parser->optional_count >= parser->optional_capacity) {
    parser->optional_capacity *= 2;
    parser->optional_args = (CmdArgerDesc *)realloc(
        parser->optional_args, parser->optional_capacity * sizeof(CmdArgerDesc));
  }
}

void turbo_cmd_add_flag(turbo_cmd_parser_t *parser, bool *out, const char *name,
                        const char *short_name, const char *desc) {
  if (!parser) return;
  ensure_optional_capacity(parser);
  parser->optional_args[parser->optional_count++] =
      cmd_arger_desc_flag_sh((CmdArgerBool *)out, (char *)name, (char *)short_name, (char *)desc);
}

void turbo_cmd_add_string(turbo_cmd_parser_t *parser, char **out, const char *name,
                          const char *short_name, const char *desc) {
  if (!parser) return;
  ensure_optional_capacity(parser);
  parser->optional_args[parser->optional_count++] =
      cmd_arger_desc_string_sh(out, (char *)name, (char *)short_name, (char *)desc);
}

void turbo_cmd_add_integer(turbo_cmd_parser_t *parser, int64_t *out, const char *name,
                           const char *short_name, const char *desc) {
  if (!parser) return;
  ensure_optional_capacity(parser);
  parser->optional_args[parser->optional_count++] =
      cmd_arger_desc_integer_sh(out, (char *)name, (char *)short_name, (char *)desc);
}

void turbo_cmd_add_float(turbo_cmd_parser_t *parser, double *out, const char *name,
                         const char *short_name, const char *desc) {
  if (!parser) return;
  ensure_optional_capacity(parser);
  parser->optional_args[parser->optional_count++] =
      cmd_arger_desc_float_sh(out, (char *)name, (char *)short_name, (char *)desc);
}

void turbo_cmd_add_string_list(turbo_cmd_parser_t *parser, char **out_arr, uint32_t *out_count,
                               uint32_t max_count, const char *name, const char *short_name,
                               const char *desc) {
  if (!parser) return;
  ensure_optional_capacity(parser);
  parser->optional_args[parser->optional_count++] = cmd_arger_desc_string_list_sh(
      out_arr, out_count, max_count, (char *)name, (char *)short_name, (char *)desc);
}

void turbo_cmd_add_required_string(turbo_cmd_parser_t *parser, char **out, const char *name,
                                   const char *desc) {
  if (!parser) return;
  if (parser->required_count >= parser->required_capacity) {
    parser->required_capacity *= 2;
    parser->required_args = (CmdArgerDesc *)realloc(
        parser->required_args, parser->required_capacity * sizeof(CmdArgerDesc));
  }
  parser->required_args[parser->required_count++] =
      cmd_arger_desc_string(out, (char *)name, (char *)desc);
}

void turbo_cmd_add_required_integer(turbo_cmd_parser_t *parser, int64_t *out, const char *name,
                                    const char *desc) {
  if (!parser) return;
  if (parser->required_count >= parser->required_capacity) {
    parser->required_capacity *= 2;
    parser->required_args = (CmdArgerDesc *)realloc(
        parser->required_args, parser->required_capacity * sizeof(CmdArgerDesc));
  }
  parser->required_args[parser->required_count++] =
      cmd_arger_desc_integer(out, (char *)name, (char *)desc);
}

void turbo_cmd_add_enum(turbo_cmd_parser_t *parser, int64_t *out, const char *name,
                        const char *short_name, const char *desc, turbo_cmd_enum_t *choices,
                        uint32_t choices_count) {
  if (!parser) return;
  ensure_optional_capacity(parser);
  parser->optional_args[parser->optional_count++] =
      cmd_arger_desc_enum_sh(out, (char *)name, (char *)short_name, (char *)desc,
                             (CmdArgerEnumDesc *)choices, choices_count);
}

uint32_t turbo_cmd_last_index(turbo_cmd_parser_t *parser) {
  if (!parser || parser->optional_count == 0) return 0;
  return parser->optional_count - 1;
}

void turbo_cmd_set_env(turbo_cmd_parser_t *parser, uint32_t index, const char *env_var) {
  if (!parser || index >= parser->optional_count) return;
  parser->optional_args[index] = cmd_arger_with_env(parser->optional_args[index], env_var);
}

void turbo_cmd_set_group(turbo_cmd_parser_t *parser, uint32_t index, const char *group) {
  if (!parser || index >= parser->optional_count) return;
  parser->optional_args[index] = cmd_arger_with_group(parser->optional_args[index], group);
}

void turbo_cmd_set_choices(turbo_cmd_parser_t *parser, uint32_t index, const char **choices,
                           uint32_t count) {
  if (!parser || index >= parser->optional_count) return;
  parser->optional_args[index] =
      cmd_arger_with_choices(parser->optional_args[index], choices, count);
}

void turbo_cmd_set_validator(turbo_cmd_parser_t *parser, uint32_t index,
                             turbo_cmd_validator_t validator) {
  if (!parser || index >= parser->optional_count) return;
  parser->optional_args[index] =
      cmd_arger_with_validator(parser->optional_args[index], (CmdArgerValidator)validator);
}

void turbo_cmd_set_required(turbo_cmd_parser_t *parser, uint32_t index) {
  if (!parser || index >= parser->optional_count) return;
  parser->optional_args[index] = cmd_arger_required(parser->optional_args[index]);
}

/* Subcommand support */
turbo_cmd_subcommand_t *turbo_cmd_add_subcommand(turbo_cmd_parser_t *parser, const char *name,
                                                 const char *desc) {
  if (!parser) return NULL;
  if (parser->subcommand_count >= parser->subcommand_capacity) {
    parser->subcommand_capacity *= 2;
    parser->subcommands = (turbo_cmd_subcommand_t *)realloc(
        parser->subcommands, parser->subcommand_capacity * sizeof(turbo_cmd_subcommand_t));
  }
  turbo_cmd_subcommand_t *sub = &parser->subcommands[parser->subcommand_count++];
  memset(sub, 0, sizeof(*sub));
  sub->name = strdup(name);
  sub->info = desc ? strdup(desc) : NULL;
  sub->optional_capacity = 8;
  sub->optional_args = (CmdArgerDesc *)calloc(sub->optional_capacity, sizeof(CmdArgerDesc));
  sub->required_capacity = 4;
  sub->required_args = (CmdArgerDesc *)calloc(sub->required_capacity, sizeof(CmdArgerDesc));
  return sub;
}

static void ensure_sub_optional_capacity(turbo_cmd_subcommand_t *sub) {
  if (sub->optional_count >= sub->optional_capacity) {
    sub->optional_capacity *= 2;
    sub->optional_args =
        (CmdArgerDesc *)realloc(sub->optional_args, sub->optional_capacity * sizeof(CmdArgerDesc));
  }
}

void turbo_cmd_sub_add_flag(turbo_cmd_subcommand_t *sub, bool *out, const char *name,
                            const char *short_name, const char *desc) {
  if (!sub) return;
  ensure_sub_optional_capacity(sub);
  sub->optional_args[sub->optional_count++] =
      cmd_arger_desc_flag_sh((CmdArgerBool *)out, (char *)name, (char *)short_name, (char *)desc);
}

void turbo_cmd_sub_add_string(turbo_cmd_subcommand_t *sub, char **out, const char *name,
                              const char *short_name, const char *desc) {
  if (!sub) return;
  ensure_sub_optional_capacity(sub);
  sub->optional_args[sub->optional_count++] =
      cmd_arger_desc_string_sh(out, (char *)name, (char *)short_name, (char *)desc);
}

void turbo_cmd_sub_add_integer(turbo_cmd_subcommand_t *sub, int64_t *out, const char *name,
                               const char *short_name, const char *desc) {
  if (!sub) return;
  ensure_sub_optional_capacity(sub);
  sub->optional_args[sub->optional_count++] =
      cmd_arger_desc_integer_sh(out, (char *)name, (char *)short_name, (char *)desc);
}

void turbo_cmd_sub_add_required_string(turbo_cmd_subcommand_t *sub, char **out, const char *name,
                                       const char *desc) {
  if (!sub) return;
  if (sub->required_count >= sub->required_capacity) {
    sub->required_capacity *= 2;
    sub->required_args =
        (CmdArgerDesc *)realloc(sub->required_args, sub->required_capacity * sizeof(CmdArgerDesc));
  }
  sub->required_args[sub->required_count++] =
      cmd_arger_desc_string(out, (char *)name, (char *)desc);
}

/* TOON */
int turbo_parse_toon(const uint8_t *data, size_t len, void *out) {
  if (!data || !out) return -1;
  toonObject *doc = TOONc_parseStringLen((const char *)data, len);
  if (!doc) return -1;
  *(toonObject **)out = doc;
  return 0;
}

void turbo_free_toon(void *out) {
  if (!out) return;
  void *ptr = *(void **)out;
  if (ptr) TOONc_free((toonObject *)ptr);
  *(void **)out = NULL;
}

turbo_toon_type_t turbo_toon_type(const turbo_toon_node_t *node) {
  if (!node) return TURBO_TOON_NULL;
  return (turbo_toon_type_t)node->kvtype;
}

bool turbo_toon_is_null(const turbo_toon_node_t *node) { return node && node->kvtype == KV_NULL; }

bool turbo_toon_bool(const turbo_toon_node_t *node) {
  return node && node->kvtype == KV_BOOL ? (bool)node->boolean : false;
}

double turbo_toon_number(const turbo_toon_node_t *node) {
  if (!node) return 0.0;
  if (node->kvtype == KV_DOUBLE) return node->d;
  if (node->kvtype == KV_INT) return (double)node->i;
  return 0.0;
}

int turbo_toon_int(const turbo_toon_node_t *node) {
  if (!node) return 0;
  if (node->kvtype == KV_INT) return node->i;
  if (node->kvtype == KV_DOUBLE) return (int)node->d;
  return 0;
}

const char *turbo_toon_string(const turbo_toon_node_t *node) {
  return node && node->kvtype == KV_STRING ? node->str.ptr : NULL;
}

size_t turbo_toon_string_len(const turbo_toon_node_t *node) {
  return node && node->kvtype == KV_STRING ? node->str.len : 0;
}

turbo_toon_node_t *turbo_toon_get(turbo_toon_node_t *root, const char *path) {
  return TOONc_get(root, path);
}

size_t turbo_toon_array_size(const turbo_toon_node_t *arr) {
  return TOONc_getArrayLength((toonObject *)arr);
}

turbo_toon_node_t *turbo_toon_array_get(const turbo_toon_node_t *arr, size_t index) {
  return TOONc_getArrayItem((toonObject *)arr, index);
}

char *turbo_toon_serialize(const turbo_toon_node_t *node, size_t *out_len) {
  return TOONc_serialize((const toonObject *)node, out_len);
}

void turbo_toon_serialize_free(char *str) { TOONc_serializeFree(str); }

char *turbo_toon_serialize_json(const turbo_toon_node_t *node, size_t *out_len) {
  return TOONc_toJSONString((const toonObject *)node, out_len);
}

void turbo_toon_serialize_json_free(char *str) { TOONc_serializeFree(str); }

turbo_toon_node_t *turbo_toon_from_json(const char *json, size_t len) {
  return TOONc_fromJSONString(json, len);
}

int turbo_toon_from_json_doc(const turbo_json_doc_t *json,
                             turbo_toon_node_t **out) {
  return toon_json_from_value(json, out);
}

int turbo_toon_to_json_doc(const turbo_toon_node_t *toon,
                           turbo_json_doc_t **out) {
  return toon_json_to_value(toon, out);
}

void turbo_cmd_parse(turbo_cmd_parser_t *parser, int argc, char **argv, bool colors) {
  if (!parser) return;

  turbo_cmd_sync_environment(parser);

  char app_ver[256];
  if (parser->app_name && parser->version) {
    fmt(app_ver, sizeof(app_ver), "{} {}", parser->app_name, parser->version);
  } else if (parser->app_name) {
    fmt(app_ver, sizeof(app_ver), "{}", parser->app_name);
  } else {
    fmt(app_ver, sizeof(app_ver), "Application");
  }

  cmd_arger_parse(parser->optional_args, parser->optional_count, parser->required_args,
                  parser->required_count, argc, argv, app_ver, (CmdArgerBool)colors);
}

int turbo_cmd_parse_subcommand(turbo_cmd_parser_t *parser, int argc, char **argv, bool colors) {
  if (!parser || parser->subcommand_count == 0) return -1;

  turbo_cmd_sync_environment(parser);

  char app_ver[256];
  if (parser->app_name && parser->version) {
    fmt(app_ver, sizeof(app_ver), "{} {}", parser->app_name, parser->version);
  } else if (parser->app_name) {
    fmt(app_ver, sizeof(app_ver), "{}", parser->app_name);
  } else {
    fmt(app_ver, sizeof(app_ver), "Application");
  }

  CmdArgerSubCommand *subs =
      (CmdArgerSubCommand *)calloc(parser->subcommand_count, sizeof(CmdArgerSubCommand));
  for (uint32_t i = 0; i < parser->subcommand_count; i++) {
    turbo_cmd_subcommand_t *src = &parser->subcommands[i];
    subs[i].name = src->name;
    subs[i].info = src->info;
    subs[i].optional_args = src->optional_args;
    subs[i].optional_args_count = src->optional_count;
    subs[i].required_args = src->required_args;
    subs[i].required_args_count = src->required_count;
  }

  int selected = -1;
  cmd_arger_parse_subcommand(parser->optional_args, parser->optional_count, subs,
                             parser->subcommand_count, &selected, argc, argv, app_ver,
                             (CmdArgerBool)colors);
  free(subs);
  return selected;
}

void turbo_cmd_show_help(turbo_cmd_parser_t *parser, bool colors) {
  if (!parser) return;

  char app_ver[256];
  if (parser->app_name && parser->version) {
    fmt(app_ver, sizeof(app_ver), "{} {}", parser->app_name, parser->version);
  } else if (parser->app_name) {
    fmt(app_ver, sizeof(app_ver), "{}", parser->app_name);
  } else {
    fmt(app_ver, sizeof(app_ver), "Application");
  }

  cmd_arger_show_help_and_exit(parser->optional_args, parser->optional_count, parser->required_args,
                               parser->required_count, NULL, app_ver, (CmdArgerBool)colors);
}

/* DotEnv */
int turbo_dotenv_load(const char *path, bool overwrite) { return dotenv_load(path, overwrite); }

int turbo_dotenv_load_default(bool overwrite) { return dotenv_load_default(overwrite); }

/* TOML */
int turbo_parse_toml(const uint8_t *data, size_t len, void *out) {
  if (!data || !out) return -1;
  char errbuf[200];
  char *temp = (char *)malloc(len + 1);
  if (!temp) return -1;
  memcpy(temp, data, len);
  temp[len] = '\0';

  toml_table_t *table = toml_parse(temp, errbuf, sizeof(errbuf));
  free(temp);

  if (!table) return -1;
  *(toml_table_t **)out = table;
  return 0;
}

void turbo_free_toml(void *out) {
  if (!out) return;
  void *ptr = *(void **)out;
  if (ptr) toml_free((toml_table_t *)ptr);
  *(void **)out = NULL;
}

int turbo_toml_len(const turbo_toml_t *table) {
  return toml_table_len((const toml_table_t *)table);
}

const char *turbo_toml_key(const turbo_toml_t *table, int index, int *keylen) {
  return toml_table_key((const toml_table_t *)table, index, keylen);
}

turbo_toml_value_t turbo_toml_string(const turbo_toml_t *table, const char *key) {
  toml_value_t v = toml_table_string((const toml_table_t *)table, key);
  turbo_toml_value_t ret;
  memcpy(&ret, &v, sizeof(v));
  return ret;
}

turbo_toml_value_t turbo_toml_bool(const turbo_toml_t *table, const char *key) {
  toml_value_t v = toml_table_bool((const toml_table_t *)table, key);
  turbo_toml_value_t ret;
  memcpy(&ret, &v, sizeof(v));
  return ret;
}

turbo_toml_value_t turbo_toml_int(const turbo_toml_t *table, const char *key) {
  toml_value_t v = toml_table_int((const toml_table_t *)table, key);
  turbo_toml_value_t ret;
  memcpy(&ret, &v, sizeof(v));
  return ret;
}

turbo_toml_value_t turbo_toml_double(const turbo_toml_t *table, const char *key) {
  toml_value_t v = toml_table_double((const toml_table_t *)table, key);
  turbo_toml_value_t ret;
  memcpy(&ret, &v, sizeof(v));
  return ret;
}

turbo_toml_value_t turbo_toml_timestamp(const turbo_toml_t *table, const char *key) {
  toml_value_t v = toml_table_timestamp((const toml_table_t *)table, key);
  turbo_toml_value_t ret;
  memcpy(&ret, &v, sizeof(v));
  return ret;
}

turbo_toml_array_t *turbo_toml_array(const turbo_toml_t *table, const char *key) {
  return (turbo_toml_array_t *)toml_table_array((const toml_table_t *)table, key);
}

turbo_toml_t *turbo_toml_table(const turbo_toml_t *table, const char *key) {
  return (turbo_toml_t *)toml_table_table((const toml_table_t *)table, key);
}

int turbo_toml_array_len(const turbo_toml_array_t *array) {
  return toml_array_len((const toml_array_t *)array);
}

turbo_toml_value_t turbo_toml_array_string(const turbo_toml_array_t *array, int idx) {
  toml_value_t v = toml_array_string((const toml_array_t *)array, idx);
  turbo_toml_value_t ret;
  memcpy(&ret, &v, sizeof(v));
  return ret;
}

turbo_toml_value_t turbo_toml_array_bool(const turbo_toml_array_t *array, int idx) {
  toml_value_t v = toml_array_bool((const toml_array_t *)array, idx);
  turbo_toml_value_t ret;
  memcpy(&ret, &v, sizeof(v));
  return ret;
}

turbo_toml_value_t turbo_toml_array_int(const turbo_toml_array_t *array, int idx) {
  toml_value_t v = toml_array_int((const toml_array_t *)array, idx);
  turbo_toml_value_t ret;
  memcpy(&ret, &v, sizeof(v));
  return ret;
}

turbo_toml_value_t turbo_toml_array_double(const turbo_toml_array_t *array, int idx) {
  toml_value_t v = toml_array_double((const toml_array_t *)array, idx);
  turbo_toml_value_t ret;
  memcpy(&ret, &v, sizeof(v));
  return ret;
}

turbo_toml_value_t turbo_toml_array_timestamp(const turbo_toml_array_t *array, int idx) {
  toml_value_t v = toml_array_timestamp((const toml_array_t *)array, idx);
  turbo_toml_value_t ret;
  memcpy(&ret, &v, sizeof(v));
  return ret;
}

turbo_toml_array_t *turbo_toml_array_array(const turbo_toml_array_t *array, int idx) {
  return (turbo_toml_array_t *)toml_array_array((const toml_array_t *)array, idx);
}

turbo_toml_t *turbo_toml_array_table(const turbo_toml_array_t *array, int idx) {
  return (turbo_toml_t *)toml_array_table((const toml_array_t *)array, idx);
}

/* Datetime */
int turbo_parse_datetime(const char *str, size_t len, turbo_datetime_t *out) {
  return datetime_parse(str, len, (datetime_t *)out);
}

time_t turbo_datetime_to_time(const turbo_datetime_t *dt) {
  return datetime_to_time((const datetime_t *)dt);
}

int turbo_datetime_format_rfc822(time_t t, char *buf, size_t buf_len) {
  return datetime_format_rfc822(t, buf, buf_len);
}
