#include <xml_parser/xml_parser.h>

#include "core/cxdefs.h"
#include "core/cxlist.h"
#include "core/cxstr.h"
#include "core/cxtable.h"
#include "xml/cxparser.h"
#include "xml/cxprinter.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TURBO_XML_DEFAULT_MAX_INPUT_BYTES (16u * 1024u * 1024u)
#define TURBO_XML_DEFAULT_MAX_NODES 1048576u
#define TURBO_XML_DEFAULT_MAX_ATTRIBUTES 1048576u
#define TURBO_XML_DEFAULT_MAX_DEPTH 256u
#define TURBO_XML_DEFAULT_MAX_RETAINED_STRING_BYTES (32u * 1024u * 1024u)

typedef struct turbo_xml_document_impl {
    char *input;
    size_t input_size;
    cxml_root_node *root;
} turbo_xml_document_impl;

typedef struct turbo_xml_counts {
    size_t nodes;
    size_t attributes;
    size_t retained_strings;
} turbo_xml_counts;

static turbo_xml_string_view empty_view(void) {
    const turbo_xml_string_view view = {NULL, 0u};
    return view;
}

static turbo_xml_location empty_location(void) {
    const turbo_xml_location location = {0u, 0u, 0u};
    return location;
}

static turbo_xml_location convert_location(cxml_source_location source) {
    const turbo_xml_location location = {
        source.byte_offset, (uint32_t)source.line, (uint32_t)source.column};
    return location;
}

static void clear_diagnostic(turbo_xml_diagnostic *diagnostic) {
    if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
}

static turbo_xml_status fail(turbo_xml_diagnostic *diagnostic,
                             turbo_xml_status status,
                             turbo_xml_location location,
                             const char *message) {
    if (diagnostic != NULL) {
        diagnostic->status = status;
        diagnostic->location = location;
        if (message != NULL) {
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "%s", message);
        }
    }
    return status;
}

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

static turbo_xml_location location_at(const char *input, const char *position) {
    turbo_xml_location location = {0u, 1u, 1u};
    const char *cursor;

    location.byte_offset = (size_t)(position - input);
    for (cursor = input; cursor < position; ++cursor) {
        if (*cursor == '\n') {
            ++location.line;
            location.column = 1u;
        } else {
            ++location.column;
        }
    }
    return location;
}

static const char *find_sequence(const char *cursor, const char *end,
                                 const char *sequence, size_t sequence_size) {
    while ((size_t)(end - cursor) >= sequence_size) {
        if (memcmp(cursor, sequence, sequence_size) == 0) return cursor;
        ++cursor;
    }
    return NULL;
}

