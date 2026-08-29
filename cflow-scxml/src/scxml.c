#include <cflow/scxml.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CFLOW_SCXML_NAMESPACE "http://www.w3.org/2005/07/scxml"
#define CFLOW_SCXML_DEFAULT_MAX_STATES 65536u
#define CFLOW_SCXML_DEFAULT_MAX_EVENTS 65536u
#define CFLOW_SCXML_DEFAULT_MAX_TRANSITIONS 1048576u
#define CFLOW_SCXML_DEFAULT_MAX_NAME_BYTES (16u * 1024u * 1024u)

typedef struct scxml_name_ref {
    turbo_xml_string_view name;
    turbo_xml_location location;
    uint64_t id;
    size_t order;
} scxml_name_ref;

typedef struct scxml_program_name {
    const char *name;
    size_t size;
    uint64_t id;
} scxml_program_name;

typedef struct scxml_node_ref {
    const void *node;
    cflow_machine_state_id id;
} scxml_node_ref;

typedef struct scxml_synthetic_initial {
    cflow_machine_state_id parent;
    cflow_machine_state_id state;
    turbo_xml_string_view target;
    turbo_xml_location location;
} scxml_synthetic_initial;

typedef struct scxml_counts {
    size_t state_rows;
    size_t node_refs;
    size_t state_names;
    size_t synthetic_initials;
    size_t transition_rows;
    size_t event_occurrences;
} scxml_counts;

typedef struct scxml_build {
    cflow_scxml_limits limits;
    cflow_scxml_diagnostic *diagnostic;
    cflow_statechart_state *states;
    cflow_statechart_transition *transitions;
    cflow_event_type *events;
    scxml_name_ref *state_names;
    scxml_name_ref *event_names;
    scxml_name_ref *event_occurrences;
    scxml_node_ref *node_refs;
    scxml_synthetic_initial *synthetic_initials;
    size_t state_index;
    size_t node_ref_index;
    size_t state_name_index;
    size_t synthetic_index;
    size_t transition_index;
    size_t event_occurrence_index;
    size_t event_name_count;
} scxml_build;

typedef struct cflow_scxml_program_impl {
    cflow_statechart statechart;
    scxml_program_name *state_names;
    size_t state_name_count;
    scxml_program_name *event_names;
    size_t event_name_count;
    char *name_storage;
    bool null_value;
} cflow_scxml_program_impl;

typedef enum scxml_element_kind {
    SCXML_ELEMENT_UNKNOWN = 0,
    SCXML_ELEMENT_SCXML,
    SCXML_ELEMENT_STATE,
    SCXML_ELEMENT_PARALLEL,
    SCXML_ELEMENT_TRANSITION,
    SCXML_ELEMENT_INITIAL,
    SCXML_ELEMENT_FINAL,
    SCXML_ELEMENT_HISTORY,
    SCXML_ELEMENT_ONENTRY,
    SCXML_ELEMENT_ONEXIT
} scxml_element_kind;

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

static bool checked_multiply(size_t left, size_t right, size_t *out) {
    if (out == NULL || (left != 0u && right > SIZE_MAX / left)) return false;
    *out = left * right;
    return true;
}

static bool view_equal(turbo_xml_string_view left,
                       turbo_xml_string_view right) {
    return left.size == right.size &&
           (left.size == 0u || memcmp(left.data, right.data, left.size) == 0);
}

static bool view_equal_raw(turbo_xml_string_view view, const char *raw) {
    const size_t size = strlen(raw);
    return view.size == size &&
           (size == 0u || memcmp(view.data, raw, size) == 0);
}

static int compare_view(turbo_xml_string_view left,
                        turbo_xml_string_view right) {
    const size_t common = left.size < right.size ? left.size : right.size;
    const int compared = common != 0u ? memcmp(left.data, right.data, common) : 0;
    if (compared != 0) return compared;
    if (left.size < right.size) return -1;
    if (left.size > right.size) return 1;
    return 0;
}

static int compare_name_ref(const void *left, const void *right) {
    return compare_view(((const scxml_name_ref *)left)->name,
                        ((const scxml_name_ref *)right)->name);
}

static int compare_name_order(const void *left, const void *right) {
    const size_t left_order = ((const scxml_name_ref *)left)->order;
    const size_t right_order = ((const scxml_name_ref *)right)->order;
    return left_order < right_order ? -1 : left_order > right_order ? 1 : 0;
}

static const scxml_name_ref *find_earliest_duplicate(
        scxml_name_ref *names, size_t count) {
    const scxml_name_ref *earliest = NULL;
    size_t group_begin = 0u;

    while (group_begin < count) {
        const scxml_name_ref *first = NULL;
        const scxml_name_ref *second = NULL;
        size_t group_end = group_begin + 1u;
        size_t index;

        while (group_end < count &&
               view_equal(names[group_begin].name, names[group_end].name)) {
            ++group_end;
        }
        for (index = group_begin; index < group_end; ++index) {
            const scxml_name_ref *candidate = &names[index];
            if (first == NULL || candidate->order < first->order) {
                second = first;
                first = candidate;
            } else if (second == NULL || candidate->order < second->order) {
                second = candidate;
            }
        }
        if (second != NULL &&
            (earliest == NULL || second->order < earliest->order)) {
            earliest = second;
        }
        group_begin = group_end;
    }
    return earliest;
}

static int compare_node_ref(const void *left, const void *right) {
    const uintptr_t left_node =
        (uintptr_t)((const scxml_node_ref *)left)->node;
    const uintptr_t right_node =
        (uintptr_t)((const scxml_node_ref *)right)->node;
    return left_node < right_node ? -1 : left_node > right_node ? 1 : 0;
}

static int compare_program_name(const void *left, const void *right) {
    const scxml_program_name *left_name = (const scxml_program_name *)left;
    const scxml_program_name *right_name = (const scxml_program_name *)right;
    const turbo_xml_string_view left_view = {left_name->name, left_name->size};
    const turbo_xml_string_view right_view = {right_name->name, right_name->size};
    return compare_view(left_view, right_view);
}

static cflow_scxml_status scxml_fail(scxml_build *build,
                                     cflow_scxml_status status,
                                     turbo_xml_location location,
                                     const char *message) {
    if (build != NULL && build->diagnostic != NULL) {
        build->diagnostic->status = status;
        build->diagnostic->location = location;
        (void)snprintf(build->diagnostic->message,
                       sizeof(build->diagnostic->message), "%s", message);
    }
    return status;
}

static bool is_empty_view(turbo_xml_string_view view) {
    return view.data == NULL || view.size == 0u;
}

