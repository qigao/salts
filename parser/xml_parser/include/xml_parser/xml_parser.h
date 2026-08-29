#ifndef TURBO_XML_PARSER_H
#define TURBO_XML_PARSER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TURBO_XML_DIAGNOSTIC_CAPACITY 256u

typedef enum turbo_xml_status {
    TURBO_XML_OK = 0,
    TURBO_XML_INVALID_ARGUMENT,
    TURBO_XML_LIMIT_EXCEEDED,
    TURBO_XML_ALLOCATION_FAILED,
    TURBO_XML_EMBEDDED_NUL,
    TURBO_XML_UNSUPPORTED,
    TURBO_XML_MALFORMED
} turbo_xml_status;

typedef enum turbo_xml_node_kind {
    TURBO_XML_ELEMENT = 0,
    TURBO_XML_TEXT,
    TURBO_XML_COMMENT,
    TURBO_XML_PROCESSING_INSTRUCTION,
    TURBO_XML_INVALID_NODE
} turbo_xml_node_kind;

typedef struct turbo_xml_location {
    /** Zero-based byte offset in the original UTF-8 input. */
    size_t byte_offset;
    /** One-based source line. */
    uint32_t line;
    /** One-based byte column. */
    uint32_t column;
} turbo_xml_location;

typedef struct turbo_xml_string_view {
    const char *data;
    size_t size;
} turbo_xml_string_view;

typedef struct turbo_xml_limits {
    size_t max_input_bytes;
    /** All retained parser nodes, including declarations and DTD nodes. */
    size_t max_nodes;
    /** Element, namespace, and XML declaration attributes. */
    size_t max_attributes;
    size_t max_depth;
    /** Bytes retained by names and values in the private parser DOM. */
    size_t max_retained_string_bytes;
} turbo_xml_limits;

typedef struct turbo_xml_diagnostic {
    turbo_xml_status status;
    turbo_xml_location location;
    char message[TURBO_XML_DIAGNOSTIC_CAPACITY];
} turbo_xml_diagnostic;

typedef struct turbo_xml_document {
    void *impl;
} turbo_xml_document;

/**
 * Borrowed immutable node handle. It and every returned string view are valid
 * only while the owning document remains alive.
 */
typedef struct turbo_xml_node {
    const void *impl;
} turbo_xml_node;

/** Borrowed immutable attribute handle with the same document lifetime. */
typedef struct turbo_xml_attribute {
    const void *impl;
} turbo_xml_attribute;

/** Return the bounded defaults used when parse receives a NULL limits pointer. */
turbo_xml_limits turbo_xml_default_limits(void);

/**
 * Parse exactly input_size bytes and atomically publish an owning document.
 * `out` must be zero-initialized. Input bytes are copied. Failure leaves an
 * empty output and writes the first caller-owned diagnostic when provided.
 */
turbo_xml_status turbo_xml_parse(turbo_xml_document *out,
                                 const char *input,
                                 size_t input_size,
                                 const turbo_xml_limits *limits,
                                 turbo_xml_diagnostic *diagnostic);

/** Destroy a document after all borrowed nodes/views are quiescent. */
void turbo_xml_document_destroy(turbo_xml_document *document);

turbo_xml_node turbo_xml_document_root(const turbo_xml_document *document);
turbo_xml_node_kind turbo_xml_node_type(turbo_xml_node node);
turbo_xml_location turbo_xml_node_location(turbo_xml_node node);
turbo_xml_string_view turbo_xml_node_qualified_name(turbo_xml_node node);
turbo_xml_string_view turbo_xml_node_local_name(turbo_xml_node node);
turbo_xml_string_view turbo_xml_node_namespace_uri(turbo_xml_node node);
turbo_xml_string_view turbo_xml_node_value(turbo_xml_node node);
size_t turbo_xml_node_child_count(turbo_xml_node node);
turbo_xml_node turbo_xml_node_child_at(turbo_xml_node node, size_t index);
size_t turbo_xml_node_attribute_count(turbo_xml_node node);
turbo_xml_attribute turbo_xml_node_attribute_at(turbo_xml_node node,
                                                 size_t index);

turbo_xml_location turbo_xml_attribute_location(turbo_xml_attribute attribute);
turbo_xml_string_view turbo_xml_attribute_qualified_name(
    turbo_xml_attribute attribute);
turbo_xml_string_view turbo_xml_attribute_local_name(
    turbo_xml_attribute attribute);
turbo_xml_string_view turbo_xml_attribute_namespace_uri(
    turbo_xml_attribute attribute);
turbo_xml_string_view turbo_xml_attribute_value(turbo_xml_attribute attribute);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_XML_PARSER_H */