static turbo_xml_status preflight_depth(const char *input, size_t input_size,
                                        size_t max_depth,
                                        turbo_xml_diagnostic *diagnostic) {
    const char *cursor = input;
    const char *const end = input + input_size;
    size_t depth = 0u;

    while (cursor < end) {
        const char *tag_end;
        bool closing = false;
        bool self_closing = false;
        char quote = '\0';

        if (*cursor != '<') {
            ++cursor;
            continue;
        }
        if ((size_t)(end - cursor) >= 4u &&
            memcmp(cursor, "<!--", 4u) == 0) {
            tag_end = find_sequence(cursor + 4, end, "-->", 3u);
            if (tag_end == NULL) return TURBO_XML_OK;
            cursor = tag_end + 3;
            continue;
        }
        if ((size_t)(end - cursor) >= 9u &&
            memcmp(cursor, "<![CDATA[", 9u) == 0) {
            tag_end = find_sequence(cursor + 9, end, "]]>", 3u);
            if (tag_end == NULL) return TURBO_XML_OK;
            cursor = tag_end + 3;
            continue;
        }
        if ((size_t)(end - cursor) >= 9u &&
            memcmp(cursor, "<!DOCTYPE", 9u) == 0) {
            return fail(diagnostic, TURBO_XML_UNSUPPORTED,
                        location_at(input, cursor),
                        "DTD declarations are not supported");
        }
        if ((size_t)(end - cursor) >= 2u && cursor[1] == '?') {
            tag_end = find_sequence(cursor + 2, end, "?>", 2u);
            if (tag_end == NULL) return TURBO_XML_OK;
            cursor = tag_end + 2;
            continue;
        }
        closing = (size_t)(end - cursor) >= 2u && cursor[1] == '/';
        tag_end = cursor + (closing ? 2 : 1);
        while (tag_end < end) {
            if (quote != '\0') {
                if (*tag_end == quote) quote = '\0';
            } else if (*tag_end == '\'' || *tag_end == '"') {
                quote = *tag_end;
            } else if (*tag_end == '>') {
                break;
            }
            ++tag_end;
        }
        if (tag_end == end) return TURBO_XML_OK;
        if (closing) {
            if (depth > 0u) --depth;
        } else {
            const char *before_end = tag_end;
            while (before_end > cursor &&
                   isspace((unsigned char)before_end[-1])) {
                --before_end;
            }
            self_closing = before_end > cursor && before_end[-1] == '/';
            if (!self_closing) {
                ++depth;
                if (depth > max_depth) {
                    return fail(diagnostic, TURBO_XML_LIMIT_EXCEEDED,
                                location_at(input, cursor),
                                "XML nesting depth exceeds max_depth");
                }
            }
        }
        cursor = tag_end + 1;
    }
    return TURBO_XML_OK;
}

static bool is_whitespace_text(const void *node) {
    const cxml_text_node *text;
    const char *value;
    unsigned int index;
    unsigned int size;

    if (node == NULL || _cxml_node_type(node) != CXML_TEXT_NODE) return false;
    text = (const cxml_text_node *)node;
    value = cxml_string_as_raw((cxml_string *)&text->value);
    size = cxml_string_len((cxml_string *)&text->value);
    for (index = 0u; index < size; ++index) {
        if (!isspace((unsigned char)value[index])) return false;
    }
    return true;
}

static bool count_string(size_t *total, const cxml_string *string) {
    return checked_add(*total,
                       (size_t)cxml_string_len((cxml_string *)string), total);
}

static bool collect_counts(const cxml_elem_node *element, size_t depth,
                           const turbo_xml_limits *limits,
                           turbo_xml_counts *counts,
                           turbo_xml_location *limit_location) {
    struct _cxml_list__node *cursor;

    if (element == NULL || depth > limits->max_depth ||
        !checked_add(counts->nodes, 1u, &counts->nodes) ||
        counts->nodes > limits->max_nodes ||
        !count_string(&counts->retained_strings, &element->name.qname)) {
        if (element != NULL) *limit_location = convert_location(element->source);
        return false;
    }

    if (element->namespaces != NULL) {
        for (cursor = element->namespaces->head; cursor != NULL;
             cursor = cursor->next) {
            const cxml_ns_node *ns = (const cxml_ns_node *)cursor->item;
            if (!checked_add(counts->attributes, 1u, &counts->attributes) ||
                !count_string(&counts->retained_strings, &ns->prefix) ||
                !count_string(&counts->retained_strings, &ns->uri)) {
                *limit_location = convert_location(ns->source);
                return false;
            }
        }
    }
    if (element->attributes != NULL) {
        for (cursor = element->attributes->keys.head; cursor != NULL;
             cursor = cursor->next) {
            const char *key = (const char *)cursor->item;
            const cxml_attr_node *attribute =
                (const cxml_attr_node *)cxml_table_get(element->attributes, key);
            if (!checked_add(counts->attributes, 1u, &counts->attributes) ||
                !count_string(&counts->retained_strings,
                              &attribute->name.qname) ||
                !count_string(&counts->retained_strings, &attribute->value)) {
                *limit_location = convert_location(attribute->source);
                return false;
            }
        }
    }
    if (counts->attributes > limits->max_attributes ||
        counts->retained_strings > limits->max_retained_string_bytes) {
        *limit_location = convert_location(element->source);
        return false;
    }

    for (cursor = element->children.head; cursor != NULL;
         cursor = cursor->next) {
        const void *child = cursor->item;
        const _cxml_node_t type = _cxml_node_type(child);
        if (type == CXML_ELEM_NODE) {
            if (!collect_counts((const cxml_elem_node *)child, depth + 1u,
                                limits, counts, limit_location)) {
                return false;
            }
        } else if (type == CXML_TEXT_NODE) {
            const cxml_text_node *text = (const cxml_text_node *)child;
            if (!checked_add(counts->nodes, 1u, &counts->nodes) ||
                !count_string(&counts->retained_strings, &text->value)) {
                *limit_location = convert_location(text->source);
                return false;
            }
        } else if (type == CXML_COMM_NODE) {
            const cxml_comm_node *comment = (const cxml_comm_node *)child;
            if (!checked_add(counts->nodes, 1u, &counts->nodes) ||
                !count_string(&counts->retained_strings, &comment->value)) {
                *limit_location = convert_location(comment->source);
                return false;
            }
        } else if (type == CXML_PI_NODE) {
            const cxml_pi_node *pi = (const cxml_pi_node *)child;
            if (!checked_add(counts->nodes, 1u, &counts->nodes) ||
                !count_string(&counts->retained_strings, &pi->target) ||
                !count_string(&counts->retained_strings, &pi->value)) {
                *limit_location = convert_location(pi->source);
                return false;
            }
        }
        if (counts->nodes > limits->max_nodes ||
            counts->retained_strings > limits->max_retained_string_bytes) {
            *limit_location = convert_location(element->source);
            return false;
        }
    }
    return true;
}