static bool decode_utf8(const char *data, size_t size, size_t *cursor,
                        uint32_t *codepoint) {
    const size_t start = *cursor;
    const unsigned char lead = (unsigned char)data[start];
    size_t width;
    size_t index;
    uint32_t value;

    if (lead <= 0x7fu) {
        *codepoint = lead;
        *cursor = start + 1u;
        return true;
    }
    if (lead >= 0xc2u && lead <= 0xdfu) {
        width = 2u;
        value = lead & 0x1fu;
    } else if (lead >= 0xe0u && lead <= 0xefu) {
        width = 3u;
        value = lead & 0x0fu;
    } else if (lead >= 0xf0u && lead <= 0xf4u) {
        width = 4u;
        value = lead & 0x07u;
    } else {
        return false;
    }
    if (width > size - start) return false;
    for (index = 1u; index < width; ++index) {
        const unsigned char continuation = (unsigned char)data[start + index];
        if ((continuation & 0xc0u) != 0x80u) return false;
        value = (value << 6) | (continuation & 0x3fu);
    }
    if ((width == 3u && value < 0x800u) ||
        (width == 4u && value < 0x10000u) ||
        (value >= 0xd800u && value <= 0xdfffu) || value > 0x10ffffu) {
        return false;
    }
    *codepoint = value;
    *cursor = start + width;
    return true;
}

static bool is_ncname_start(uint32_t codepoint) {
    return codepoint == '_' || (codepoint >= 'A' && codepoint <= 'Z') ||
           (codepoint >= 'a' && codepoint <= 'z') ||
           (codepoint >= 0xc0u && codepoint <= 0xd6u) ||
           (codepoint >= 0xd8u && codepoint <= 0xf6u) ||
           (codepoint >= 0xf8u && codepoint <= 0x2ffu) ||
           (codepoint >= 0x370u && codepoint <= 0x37du) ||
           (codepoint >= 0x37fu && codepoint <= 0x1fffu) ||
           (codepoint >= 0x200cu && codepoint <= 0x200du) ||
           (codepoint >= 0x2070u && codepoint <= 0x218fu) ||
           (codepoint >= 0x2c00u && codepoint <= 0x2fefu) ||
           (codepoint >= 0x3001u && codepoint <= 0xd7ffu) ||
           (codepoint >= 0xf900u && codepoint <= 0xfdcfu) ||
           (codepoint >= 0xfdf0u && codepoint <= 0xfffdu) ||
           (codepoint >= 0x10000u && codepoint <= 0xeffffu);
}

static bool is_ncname_char(uint32_t codepoint) {
    return is_ncname_start(codepoint) || codepoint == '-' || codepoint == '.' ||
           (codepoint >= '0' && codepoint <= '9') || codepoint == 0xb7u ||
           (codepoint >= 0x300u && codepoint <= 0x36fu) ||
           (codepoint >= 0x203fu && codepoint <= 0x2040u);
}

static bool is_xml_ncname(turbo_xml_string_view name) {
    size_t cursor = 0u;
    uint32_t codepoint;
    if (is_empty_view(name) ||
        !decode_utf8(name.data, name.size, &cursor, &codepoint) ||
        !is_ncname_start(codepoint)) {
        return false;
    }
    while (cursor < name.size) {
        if (!decode_utf8(name.data, name.size, &cursor, &codepoint) ||
            !is_ncname_char(codepoint)) {
            return false;
        }
    }
    return true;
}

static scxml_element_kind element_kind(turbo_xml_node node) {
    const turbo_xml_string_view name = turbo_xml_node_local_name(node);
    if (view_equal_raw(name, "scxml")) return SCXML_ELEMENT_SCXML;
    if (view_equal_raw(name, "state")) return SCXML_ELEMENT_STATE;
    if (view_equal_raw(name, "parallel")) return SCXML_ELEMENT_PARALLEL;
    if (view_equal_raw(name, "transition")) return SCXML_ELEMENT_TRANSITION;
    if (view_equal_raw(name, "initial")) return SCXML_ELEMENT_INITIAL;
    if (view_equal_raw(name, "final")) return SCXML_ELEMENT_FINAL;
    if (view_equal_raw(name, "history")) return SCXML_ELEMENT_HISTORY;
    if (view_equal_raw(name, "onentry")) return SCXML_ELEMENT_ONENTRY;
    if (view_equal_raw(name, "onexit")) return SCXML_ELEMENT_ONEXIT;
    return SCXML_ELEMENT_UNKNOWN;
}

static bool is_state_element(scxml_element_kind kind) {
    return kind == SCXML_ELEMENT_STATE || kind == SCXML_ELEMENT_PARALLEL ||
           kind == SCXML_ELEMENT_FINAL;
}

static turbo_xml_attribute find_attribute(turbo_xml_node node,
                                          const char *local_name) {
    turbo_xml_attribute result = {NULL};
    size_t index;
    for (index = 0u; index < turbo_xml_node_attribute_count(node); ++index) {
        const turbo_xml_attribute attribute =
            turbo_xml_node_attribute_at(node, index);
        const turbo_xml_string_view namespace_uri =
            turbo_xml_attribute_namespace_uri(attribute);
        if (!is_empty_view(namespace_uri)) continue;
        if (view_equal_raw(turbo_xml_attribute_local_name(attribute),
                           local_name)) {
            return attribute;
        }
    }
    return result;
}

static bool attribute_allowed(scxml_element_kind kind,
                              turbo_xml_string_view name) {
    switch (kind) {
        case SCXML_ELEMENT_SCXML:
            return view_equal_raw(name, "version") ||
                   view_equal_raw(name, "datamodel") ||
                   view_equal_raw(name, "initial") ||
                   view_equal_raw(name, "id");
        case SCXML_ELEMENT_STATE:
            return view_equal_raw(name, "id") ||
                   view_equal_raw(name, "initial");
        case SCXML_ELEMENT_PARALLEL:
        case SCXML_ELEMENT_FINAL:
            return view_equal_raw(name, "id");
        case SCXML_ELEMENT_HISTORY:
            return view_equal_raw(name, "id") ||
                   view_equal_raw(name, "type");
        case SCXML_ELEMENT_TRANSITION:
            return view_equal_raw(name, "event") ||
                   view_equal_raw(name, "target") ||
                   view_equal_raw(name, "type") ||
                   view_equal_raw(name, "cond");
        case SCXML_ELEMENT_INITIAL:
        case SCXML_ELEMENT_ONENTRY:
        case SCXML_ELEMENT_ONEXIT:
        case SCXML_ELEMENT_UNKNOWN: return false;
    }
    return false;
}

