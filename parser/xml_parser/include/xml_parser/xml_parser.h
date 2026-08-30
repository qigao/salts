#ifndef TURBO_XML_PARSER_H
#define TURBO_XML_PARSER_H

#include <stddef.h>
#include <stdint.h>
#include <query_vm.h>

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
    TURBO_XML_ATTRIBUTE,
    TURBO_XML_DOCUMENT,
    TURBO_XML_NAMESPACE,
    TURBO_XML_XML_HEADER,
    TURBO_XML_DTD,
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

/** Owned array of borrowed nodes. Destroy the list before its document. */
typedef struct turbo_xml_node_list {
    void *items;
    size_t size;
} turbo_xml_node_list;

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

/**
 * Create an owning mutable document with one root element.
 * `out` must be zero-initialized. The document is single-owner and must not be
 * copied. Mutation and destruction require all readers to be quiescent.
 */
turbo_xml_status turbo_xml_document_create(turbo_xml_document *out,
                                           const char *root_name);

/** Append a new element. Existing handles remain valid; borrowed views may not. */
turbo_xml_status turbo_xml_node_add_element(turbo_xml_node parent,
                                            const char *name,
                                            turbo_xml_node *out);

/** Append escaped text content. Existing borrowed string views may be invalidated. */
turbo_xml_status turbo_xml_node_set_text(turbo_xml_node node,
                                         const char *text);

/** Return a malloc-owned complete document string. Free with turbo_xml_owned_string_free(). */
char *turbo_xml_document_serialize(const turbo_xml_document *document,
                                   size_t *out_size);
void turbo_xml_owned_string_free(char *string);

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

/**
 * Serialize every child of one element as a compact UTF-8 XML fragment.
 * Text, element, comment, and processing-instruction order is preserved,
 * including whitespace-only text hidden by the semantic child iterator.
 *
 * Pass NULL/0 for output/output_capacity to measure. On success out_size is
 * the byte count excluding NUL. A provided output requires out_size + 1 bytes.
 * max_bytes is a mandatory hard bound. The facade owns all temporary storage.
 */
turbo_xml_status turbo_xml_serialize_children(
    turbo_xml_node node, char *output, size_t output_capacity,
    size_t max_bytes, size_t *out_size);

turbo_xml_location turbo_xml_attribute_location(turbo_xml_attribute attribute);
turbo_xml_string_view turbo_xml_attribute_qualified_name(
    turbo_xml_attribute attribute);
turbo_xml_string_view turbo_xml_attribute_local_name(
    turbo_xml_attribute attribute);
turbo_xml_string_view turbo_xml_attribute_namespace_uri(
    turbo_xml_attribute attribute);
turbo_xml_string_view turbo_xml_attribute_value(turbo_xml_attribute attribute);

/** Return the first borrowed node matching the legacy element-query syntax. */
turbo_xml_node turbo_xml_node_find(turbo_xml_node root, const char *query);

/**
 * Populate a zero-initialized owned list with borrowed matching nodes.
 * Destroy the list before destroying or mutating its document.
 */
turbo_xml_status turbo_xml_node_find_all(turbo_xml_node root, const char *query,
                                         turbo_xml_node_list *out);
/** Return malloc-owned aggregate node text. Free with turbo_xml_owned_string_free(). */
char *turbo_xml_node_text_dup(turbo_xml_node node);

/**
 * Execute XPath and populate a zero-initialized owned list of borrowed nodes.
 * XPath evaluation currently requires process-wide external serialization;
 * concurrent XPath calls are unsupported by the private engine.
 */
qvm_status_t turbo_xml_document_xpath_query(const turbo_xml_document *document,
                                            const char *xpath,
                                            turbo_xml_node_list *out,
                                            const qvm_limits_t *limits,
                                            qvm_diagnostic_t *diagnostic);
size_t turbo_xml_node_list_size(const turbo_xml_node_list *list);
turbo_xml_node turbo_xml_node_list_at(const turbo_xml_node_list *list,
                                      size_t index);
void turbo_xml_node_list_destroy(turbo_xml_node_list *list);

turbo_xml_string_view turbo_xml_node_display_name(turbo_xml_node node);
turbo_xml_string_view turbo_xml_node_text_view(turbo_xml_node node);
char *turbo_xml_node_serialize(turbo_xml_node node, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_XML_PARSER_H */