turbo_xml_limits turbo_xml_default_limits(void) {
    const turbo_xml_limits limits = {
        TURBO_XML_DEFAULT_MAX_INPUT_BYTES,
        TURBO_XML_DEFAULT_MAX_NODES,
        TURBO_XML_DEFAULT_MAX_ATTRIBUTES,
        TURBO_XML_DEFAULT_MAX_DEPTH,
        TURBO_XML_DEFAULT_MAX_RETAINED_STRING_BYTES};
    return limits;
}

turbo_xml_status turbo_xml_parse(turbo_xml_document *out,
                                 const char *input,
                                 size_t input_size,
                                 const turbo_xml_limits *limits_or_null,
                                 turbo_xml_diagnostic *diagnostic) {
    turbo_xml_limits limits;
    turbo_xml_document_impl *impl = NULL;
    turbo_xml_counts counts = {0u, 0u, 0u};
    turbo_xml_location limit_location = {0u, 1u, 1u};
    cxml_parse_error parse_error;
    cxml_parse_limits parse_limits;
    turbo_xml_status status;
    const char *nul;

    clear_diagnostic(diagnostic);
    if (out == NULL || out->impl != NULL || input == NULL || input_size == 0u) {
        return fail(diagnostic, TURBO_XML_INVALID_ARGUMENT, empty_location(),
                    "out must be empty and input must be nonempty");
    }
    limits = limits_or_null != NULL ? *limits_or_null
                                    : turbo_xml_default_limits();
    if (limits.max_input_bytes == 0u || limits.max_nodes == 0u ||
        limits.max_attributes == 0u || limits.max_depth == 0u ||
        limits.max_retained_string_bytes == 0u) {
        return fail(diagnostic, TURBO_XML_INVALID_ARGUMENT, empty_location(),
                    "all XML limits must be positive");
    }
    if (input_size > limits.max_input_bytes) {
        return fail(diagnostic, TURBO_XML_LIMIT_EXCEEDED,
                    location_at(input, input + limits.max_input_bytes),
                    "XML input exceeds max_input_bytes");
    }
    nul = (const char *)memchr(input, '\0', input_size);
    if (nul != NULL) {
        return fail(diagnostic, TURBO_XML_EMBEDDED_NUL,
                    location_at(input, nul),
                    "embedded NUL is not valid XML input");
    }
    status = preflight_depth(input, input_size, limits.max_depth, diagnostic);
    if (status != TURBO_XML_OK) return status;

    impl = (turbo_xml_document_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) {
        return fail(diagnostic, TURBO_XML_ALLOCATION_FAILED, empty_location(),
                    "unable to allocate XML document");
    }
    if (input_size == SIZE_MAX) {
        free(impl);
        return fail(diagnostic, TURBO_XML_LIMIT_EXCEEDED, empty_location(),
                    "XML input size cannot be terminated safely");
    }
    impl->input = (char *)malloc(input_size + 1u);
    if (impl->input == NULL) {
        free(impl);
        return fail(diagnostic, TURBO_XML_ALLOCATION_FAILED, empty_location(),
                    "unable to copy XML input");
    }
    memcpy(impl->input, input, input_size);
    impl->input[input_size] = '\0';
    impl->input_size = input_size;
    memset(&parse_error, 0, sizeof(parse_error));
    parse_limits.max_nodes = limits.max_nodes;
    parse_limits.max_attributes = limits.max_attributes;
    parse_limits.max_retained_string_bytes =
        limits.max_retained_string_bytes;
    impl->root = cxml_parse_xml_limited(impl->input, &parse_limits,
                                        &parse_error);
    if (impl->root == NULL) {
        turbo_xml_location location = convert_location(parse_error.source);
        const char *message = parse_error.message[0] != '\0'
                                  ? parse_error.message
                                  : "malformed XML";
        const turbo_xml_status parse_status =
            parse_error.kind == CXML_PARSE_ERROR_ALLOCATION
                ? TURBO_XML_ALLOCATION_FAILED
                : parse_error.kind == CXML_PARSE_ERROR_LIMIT
                      ? TURBO_XML_LIMIT_EXCEEDED
                      : TURBO_XML_MALFORMED;
        free(impl->input);
        free(impl);
        return fail(diagnostic, parse_status, location, message);
    }
    if (!impl->root->is_well_formed || impl->root->root_element == NULL) {
        cxml_root_node_free(impl->root);
        free(impl->input);
        free(impl);
        return fail(diagnostic, TURBO_XML_MALFORMED, limit_location,
                    "XML document is not well formed");
    }
    if (!collect_counts(impl->root->root_element, 1u, &limits, &counts,
                        &limit_location)) {
        cxml_root_node_free(impl->root);
        free(impl->input);
        free(impl);
        return fail(diagnostic, TURBO_XML_LIMIT_EXCEEDED, limit_location,
                    "XML document exceeds a configured structural limit");
    }
    out->impl = impl;
    return TURBO_XML_OK;
}