static cflow_scxml_status validate_element_attributes(
    scxml_build *build, turbo_xml_node node, scxml_element_kind kind) {
    size_t index;
    for (index = 0u; index < turbo_xml_node_attribute_count(node); ++index) {
        const turbo_xml_attribute attribute =
            turbo_xml_node_attribute_at(node, index);
        const turbo_xml_string_view namespace_uri =
            turbo_xml_attribute_namespace_uri(attribute);
        const turbo_xml_string_view name =
            turbo_xml_attribute_local_name(attribute);
        if (!is_empty_view(namespace_uri)) continue;
        if (kind == SCXML_ELEMENT_TRANSITION && view_equal_raw(name, "cond")) {
            return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                              turbo_xml_attribute_location(attribute),
                              "transition conditions require a later data-model phase");
        }
        if (!attribute_allowed(kind, name)) {
            return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                              turbo_xml_attribute_location(attribute),
                              "unsupported unqualified SCXML attribute");
        }
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status require_scxml_element(scxml_build *build,
                                                turbo_xml_node node,
                                                scxml_element_kind *out_kind) {
    const turbo_xml_string_view namespace_uri =
        turbo_xml_node_namespace_uri(node);
    const scxml_element_kind kind = element_kind(node);
    if (!view_equal_raw(namespace_uri, CFLOW_SCXML_NAMESPACE)) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_NAMESPACE,
                          turbo_xml_node_location(node),
                          "SCXML elements must use the W3C SCXML namespace");
    }
    if (kind == SCXML_ELEMENT_UNKNOWN) {
        return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                          turbo_xml_node_location(node),
                          "unsupported SCXML element");
    }
    *out_kind = kind;
    return validate_element_attributes(build, node, kind);
}

static size_t element_child_count(turbo_xml_node node,
                                  scxml_element_kind wanted) {
    size_t count = 0u;
    size_t index;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        if (turbo_xml_node_type(child) == TURBO_XML_ELEMENT &&
            element_kind(child) == wanted) {
            ++count;
        }
    }
    return count;
}

static turbo_xml_node first_real_child(turbo_xml_node node) {
    size_t index;
    turbo_xml_node empty = {NULL};
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        if (turbo_xml_node_type(child) == TURBO_XML_ELEMENT &&
            is_state_element(element_kind(child))) {
            return child;
        }
    }
    return empty;
}

static bool token_next(turbo_xml_string_view value, size_t *cursor,
                       turbo_xml_string_view *token) {
    size_t begin;
    if (cursor == NULL || token == NULL) return false;
    begin = *cursor;
    while (begin < value.size && isspace((unsigned char)value.data[begin]))
        ++begin;
    if (begin == value.size) {
        *cursor = begin;
        return false;
    }
    *cursor = begin;
    while (*cursor < value.size &&
           !isspace((unsigned char)value.data[*cursor])) {
        ++*cursor;
    }
    token->data = value.data + begin;
    token->size = *cursor - begin;
    return true;
}

static bool token_has_wildcard(turbo_xml_string_view token) {
    return token.size != 0u && memchr(token.data, '*', token.size) != NULL;
}

static bool completion_token(turbo_xml_string_view token,
                             turbo_xml_string_view *state_name) {
    static const char prefix[] = "done.state.";
    if (token.size <= sizeof(prefix) - 1u ||
        memcmp(token.data, prefix, sizeof(prefix) - 1u) != 0) {
        return false;
    }
    state_name->data = token.data + sizeof(prefix) - 1u;
    state_name->size = token.size - (sizeof(prefix) - 1u);
    return true;
}

static cflow_scxml_status analyze_transition(scxml_build *build,
                                             turbo_xml_node node,
                                             bool require_default,
                                             scxml_counts *counts) {
    turbo_xml_attribute event_attribute;
    turbo_xml_attribute target_attribute;
    turbo_xml_string_view value;
    turbo_xml_string_view token;
    size_t cursor = 0u;
    size_t token_count = 0u;
    cflow_scxml_status status;
    size_t index;

    status = validate_element_attributes(build, node, SCXML_ELEMENT_TRANSITION);
    if (status != CFLOW_SCXML_OK) return status;
    if (turbo_xml_node_child_count(node) != 0u) {
        return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                          turbo_xml_node_location(node),
                          "transition executable content is not supported");
    }
    event_attribute = find_attribute(node, "event");
    target_attribute = find_attribute(node, "target");
    if (require_default && event_attribute.impl != NULL) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_attribute_location(event_attribute),
                          "initial and history defaults must be eventless");
    }
    if (require_default && target_attribute.impl == NULL) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "initial and history defaults require one target");
    }
    if (target_attribute.impl != NULL) {
        value = turbo_xml_attribute_value(target_attribute);
        cursor = 0u;
        while (token_next(value, &cursor, &token)) ++token_count;
        if (token_count != 1u) {
            return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                              turbo_xml_attribute_location(target_attribute),
                              "exactly one transition target is supported");
        }
    }
    token_count = 0u;
    if (event_attribute.impl != NULL) {
        value = turbo_xml_attribute_value(event_attribute);
        cursor = 0u;
        while (token_next(value, &cursor, &token)) {
            turbo_xml_string_view completed = {NULL, 0u};
            if (token_has_wildcard(token)) {
                return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                                  turbo_xml_attribute_location(event_attribute),
                                  "wildcard event descriptors are not supported");
            }
            ++token_count;
            if (!completion_token(token, &completed) &&
                !checked_add(counts->event_occurrences, 1u,
                             &counts->event_occurrences)) {
                return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                  turbo_xml_attribute_location(event_attribute),
                                  "event occurrence count overflow");
            }
        }
        if (token_count == 0u) {
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_attribute_location(event_attribute),
                              "event attribute must contain a descriptor");
        }
    } else {
        token_count = 1u;
    }
    if (!checked_add(counts->transition_rows, token_count,
                     &counts->transition_rows)) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "transition count overflow");
    }
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        if (turbo_xml_node_type(turbo_xml_node_child_at(node, index)) ==
            TURBO_XML_TEXT) {
            return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                              turbo_xml_node_location(
                                  turbo_xml_node_child_at(node, index)),
                              "transition text content is not supported");
        }
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status analyze_state(scxml_build *build,
                                        turbo_xml_node node,
                                        scxml_element_kind kind,
                                        bool is_root,
                                        scxml_counts *counts) {
    const turbo_xml_attribute id_attribute = find_attribute(node, "id");
    const turbo_xml_attribute initial_attribute = find_attribute(node, "initial");
    const size_t real_children =
        element_child_count(node, SCXML_ELEMENT_STATE) +
        element_child_count(node, SCXML_ELEMENT_PARALLEL) +
        element_child_count(node, SCXML_ELEMENT_FINAL);
    const size_t explicit_initials =
        element_child_count(node, SCXML_ELEMENT_INITIAL);
    const bool compound = is_root ||
        (kind == SCXML_ELEMENT_STATE && real_children != 0u);
    size_t index;
    cflow_scxml_status status;

    if (!checked_add(counts->state_rows, 1u, &counts->state_rows) ||
        !checked_add(counts->node_refs, 1u, &counts->node_refs)) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "state count overflow");
    }
    if (id_attribute.impl != NULL &&
        !is_xml_ncname(turbo_xml_attribute_value(id_attribute))) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_attribute_location(id_attribute),
                          "SCXML state id must be an XML NCName");
    }
    if (is_root) {
        if (id_attribute.impl != NULL) {
            if (!checked_add(counts->state_names, 1u,
                             &counts->state_names)) {
                return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                  turbo_xml_attribute_location(id_attribute),
                                  "state name count overflow");
            }
        }
    } else if (kind != SCXML_ELEMENT_INITIAL) {
        if (id_attribute.impl == NULL ||
            is_empty_view(turbo_xml_attribute_value(id_attribute))) {
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_node_location(node),
                              "supported SCXML states require a nonempty id");
        }
        if (!checked_add(counts->state_names, 1u, &counts->state_names)) {
            return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                              turbo_xml_attribute_location(id_attribute),
                              "state name count overflow");
        }
    }
    if (kind == SCXML_ELEMENT_HISTORY) {
        const turbo_xml_attribute type_attribute = find_attribute(node, "type");
        if (type_attribute.impl != NULL) {
            const turbo_xml_string_view type =
                turbo_xml_attribute_value(type_attribute);
            if (!view_equal_raw(type, "shallow") &&
                !view_equal_raw(type, "deep")) {
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_attribute_location(type_attribute),
                                  "history type must be shallow or deep");
            }
        }
    }
    if ((kind == SCXML_ELEMENT_PARALLEL && real_children == 0u) ||
        (is_root && real_children == 0u)) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "compound and parallel states require real children");
    }
    if (kind == SCXML_ELEMENT_STATE && !compound &&
        (explicit_initials != 0u || initial_attribute.impl != NULL ||
         element_child_count(node, SCXML_ELEMENT_HISTORY) != 0u)) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "atomic states cannot declare initial or history children");
    }
    if (kind == SCXML_ELEMENT_PARALLEL &&
        (explicit_initials != 0u || initial_attribute.impl != NULL)) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "parallel states do not declare one initial child");
    }
    if (compound) {
        if (explicit_initials > 1u ||
            (explicit_initials != 0u && initial_attribute.impl != NULL)) {
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_node_location(node),
                              "compound states require one initial declaration");
        }
        if (explicit_initials == 0u) {
            if (!checked_add(counts->state_rows, 1u, &counts->state_rows) ||
                !checked_add(counts->synthetic_initials, 1u,
                             &counts->synthetic_initials) ||
                !checked_add(counts->transition_rows, 1u,
                             &counts->transition_rows)) {
                return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                  turbo_xml_node_location(node),
                                  "synthetic initial count overflow");
            }
        }
    }

    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        scxml_element_kind child_kind;
        if (turbo_xml_node_type(child) == TURBO_XML_COMMENT) continue;
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT) {
            return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                              turbo_xml_node_location(child),
                              "non-whitespace SCXML text is not supported");
        }
        status = require_scxml_element(build, child, &child_kind);
        if (status != CFLOW_SCXML_OK) return status;
        if (is_state_element(child_kind)) {
            if (kind == SCXML_ELEMENT_FINAL || kind == SCXML_ELEMENT_INITIAL ||
                kind == SCXML_ELEMENT_HISTORY) {
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "this SCXML element cannot contain states");
            }
            status = analyze_state(build, child, child_kind, false, counts);
        } else if (child_kind == SCXML_ELEMENT_INITIAL) {
            if (!compound) {
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "initial must be a child of a compound state");
            }
            status = analyze_state(build, child, child_kind, false, counts);
        } else if (child_kind == SCXML_ELEMENT_HISTORY) {
            if (!(compound || kind == SCXML_ELEMENT_PARALLEL)) {
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "history must be a child of compound or parallel");
            }
            status = analyze_state(build, child, child_kind, false, counts);
        } else if (child_kind == SCXML_ELEMENT_TRANSITION) {
            if (is_root || kind == SCXML_ELEMENT_FINAL) {
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "this SCXML element cannot contain transitions");
            }
            status = analyze_transition(
                build, child,
                kind == SCXML_ELEMENT_INITIAL || kind == SCXML_ELEMENT_HISTORY,
                counts);
        } else if (child_kind == SCXML_ELEMENT_ONENTRY ||
                   child_kind == SCXML_ELEMENT_ONEXIT) {
            if (kind == SCXML_ELEMENT_INITIAL || kind == SCXML_ELEMENT_HISTORY ||
                is_root) {
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "onentry/onexit is not allowed at this location");
            }
            if (turbo_xml_node_child_count(child) != 0u) {
                return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                                  turbo_xml_node_location(child),
                                  "onentry/onexit executable content is not supported");
            }
            status = CFLOW_SCXML_OK;
        } else {
            return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                              turbo_xml_node_location(child),
                              "unsupported SCXML child element");
        }
        if (status != CFLOW_SCXML_OK) return status;
    }

    if ((kind == SCXML_ELEMENT_INITIAL || kind == SCXML_ELEMENT_HISTORY) &&
        element_child_count(node, SCXML_ELEMENT_TRANSITION) != 1u) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "initial and history require exactly one transition");
    }
    return CFLOW_SCXML_OK;
}

static cflow_statechart_state_kind native_state_kind(turbo_xml_node node,
                                                      bool is_root) {
    const scxml_element_kind kind = element_kind(node);
    if (is_root) return CFLOW_STATECHART_COMPOUND;
    if (kind == SCXML_ELEMENT_PARALLEL) return CFLOW_STATECHART_PARALLEL;
    if (kind == SCXML_ELEMENT_FINAL) return CFLOW_STATECHART_FINAL;
    if (kind == SCXML_ELEMENT_INITIAL) return CFLOW_STATECHART_INITIAL;
    if (kind == SCXML_ELEMENT_HISTORY) {
        const turbo_xml_attribute type = find_attribute(node, "type");
        return type.impl != NULL &&
                       view_equal_raw(turbo_xml_attribute_value(type), "deep")
                   ? CFLOW_STATECHART_HISTORY_DEEP
                   : CFLOW_STATECHART_HISTORY_SHALLOW;
    }
    return first_real_child(node).impl != NULL ? CFLOW_STATECHART_COMPOUND
                                                : CFLOW_STATECHART_ATOMIC;
}