void turbo_xml_document_destroy(turbo_xml_document *document) {
    turbo_xml_document_impl *impl;
    if (document == NULL || document->impl == NULL) return;
    impl = (turbo_xml_document_impl *)document->impl;
    cxml_root_node_free(impl->root);
    free(impl->input);
    free(impl);
    document->impl = NULL;
}

turbo_xml_node turbo_xml_document_root(const turbo_xml_document *document) {
    turbo_xml_node node = {NULL};
    if (document != NULL && document->impl != NULL) {
        const turbo_xml_document_impl *impl =
            (const turbo_xml_document_impl *)document->impl;
        node.impl = impl->root->root_element;
    }
    return node;
}

turbo_xml_status turbo_xml_serialize_children(
    turbo_xml_node node, char *output, size_t output_capacity,
    size_t max_bytes, size_t *out_size) {
    const cxml_elem_node *element;
    char *serialized;
    size_t serialized_size;
    size_t required_capacity;

    if (turbo_xml_node_type(node) != TURBO_XML_ELEMENT || out_size == NULL ||
        max_bytes == 0u || (output == NULL) != (output_capacity == 0u)) {
        return TURBO_XML_INVALID_ARGUMENT;
    }
    element = (const cxml_elem_node *)node.impl;
    if (element->children.head == NULL) {
        *out_size = 0u;
        if (output != NULL) output[0] = '\0';
        return TURBO_XML_OK;
    }

    serialized = cxml_element_children_to_xml_rstring(
        (cxml_element_node *)element);
    if (serialized == NULL) return TURBO_XML_ALLOCATION_FAILED;
    serialized_size = strlen(serialized);
    *out_size = serialized_size;
    if (serialized_size > max_bytes ||
        !checked_add(serialized_size, 1u, &required_capacity) ||
        (output != NULL && output_capacity < required_capacity)) {
        free(serialized);
        return TURBO_XML_LIMIT_EXCEEDED;
    }
    if (output != NULL) memcpy(output, serialized, required_capacity);
    free(serialized);
    return TURBO_XML_OK;
}