static cflow_scxml_status emit_state(scxml_build *build,
                                     turbo_xml_node node,
                                     cflow_machine_state_id parent,
                                     bool is_root) {
    const scxml_element_kind kind = element_kind(node);
    const cflow_machine_state_id id =
        (cflow_machine_state_id)(build->state_index + 1u);
    const cflow_statechart_state_kind native_kind =
        native_state_kind(node, is_root);
    const turbo_xml_attribute id_attribute = find_attribute(node, "id");
    const turbo_xml_attribute initial_attribute = find_attribute(node, "initial");
    const bool compound = native_kind == CFLOW_STATECHART_COMPOUND;
    const bool has_explicit_initial =
        element_child_count(node, SCXML_ELEMENT_INITIAL) != 0u;
    size_t index;

    build->states[build->state_index] = (cflow_statechart_state){
        id, parent, native_kind, (uint32_t)build->state_index};
    ++build->state_index;
    build->node_refs[build->node_ref_index++] =
        (scxml_node_ref){node.impl, id};
    if (id_attribute.impl != NULL) {
        build->state_names[build->state_name_index] = (scxml_name_ref){
            turbo_xml_attribute_value(id_attribute),
            turbo_xml_attribute_location(id_attribute), id,
            build->state_name_index};
        ++build->state_name_index;
    }

    if (compound && !has_explicit_initial) {
        turbo_xml_node target_node = first_real_child(node);
        turbo_xml_attribute target_id;
        turbo_xml_string_view target;
        turbo_xml_location location;
        const cflow_machine_state_id initial_id =
            (cflow_machine_state_id)(build->state_index + 1u);
        if (initial_attribute.impl != NULL) {
            target = turbo_xml_attribute_value(initial_attribute);
            location = turbo_xml_attribute_location(initial_attribute);
        } else {
            target_id = find_attribute(target_node, "id");
            target = turbo_xml_attribute_value(target_id);
            location = turbo_xml_node_location(target_node);
        }
        build->states[build->state_index] = (cflow_statechart_state){
            initial_id, id, CFLOW_STATECHART_INITIAL,
            (uint32_t)build->state_index};
        ++build->state_index;
        build->synthetic_initials[build->synthetic_index++] =
            (scxml_synthetic_initial){id, initial_id, target, location};
    }

    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        const scxml_element_kind child_kind = element_kind(child);
        if (turbo_xml_node_type(child) == TURBO_XML_ELEMENT &&
            (is_state_element(child_kind) ||
             child_kind == SCXML_ELEMENT_INITIAL ||
             child_kind == SCXML_ELEMENT_HISTORY)) {
            cflow_scxml_status status = emit_state(build, child, id, false);
            if (status != CFLOW_SCXML_OK) return status;
        }
    }
    (void)kind;
    return CFLOW_SCXML_OK;
}

static const scxml_name_ref *find_name_ref(const scxml_name_ref *names,
                                           size_t count,
                                           turbo_xml_string_view name) {
    size_t low = 0u;
    size_t high = count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        const int compared = compare_view(names[middle].name, name);
        if (compared < 0) low = middle + 1u;
        else high = middle;
    }
    return low < count && view_equal(names[low].name, name) ? &names[low]
                                                             : NULL;
}

static cflow_machine_state_id node_id(const scxml_build *build,
                                      turbo_xml_node node,
                                      size_t node_count) {
    size_t low = 0u;
    size_t high = node_count;
    const uintptr_t wanted = (uintptr_t)node.impl;
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        const uintptr_t value = (uintptr_t)build->node_refs[middle].node;
        if (value < wanted) low = middle + 1u;
        else high = middle;
    }
    return low < node_count && build->node_refs[low].node == node.impl
               ? build->node_refs[low].id
               : 0u;
}

static cflow_scxml_status collect_transition_events(
    scxml_build *build, turbo_xml_node node) {
    size_t index;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        const scxml_element_kind child_kind = element_kind(child);
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT) continue;
        if (child_kind == SCXML_ELEMENT_TRANSITION) {
            const turbo_xml_attribute event_attribute =
                find_attribute(child, "event");
            if (event_attribute.impl != NULL) {
                const turbo_xml_string_view value =
                    turbo_xml_attribute_value(event_attribute);
                turbo_xml_string_view token;
                size_t cursor = 0u;
                while (token_next(value, &cursor, &token)) {
                    turbo_xml_string_view completed = {NULL, 0u};
                    if (!completion_token(token, &completed)) {
                        build->event_occurrences[build->event_occurrence_index] =
                            (scxml_name_ref){
                                token,
                                turbo_xml_attribute_location(event_attribute),
                                0u, build->event_occurrence_index};
                        ++build->event_occurrence_index;
                    }
                }
            }
        } else if (is_state_element(child_kind) ||
                   child_kind == SCXML_ELEMENT_INITIAL ||
                   child_kind == SCXML_ELEMENT_HISTORY) {
            cflow_scxml_status status =
                collect_transition_events(build, child);
            if (status != CFLOW_SCXML_OK) return status;
        }
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status build_event_names(scxml_build *build,
                                            size_t occurrence_count) {
    size_t index = 0u;
    size_t unique = 0u;
    if (occurrence_count == 0u) {
        build->event_name_count = 0u;
        return CFLOW_SCXML_OK;
    }
    qsort(build->event_occurrences, occurrence_count,
          sizeof(*build->event_occurrences), compare_name_ref);
    while (index < occurrence_count) {
        size_t next = index + 1u;
        scxml_name_ref selected = build->event_occurrences[index];
        while (next < occurrence_count &&
               view_equal(build->event_occurrences[index].name,
                          build->event_occurrences[next].name)) {
            if (build->event_occurrences[next].order < selected.order)
                selected = build->event_occurrences[next];
            ++next;
        }
        build->event_names[unique++] = selected;
        index = next;
    }
    if (unique > build->limits.max_events) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          build->event_names[build->limits.max_events].location,
                          "SCXML event count exceeds max_events");
    }
    qsort(build->event_names, unique, sizeof(*build->event_names),
          compare_name_order);
    for (index = 0u; index < unique; ++index) {
        build->event_names[index].id = (cflow_event_id)(index + 1u);
        build->events[index] = (cflow_event_type){
            (cflow_event_id)(index + 1u), &cmeta_type_bool};
    }
    qsort(build->event_names, unique, sizeof(*build->event_names),
          compare_name_ref);
    build->event_name_count = unique;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status resolve_target(scxml_build *build,
                                         turbo_xml_attribute target_attribute,
                                         cflow_machine_state_id *out_target) {
    turbo_xml_string_view target = {NULL, 0u};
    const scxml_name_ref *resolved;
    size_t cursor = 0u;
    if (target_attribute.impl == NULL) {
        *out_target = 0u;
        return CFLOW_SCXML_OK;
    }
    (void)token_next(turbo_xml_attribute_value(target_attribute), &cursor,
                     &target);
    resolved = find_name_ref(build->state_names, build->state_name_index,
                             target);
    if (resolved == NULL) {
        return scxml_fail(build, CFLOW_SCXML_UNKNOWN_TARGET,
                          turbo_xml_attribute_location(target_attribute),
                          "transition target does not name a declared state");
    }
    *out_target = (cflow_machine_state_id)resolved->id;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_transition_token(
    scxml_build *build, cflow_machine_state_id source,
    turbo_xml_node transition_node, turbo_xml_string_view event_token,
    bool has_event) {
    const turbo_xml_attribute target_attribute =
        find_attribute(transition_node, "target");
    const turbo_xml_attribute type_attribute =
        find_attribute(transition_node, "type");
    cflow_statechart_transition row;
    cflow_scxml_status status;

    memset(&row, 0, sizeof(row));
    row.id = (cflow_statechart_transition_id)(build->transition_index + 1u);
    row.source = source;
    row.kind = type_attribute.impl != NULL &&
                       view_equal_raw(turbo_xml_attribute_value(type_attribute),
                                      "internal")
                   ? CFLOW_STATECHART_TRANSITION_INTERNAL
                   : CFLOW_STATECHART_TRANSITION_EXTERNAL;
    if (type_attribute.impl != NULL &&
        !view_equal_raw(turbo_xml_attribute_value(type_attribute), "internal") &&
        !view_equal_raw(turbo_xml_attribute_value(type_attribute), "external")) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_attribute_location(type_attribute),
                          "transition type must be internal or external");
    }
    row.priority = (uint32_t)build->transition_index;
    row.document_order = (uint32_t)build->transition_index;
    status = resolve_target(build, target_attribute, &row.target);
    if (status != CFLOW_SCXML_OK) return status;
    if (!has_event) {
        row.trigger = CFLOW_STATECHART_TRIGGER_EVENTLESS;
    } else {
        turbo_xml_string_view completed = {NULL, 0u};
        if (completion_token(event_token, &completed)) {
            const scxml_name_ref *state =
                find_name_ref(build->state_names, build->state_name_index,
                              completed);
            if (state == NULL) {
                return scxml_fail(build, CFLOW_SCXML_UNKNOWN_TARGET,
                                  turbo_xml_node_location(transition_node),
                                  "done.state event names an unknown state");
            }
            row.trigger = CFLOW_STATECHART_TRIGGER_COMPLETION;
            row.completion = (cflow_machine_state_id)state->id;
        } else {
            const scxml_name_ref *event =
                find_name_ref(build->event_names, build->event_name_count,
                              event_token);
            if (event == NULL) {
                return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                                  turbo_xml_node_location(transition_node),
                                  "SCXML event map invariant failed");
            }
            row.trigger = CFLOW_STATECHART_TRIGGER_EVENT;
            row.event = (cflow_event_id)event->id;
        }
    }
    build->transitions[build->transition_index++] = row;
    return CFLOW_SCXML_OK;
}

static const scxml_synthetic_initial *find_synthetic(
    const scxml_build *build, size_t synthetic_count,
    cflow_machine_state_id parent) {
    size_t low = 0u;
    size_t high = synthetic_count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        if (build->synthetic_initials[middle].parent < parent)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < synthetic_count &&
                   build->synthetic_initials[low].parent == parent
               ? &build->synthetic_initials[low]
               : NULL;
}

static cflow_scxml_status emit_transitions(scxml_build *build,
                                           turbo_xml_node node,
                                           size_t node_count,
                                           size_t synthetic_count) {
    const cflow_machine_state_id source = node_id(build, node, node_count);
    const scxml_synthetic_initial *synthetic =
        find_synthetic(build, synthetic_count, source);
    size_t index;
    if (synthetic != NULL) {
        const scxml_name_ref *target =
            find_name_ref(build->state_names, build->state_name_index,
                          synthetic->target);
        cflow_statechart_transition row;
        if (target == NULL) {
            return scxml_fail(build, CFLOW_SCXML_UNKNOWN_TARGET,
                              synthetic->location,
                              "initial target does not name a declared state");
        }
        memset(&row, 0, sizeof(row));
        row.id = (cflow_statechart_transition_id)(build->transition_index + 1u);
        row.source = synthetic->state;
        row.trigger = CFLOW_STATECHART_TRIGGER_EVENTLESS;
        row.target = (cflow_machine_state_id)target->id;
        row.kind = CFLOW_STATECHART_TRANSITION_EXTERNAL;
        row.priority = (uint32_t)build->transition_index;
        row.document_order = (uint32_t)build->transition_index;
        build->transitions[build->transition_index++] = row;
    }
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        const scxml_element_kind child_kind = element_kind(child);
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT) continue;
        if (child_kind == SCXML_ELEMENT_TRANSITION) {
            const turbo_xml_attribute event_attribute =
                find_attribute(child, "event");
            if (event_attribute.impl == NULL) {
                cflow_scxml_status status = emit_transition_token(
                    build, source, child, (turbo_xml_string_view){NULL, 0u},
                    false);
                if (status != CFLOW_SCXML_OK) return status;
            } else {
                const turbo_xml_string_view value =
                    turbo_xml_attribute_value(event_attribute);
                turbo_xml_string_view token;
                size_t cursor = 0u;
                while (token_next(value, &cursor, &token)) {
                    cflow_scxml_status status = emit_transition_token(
                        build, source, child, token, true);
                    if (status != CFLOW_SCXML_OK) return status;
                }
            }
        } else if (is_state_element(child_kind) ||
                   child_kind == SCXML_ELEMENT_INITIAL ||
                   child_kind == SCXML_ELEMENT_HISTORY) {
            cflow_scxml_status status = emit_transitions(
                build, child, node_count, synthetic_count);
            if (status != CFLOW_SCXML_OK) return status;
        }
    }
    return CFLOW_SCXML_OK;
}

static void free_build(scxml_build *build) {
    free(build->states);
    free(build->transitions);
    free(build->events);
    free(build->state_names);
    free(build->event_names);
    free(build->event_occurrences);
    free(build->node_refs);
    free(build->synthetic_initials);
    memset(build, 0, sizeof(*build));
}

static void *allocate_rows(size_t count, size_t element_size) {
    size_t bytes;
    if (count == 0u) return NULL;
    if (!checked_multiply(count, element_size, &bytes)) return NULL;
    return calloc(1u, bytes);
}

static void copy_program_names(scxml_program_name *destination,
                               const scxml_name_ref *source,
                               size_t count, char **cursor) {
    size_t index;
    for (index = 0u; index < count; ++index) {
        destination[index].name = *cursor;
        destination[index].size = source[index].name.size;
        destination[index].id = source[index].id;
        memcpy(*cursor, source[index].name.data, source[index].name.size);
        *cursor += source[index].name.size;
    }
}

cflow_scxml_limits cflow_scxml_default_limits(void) {
    const cflow_scxml_limits limits = {
        {16u * 1024u * 1024u, 1048576u, 1048576u, 256u,
         32u * 1024u * 1024u},
        CFLOW_SCXML_DEFAULT_MAX_STATES,
        CFLOW_SCXML_DEFAULT_MAX_EVENTS,
        CFLOW_SCXML_DEFAULT_MAX_TRANSITIONS,
        CFLOW_SCXML_DEFAULT_MAX_NAME_BYTES};
    return limits;
}