turbo_xml_node_kind turbo_xml_node_type(turbo_xml_node node) {
    if (node.impl == NULL) return TURBO_XML_INVALID_NODE;
    switch (_cxml_node_type(node.impl)) {
        case CXML_ELEM_NODE: return TURBO_XML_ELEMENT;
        case CXML_TEXT_NODE: return TURBO_XML_TEXT;
        case CXML_COMM_NODE: return TURBO_XML_COMMENT;
        case CXML_PI_NODE: return TURBO_XML_PROCESSING_INSTRUCTION;
        default: return TURBO_XML_INVALID_NODE;
    }
}

turbo_xml_location turbo_xml_node_location(turbo_xml_node node) {
    if (node.impl == NULL) return empty_location();
    switch (_cxml_node_type(node.impl)) {
        case CXML_ELEM_NODE:
            return convert_location(((const cxml_elem_node *)node.impl)->source);
        case CXML_TEXT_NODE:
            return convert_location(((const cxml_text_node *)node.impl)->source);
        case CXML_COMM_NODE:
            return convert_location(((const cxml_comm_node *)node.impl)->source);
        case CXML_PI_NODE:
            return convert_location(((const cxml_pi_node *)node.impl)->source);
        default: return empty_location();
    }
}

static turbo_xml_string_view string_view(const cxml_string *string) {
    turbo_xml_string_view view;
    view.data = cxml_string_as_raw((cxml_string *)string);
    view.size = (size_t)cxml_string_len((cxml_string *)string);
    return view;
}

turbo_xml_string_view turbo_xml_node_qualified_name(turbo_xml_node node) {
    if (turbo_xml_node_type(node) != TURBO_XML_ELEMENT) return empty_view();
    return string_view(&((const cxml_elem_node *)node.impl)->name.qname);
}

turbo_xml_string_view turbo_xml_node_local_name(turbo_xml_node node) {
    turbo_xml_string_view view = empty_view();
    const cxml_elem_node *element;
    if (turbo_xml_node_type(node) != TURBO_XML_ELEMENT) return view;
    element = (const cxml_elem_node *)node.impl;
    view.data = element->name.lname;
    view.size = (size_t)element->name.lname_len;
    return view;
}

turbo_xml_string_view turbo_xml_node_namespace_uri(turbo_xml_node node) {
    const cxml_elem_node *element;
    if (turbo_xml_node_type(node) != TURBO_XML_ELEMENT) return empty_view();
    element = (const cxml_elem_node *)node.impl;
    return element->namespace != NULL ? string_view(&element->namespace->uri)
                                      : empty_view();
}

turbo_xml_string_view turbo_xml_node_value(turbo_xml_node node) {
    if (node.impl == NULL) return empty_view();
    switch (_cxml_node_type(node.impl)) {
        case CXML_TEXT_NODE:
            return string_view(&((const cxml_text_node *)node.impl)->value);
        case CXML_COMM_NODE:
            return string_view(&((const cxml_comm_node *)node.impl)->value);
        case CXML_PI_NODE:
            return string_view(&((const cxml_pi_node *)node.impl)->value);
        default: return empty_view();
    }
}