cflow_scxml_status cflow_scxml_compile(
    cflow_scxml_program *out, const char *input, size_t input_size,
    const cflow_scxml_limits *limits_or_null,
    cflow_scxml_diagnostic *diagnostic) {
    cflow_scxml_limits limits = limits_or_null != NULL
                                    ? *limits_or_null
                                    : cflow_scxml_default_limits();
    turbo_xml_document document = {0};
    turbo_xml_diagnostic xml_diagnostic = {0};
    turbo_xml_node root;
    scxml_build build;
    scxml_counts counts = {0};
    cflow_scxml_program_impl *impl = NULL;
    cflow_statechart_definition definition;
    cflow_statechart_status native_status;
    cflow_scxml_status status;
    turbo_xml_attribute version;
    turbo_xml_attribute datamodel;
    size_t index;
    size_t name_bytes = 0u;
    char *name_cursor;

    memset(&build, 0, sizeof(build));
    build.limits = limits;
    build.diagnostic = diagnostic;
    if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
    if (out == NULL || out->impl != NULL || input == NULL || input_size == 0u ||
        limits.max_states == 0u || limits.max_events == 0u ||
        limits.max_transitions == 0u || limits.max_name_bytes == 0u) {
        return scxml_fail(&build, CFLOW_SCXML_INVALID_ARGUMENT,
                          (turbo_xml_location){0u, 0u, 0u},
                          "output/input and all SCXML limits must be valid");
    }
    switch (turbo_xml_parse(&document, input, input_size, &limits.xml,
                            &xml_diagnostic)) {
        case TURBO_XML_OK: break;
        case TURBO_XML_LIMIT_EXCEEDED:
            return scxml_fail(&build, CFLOW_SCXML_LIMIT_EXCEEDED,
                              xml_diagnostic.location,
                              xml_diagnostic.message);
        case TURBO_XML_ALLOCATION_FAILED:
            return scxml_fail(&build, CFLOW_SCXML_ALLOCATION_FAILED,
                              xml_diagnostic.location,
                              xml_diagnostic.message);
        default:
            return scxml_fail(&build, CFLOW_SCXML_XML_ERROR,
                              xml_diagnostic.location,
                              xml_diagnostic.message);
    }
    root = turbo_xml_document_root(&document);
    if (element_kind(root) != SCXML_ELEMENT_SCXML ||
        !view_equal_raw(turbo_xml_node_namespace_uri(root),
                        CFLOW_SCXML_NAMESPACE)) {
        status = scxml_fail(&build, CFLOW_SCXML_INVALID_NAMESPACE,
                            turbo_xml_node_location(root),
                            "root must be W3C SCXML scxml element");
        goto cleanup;
    }
    status = validate_element_attributes(&build, root, SCXML_ELEMENT_SCXML);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    version = find_attribute(root, "version");
    if (version.impl == NULL ||
        !view_equal_raw(turbo_xml_attribute_value(version), "1.0")) {
        status = scxml_fail(
            &build, CFLOW_SCXML_INVALID_VERSION,
            version.impl != NULL ? turbo_xml_attribute_location(version)
                                 : turbo_xml_node_location(root),
            "SCXML version must be 1.0");
        goto cleanup;
    }
    datamodel = find_attribute(root, "datamodel");
    if (datamodel.impl != NULL &&
        !view_equal_raw(turbo_xml_attribute_value(datamodel), "null")) {
        status = scxml_fail(&build, CFLOW_SCXML_UNSUPPORTED_DATAMODEL,
                            turbo_xml_attribute_location(datamodel),
                            "only the SCXML null data model is supported");
        goto cleanup;
    }
    status = analyze_state(&build, root, SCXML_ELEMENT_SCXML, true, &counts);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    if (counts.state_rows > limits.max_states ||
        counts.transition_rows > limits.max_transitions ||
        counts.state_rows > UINT32_MAX ||
        counts.transition_rows > UINT32_MAX) {
        status = scxml_fail(&build, CFLOW_SCXML_LIMIT_EXCEEDED,
                            turbo_xml_node_location(root),
                            "SCXML state or transition count exceeds limits");
        goto cleanup;
    }

    build.states = allocate_rows(counts.state_rows, sizeof(*build.states));
    build.transitions =
        allocate_rows(counts.transition_rows, sizeof(*build.transitions));
    build.events =
        allocate_rows(counts.event_occurrences, sizeof(*build.events));
    build.state_names =
        allocate_rows(counts.state_names, sizeof(*build.state_names));
    build.event_names =
        allocate_rows(counts.event_occurrences, sizeof(*build.event_names));
    build.event_occurrences = allocate_rows(
        counts.event_occurrences, sizeof(*build.event_occurrences));
    build.node_refs =
        allocate_rows(counts.node_refs, sizeof(*build.node_refs));
    build.synthetic_initials = allocate_rows(
        counts.synthetic_initials, sizeof(*build.synthetic_initials));
    if ((counts.state_rows != 0u && build.states == NULL) ||
        (counts.transition_rows != 0u && build.transitions == NULL) ||
        (counts.event_occurrences != 0u &&
         (build.events == NULL || build.event_names == NULL ||
          build.event_occurrences == NULL)) ||
        (counts.state_names != 0u && build.state_names == NULL) ||
        (counts.node_refs != 0u && build.node_refs == NULL) ||
        (counts.synthetic_initials != 0u &&
         build.synthetic_initials == NULL)) {
        status = scxml_fail(&build, CFLOW_SCXML_ALLOCATION_FAILED,
                            turbo_xml_node_location(root),
                            "unable to allocate bounded SCXML declarations");
        goto cleanup;
    }
    status = emit_state(&build, root, 0u, true);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    qsort(build.state_names, build.state_name_index,
          sizeof(*build.state_names), compare_name_ref);
    {
        const scxml_name_ref *duplicate = find_earliest_duplicate(
            build.state_names, build.state_name_index);
        if (duplicate != NULL) {
            status = scxml_fail(&build, CFLOW_SCXML_DUPLICATE_ID,
                                duplicate->location,
                                "duplicate SCXML state id");
            goto cleanup;
        }
    }
    qsort(build.node_refs, build.node_ref_index, sizeof(*build.node_refs),
          compare_node_ref);
    status = collect_transition_events(&build, root);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    status = build_event_names(&build, build.event_occurrence_index);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    status = emit_transitions(&build, root, build.node_ref_index,
                              build.synthetic_index);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    for (index = 0u; index < build.state_name_index; ++index) {
        if (!checked_add(name_bytes, build.state_names[index].name.size,
                         &name_bytes)) {
            status = scxml_fail(&build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                turbo_xml_node_location(root),
                                "retained SCXML name size overflow");
            goto cleanup;
        }
    }
    for (index = 0u; index < build.event_name_count; ++index) {
        if (!checked_add(name_bytes, build.event_names[index].name.size,
                         &name_bytes)) {
            status = scxml_fail(&build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                turbo_xml_node_location(root),
                                "retained SCXML name size overflow");
            goto cleanup;
        }
    }
    if (name_bytes > limits.max_name_bytes) {
        status = scxml_fail(&build, CFLOW_SCXML_LIMIT_EXCEEDED,
                            turbo_xml_node_location(root),
                            "retained SCXML names exceed max_name_bytes");
        goto cleanup;
    }

    memset(&definition, 0, sizeof(definition));
    definition.state_type = &cmeta_type_bool;
    definition.states = build.states;
    definition.state_count = build.state_index;
    definition.events = build.events;
    definition.event_count = build.event_name_count;
    definition.transitions = build.transitions;
    definition.transition_count = build.transition_index;
    impl = (cflow_scxml_program_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) {
        status = scxml_fail(&build, CFLOW_SCXML_ALLOCATION_FAILED,
                            turbo_xml_node_location(root),
                            "unable to allocate SCXML program");
        goto cleanup;
    }
    native_status = cflow_statechart_build(&impl->statechart, &definition);
    if (native_status != CFLOW_STATECHART_OK) {
        char message[CFLOW_SCXML_DIAGNOSTIC_CAPACITY];
        (void)snprintf(message, sizeof(message),
                       "native Statechart rejected SCXML lowering (status=%d)",
                       (int)native_status);
        status = scxml_fail(&build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                            turbo_xml_node_location(root), message);
        goto cleanup;
    }
    impl->state_names = allocate_rows(build.state_name_index,
                                      sizeof(*impl->state_names));
    impl->event_names = allocate_rows(build.event_name_count,
                                      sizeof(*impl->event_names));
    impl->name_storage = name_bytes != 0u ? (char *)malloc(name_bytes) : NULL;
    if ((build.state_name_index != 0u && impl->state_names == NULL) ||
        (build.event_name_count != 0u && impl->event_names == NULL) ||
        (name_bytes != 0u && impl->name_storage == NULL)) {
        status = scxml_fail(&build, CFLOW_SCXML_ALLOCATION_FAILED,
                            turbo_xml_node_location(root),
                            "unable to retain SCXML name mappings");
        goto cleanup;
    }
    name_cursor = impl->name_storage;
    copy_program_names(impl->state_names, build.state_names,
                       build.state_name_index, &name_cursor);
    copy_program_names(impl->event_names, build.event_names,
                       build.event_name_count, &name_cursor);
    impl->state_name_count = build.state_name_index;
    impl->event_name_count = build.event_name_count;
    impl->null_value = false;
    qsort(impl->state_names, impl->state_name_count,
          sizeof(*impl->state_names), compare_program_name);
    qsort(impl->event_names, impl->event_name_count,
          sizeof(*impl->event_names), compare_program_name);
    out->impl = impl;
    impl = NULL;
    status = CFLOW_SCXML_OK;

cleanup:
    if (impl != NULL) {
        cflow_statechart_destroy(&impl->statechart);
        free(impl->state_names);
        free(impl->event_names);
        free(impl->name_storage);
        free(impl);
    }
    free_build(&build);
    turbo_xml_document_destroy(&document);
    return status;
}