static bool visible_child(const void *child) {
    if (child == NULL || is_whitespace_text(child)) return false;
    switch (_cxml_node_type(child)) {
        case CXML_ELEM_NODE:
        case CXML_TEXT_NODE:
        case CXML_COMM_NODE:
        case CXML_PI_NODE: return true;
        default: return false;
    }
}

size_t turbo_xml_node_child_count(turbo_xml_node node) {
    const cxml_elem_node *element;
    const struct _cxml_list__node *cursor;
    size_t count = 0u;
    if (turbo_xml_node_type(node) != TURBO_XML_ELEMENT) return 0u;
    element = (const cxml_elem_node *)node.impl;
    for (cursor = element->children.head; cursor != NULL; cursor = cursor->next) {
        if (visible_child(cursor->item)) ++count;
    }
    return count;
}

turbo_xml_node turbo_xml_node_child_at(turbo_xml_node node, size_t index) {
    turbo_xml_node result = {NULL};
    const cxml_elem_node *element;
    const struct _cxml_list__node *cursor;
    size_t current = 0u;
    if (turbo_xml_node_type(node) != TURBO_XML_ELEMENT) return result;
    element = (const cxml_elem_node *)node.impl;
    for (cursor = element->children.head; cursor != NULL; cursor = cursor->next) {
        if (!visible_child(cursor->item)) continue;
        if (current == index) {
            result.impl = cursor->item;
            return result;
        }
        ++current;
    }
    return result;
}

size_t turbo_xml_node_attribute_count(turbo_xml_node node) {
    const cxml_elem_node *element;
    if (turbo_xml_node_type(node) != TURBO_XML_ELEMENT) return 0u;
    element = (const cxml_elem_node *)node.impl;
    return element->attributes != NULL
               ? (size_t)cxml_table_size(element->attributes)
               : 0u;
}

turbo_xml_attribute turbo_xml_node_attribute_at(turbo_xml_node node,
                                                 size_t index) {
    turbo_xml_attribute result = {NULL};
    const cxml_elem_node *element;
    const char *key;
    if (turbo_xml_node_type(node) != TURBO_XML_ELEMENT) return result;
    element = (const cxml_elem_node *)node.impl;
    if (element->attributes == NULL || index >= (size_t)element->attributes->count)
        return result;
    key = (const char *)cxml_list_get(&element->attributes->keys, (int)index);
    result.impl = cxml_table_get(element->attributes, key);
    return result;
}

turbo_xml_location turbo_xml_attribute_location(turbo_xml_attribute attribute) {
    return attribute.impl != NULL
               ? convert_location(((const cxml_attr_node *)attribute.impl)->source)
               : empty_location();
}

turbo_xml_string_view turbo_xml_attribute_qualified_name(
    turbo_xml_attribute attribute) {
    return attribute.impl != NULL
               ? string_view(&((const cxml_attr_node *)attribute.impl)->name.qname)
               : empty_view();
}

turbo_xml_string_view turbo_xml_attribute_local_name(
    turbo_xml_attribute attribute) {
    turbo_xml_string_view view = empty_view();
    const cxml_attr_node *node;
    if (attribute.impl == NULL) return view;
    node = (const cxml_attr_node *)attribute.impl;
    view.data = node->name.lname;
    view.size = (size_t)node->name.lname_len;
    return view;
}

turbo_xml_string_view turbo_xml_attribute_namespace_uri(
    turbo_xml_attribute attribute) {
    const cxml_attr_node *node;
    if (attribute.impl == NULL) return empty_view();
    node = (const cxml_attr_node *)attribute.impl;
    return node->namespace != NULL ? string_view(&node->namespace->uri)
                                   : empty_view();
}

turbo_xml_string_view turbo_xml_attribute_value(turbo_xml_attribute attribute) {
    return attribute.impl != NULL
               ? string_view(&((const cxml_attr_node *)attribute.impl)->value)
               : empty_view();
}