void cflow_scxml_program_destroy(cflow_scxml_program *program) {
    cflow_scxml_program_impl *impl;
    if (program == NULL || program->impl == NULL) return;
    impl = (cflow_scxml_program_impl *)program->impl;
    cflow_statechart_destroy(&impl->statechart);
    free(impl->state_names);
    free(impl->event_names);
    free(impl->name_storage);
    free(impl);
    program->impl = NULL;
}

const cflow_statechart *cflow_scxml_program_statechart(
    const cflow_scxml_program *program) {
    const cflow_scxml_program_impl *impl =
        program != NULL ? (const cflow_scxml_program_impl *)program->impl : NULL;
    return impl != NULL ? &impl->statechart : NULL;
}

static const scxml_program_name *find_program_name(
    const scxml_program_name *names, size_t count,
    const char *name, size_t name_size) {
    size_t low = 0u;
    size_t high = count;
    const turbo_xml_string_view wanted = {name, name_size};
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        const turbo_xml_string_view value = {
            names[middle].name, names[middle].size};
        if (compare_view(value, wanted) < 0) low = middle + 1u;
        else high = middle;
    }
    return low < count && names[low].size == name_size &&
                   memcmp(names[low].name, name, name_size) == 0
               ? &names[low]
               : NULL;
}

bool cflow_scxml_program_state_id(const cflow_scxml_program *program,
                                  const char *name, size_t name_size,
                                  cflow_machine_state_id *out_id) {
    const cflow_scxml_program_impl *impl;
    const scxml_program_name *found;
    if (program == NULL || program->impl == NULL || name == NULL ||
        name_size == 0u || out_id == NULL) return false;
    impl = (const cflow_scxml_program_impl *)program->impl;
    found = find_program_name(impl->state_names, impl->state_name_count,
                              name, name_size);
    if (found == NULL) return false;
    *out_id = (cflow_machine_state_id)found->id;
    return true;
}

bool cflow_scxml_program_event_id(const cflow_scxml_program *program,
                                  const char *name, size_t name_size,
                                  cflow_event_id *out_id) {
    const cflow_scxml_program_impl *impl;
    const scxml_program_name *found;
    if (program == NULL || program->impl == NULL || name == NULL ||
        name_size == 0u || out_id == NULL) return false;
    impl = (const cflow_scxml_program_impl *)program->impl;
    found = find_program_name(impl->event_names, impl->event_name_count,
                              name, name_size);
    if (found == NULL) return false;
    *out_id = (cflow_event_id)found->id;
    return true;
}

const void *cflow_scxml_program_initial_state(
    const cflow_scxml_program *program) {
    const cflow_scxml_program_impl *impl =
        program != NULL ? (const cflow_scxml_program_impl *)program->impl : NULL;
    return impl != NULL ? &impl->null_value : NULL;
}

bool cflow_scxml_program_event(const cflow_scxml_program *program,
                               const char *name, size_t name_size,
                               cflow_event_view *out_event) {
    const cflow_scxml_program_impl *impl;
    cflow_event_id id;
    if (out_event == NULL ||
        !cflow_scxml_program_event_id(program, name, name_size, &id)) {
        return false;
    }
    impl = (const cflow_scxml_program_impl *)program->impl;
    *out_event = (cflow_event_view){id, &cmeta_type_bool, &impl->null_value};
    return true;
}
