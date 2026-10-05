#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../cstl/include/cstl/detail/generic_ids.h"

typedef struct cmeta_lower_buffer {
    char *data;
    size_t size;
    size_t capacity;
} cmeta_lower_buffer;

typedef struct cmeta_lower_type {
    char owner_id[64];
    char concrete[128];
    char lifecycle_accessor[160];
} cmeta_lower_type;

typedef enum cmeta_lower_ownership_state {
    CMETA_LOWER_OWNERSHIP_NONE = 0,
    CMETA_LOWER_OWNERSHIP_LIVE_OWNED,
    CMETA_LOWER_OWNERSHIP_MOVED
} cmeta_lower_ownership_state;

typedef struct cmeta_lower_symbol {
    char name[128];
    char concrete[128];
    char lifecycle_accessor[160];
    unsigned depth;
    cmeta_lower_ownership_state ownership;
} cmeta_lower_symbol;

typedef struct cmeta_lower_context {
    const char *source;
    size_t source_size;
    const char *input_path;
    cmeta_lower_type *types;
    size_t type_count;
    size_t type_capacity;
    cmeta_lower_symbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    char error[256];
    size_t error_offset;
} cmeta_lower_context;

static int cmeta_lower_ident_start(char ch) {
    unsigned char value = (unsigned char)ch;
    return isalpha(value) || ch == '_';
}

static int cmeta_lower_ident_continue(char ch) {
    unsigned char value = (unsigned char)ch;
    return isalnum(value) || ch == '_';
}

static int cmeta_lower_buffer_reserve(cmeta_lower_buffer *buffer, size_t extra) {
    size_t required;
    size_t capacity;
    char *next;

    if (extra > (size_t)-1 - buffer->size - 1u)
        return 0;
    required = buffer->size + extra + 1u;
    if (required <= buffer->capacity)
        return 1;

    capacity = buffer->capacity == 0u ? 4096u : buffer->capacity;
    while (capacity < required) {
        if (capacity > (size_t)-1 / 2u) {
            capacity = required;
            break;
        }
        capacity *= 2u;
    }

    next = (char *)realloc(buffer->data, capacity);
    if (next == NULL)
        return 0;
    buffer->data = next;
    buffer->capacity = capacity;
    return 1;
}

static int cmeta_lower_buffer_append(
    cmeta_lower_buffer *buffer, const char *data, size_t size) {
    if (!cmeta_lower_buffer_reserve(buffer, size))
        return 0;
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    buffer->data[buffer->size] = '\0';
    return 1;
}

static int cmeta_lower_buffer_puts(
    cmeta_lower_buffer *buffer, const char *text) {
    return cmeta_lower_buffer_append(buffer, text, strlen(text));
}

static void cmeta_lower_set_error(
    cmeta_lower_context *context, size_t offset, const char *message) {
    if (context->error[0] != '\0')
        return;
    context->error_offset = offset;
    (void)snprintf(context->error, sizeof(context->error), "%s", message);
}

static void cmeta_lower_set_errorf(
    cmeta_lower_context *context, size_t offset,
    const char *prefix, const char *value, const char *suffix) {
    if (context->error[0] != '\0')
        return;
    context->error_offset = offset;
    (void)snprintf(
        context->error, sizeof(context->error),
        "%s%s%s", prefix, value, suffix);
}

static size_t cmeta_lower_skip_quoted(
    const char *source, size_t size, size_t offset, char quote) {
    size_t i = offset + 1u;
    while (i < size) {
        if (source[i] == '\\' && i + 1u < size) {
            i += 2u;
            continue;
        }
        if (source[i] == quote)
            return i + 1u;
        ++i;
    }
    return size;
}

static size_t cmeta_lower_skip_line_comment(
    const char *source, size_t size, size_t offset) {
    size_t i = offset + 2u;
    while (i < size && source[i] != '\n')
        ++i;
    return i;
}

static size_t cmeta_lower_skip_block_comment(
    const char *source, size_t size, size_t offset) {
    size_t i = offset + 2u;
    while (i + 1u < size) {
        if (source[i] == '*' && source[i + 1u] == '/')
            return i + 2u;
        ++i;
    }
    return size;
}

static int cmeta_lower_preprocessor_start(
    const char *source, size_t offset) {
    size_t i = offset;
    if (source[offset] != '#')
        return 0;
    while (i != 0u) {
        char ch = source[i - 1u];
        if (ch == '\n')
            break;
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\f' && ch != '\v')
            return 0;
        --i;
    }
    return 1;
}

static size_t cmeta_lower_skip_preprocessor(
    const char *source, size_t size, size_t offset) {
    size_t i = offset;
    while (i < size) {
        if (source[i] == '\\') {
            if (i + 1u < size && source[i + 1u] == '\n') {
                i += 2u;
                continue;
            }
            if (i + 2u < size &&
                source[i + 1u] == '\r' && source[i + 2u] == '\n') {
                i += 3u;
                continue;
            }
        }
        if (source[i] == '\n')
            return i + 1u;
        ++i;
    }
    return size;
}

static size_t cmeta_lower_skip_space_comments(
    const char *source, size_t size, size_t offset) {
    size_t i = offset;
    for (;;) {
        while (i < size && isspace((unsigned char)source[i]))
            ++i;
        if (i + 1u < size && source[i] == '/' && source[i + 1u] == '/') {
            i = cmeta_lower_skip_line_comment(source, size, i);
            continue;
        }
        if (i + 1u < size && source[i] == '/' && source[i + 1u] == '*') {
            i = cmeta_lower_skip_block_comment(source, size, i);
            continue;
        }
        return i;
    }
}

static size_t cmeta_lower_skip_space(
    const char *source, size_t size, size_t offset) {
    size_t i = offset;
    while (i < size && isspace((unsigned char)source[i]))
        ++i;
    return i;
}

static int cmeta_lower_identifier(
    const char *source, size_t size, size_t offset,
    char *out, size_t capacity, size_t *end) {
    size_t i = offset;
    size_t length;

    if (i >= size || !cmeta_lower_ident_start(source[i]))
        return 0;
    ++i;
    while (i < size && cmeta_lower_ident_continue(source[i]))
        ++i;

    length = i - offset;
    if (length + 1u > capacity)
        return 0;
    memcpy(out, source + offset, length);
    out[length] = '\0';
    if (end != NULL)
        *end = i;
    return 1;
}

typedef struct cmeta_lower_generic_owner {
    const char *token;
    const char *stable_id;
} cmeta_lower_generic_owner;

#define CMETA_LOWER_GENERIC_OWNER_ROW(kind) \
    { #kind, CSTL_GENERIC_STABLE_ID(kind) },

static const cmeta_lower_generic_owner cmeta_lower_generic_owners[] = {
    CSTL_GENERIC_OWNER_KINDS(CMETA_LOWER_GENERIC_OWNER_ROW)
};

#undef CMETA_LOWER_GENERIC_OWNER_ROW

static const char *cmeta_lower_container_owner_id(const char *owner) {
    size_t i;
    if (owner == NULL) return NULL;
    for (i = 0u;
         i < sizeof(cmeta_lower_generic_owners) /
                 sizeof(cmeta_lower_generic_owners[0]);
         ++i) {
        if (strcmp(owner, cmeta_lower_generic_owners[i].token) == 0)
            return cmeta_lower_generic_owners[i].stable_id;
    }
    return NULL;
}

static const cmeta_lower_type *
cmeta_lower_find_type(const cmeta_lower_context *context, const char *concrete) {
    size_t i;
    for (i = context->type_count; i != 0u; --i)
        if (strcmp(context->types[i - 1u].concrete, concrete) == 0)
            return &context->types[i - 1u];
    return NULL;
}

static cmeta_lower_type *
cmeta_lower_find_type_mutable(
    cmeta_lower_context *context, const char *concrete) {
    size_t i;
    for (i = context->type_count; i != 0u; --i)
        if (strcmp(context->types[i - 1u].concrete, concrete) == 0)
            return &context->types[i - 1u];
    return NULL;
}

static int cmeta_lower_reserve_type(
    cmeta_lower_context *context, size_t offset,
    const char *concrete, cmeta_lower_type **out) {
    cmeta_lower_type *next;
    size_t capacity;

    if (out != NULL) *out = NULL;
    if (context->type_count == context->type_capacity) {
        capacity = context->type_capacity == 0u
                       ? 16u
                       : context->type_capacity * 2u;
        next = (cmeta_lower_type *)realloc(
            context->types, capacity * sizeof(*next));
        if (next == NULL) {
            cmeta_lower_set_error(context, offset, "out of memory");
            return 0;
        }
        context->types = next;
        context->type_capacity = capacity;
    }

    next = &context->types[context->type_count++];
    memset(next, 0, sizeof(*next));
    (void)snprintf(next->concrete, sizeof(next->concrete), "%s", concrete);
    if (out != NULL) *out = next;
    return 1;
}

static int cmeta_lower_owner_registered(
    const cmeta_lower_context *context, const char *owner_id) {
    size_t i;
    if (owner_id == NULL) return 0;
    for (i = 0u; i < context->type_count; ++i)
        if (strcmp(context->types[i].owner_id, owner_id) == 0)
            return 1;
    return 0;
}

static int cmeta_lower_add_type(
    cmeta_lower_context *context, size_t offset,
    const char *owner_id, const char *concrete) {
    cmeta_lower_type *entry =
        cmeta_lower_find_type_mutable(context, concrete);
    char default_accessor[160];

    (void)snprintf(
        default_accessor, sizeof(default_accessor),
        "%s_cmeta_data", concrete);

    if (entry == NULL) {
        if (!cmeta_lower_reserve_type(context, offset, concrete, &entry))
            return 0;
    }

    if (entry->owner_id[0] != '\0' &&
        strcmp(entry->owner_id, owner_id) != 0) {
        cmeta_lower_set_errorf(
            context, offset, "typed concrete type '", concrete,
            "' is registered with conflicting owners");
        return 0;
    }
    if (entry->lifecycle_accessor[0] != '\0' &&
        strcmp(entry->lifecycle_accessor, default_accessor) != 0) {
        cmeta_lower_set_errorf(
            context, offset, "typed concrete type '", concrete,
            "' conflicts with its canonical lifecycle binding");
        return 0;
    }

    (void)snprintf(
        entry->owner_id, sizeof(entry->owner_id), "%s", owner_id);
    (void)snprintf(
        entry->lifecycle_accessor, sizeof(entry->lifecycle_accessor),
        "%s", default_accessor);
    return 1;
}

static int cmeta_lower_bind_lifecycle(
    cmeta_lower_context *context, size_t offset,
    const char *concrete, const char *accessor) {
    cmeta_lower_type *entry =
        cmeta_lower_find_type_mutable(context, concrete);

    if (entry == NULL) {
        if (!cmeta_lower_reserve_type(context, offset, concrete, &entry))
            return 0;
    }

    if (entry->lifecycle_accessor[0] != '\0' &&
        strcmp(entry->lifecycle_accessor, accessor) != 0) {
        cmeta_lower_set_errorf(
            context, offset, "lifecycle binding for type '", concrete,
            "' conflicts with an existing canonical accessor");
        return 0;
    }

    (void)snprintf(
        entry->lifecycle_accessor, sizeof(entry->lifecycle_accessor),
        "%s", accessor);
    return 1;
}

static int cmeta_lower_parse_typed(
    cmeta_lower_context *context, size_t typed_end) {
    const char *source = context->source;
    size_t size = context->source_size;
    size_t i = cmeta_lower_skip_space_comments(source, size, typed_end);
    size_t end;
    char owner[64];
    char concrete[128];
    const char *owner_id;

    if (i >= size || source[i] != '(')
        return 1;
    i = cmeta_lower_skip_space_comments(source, size, i + 1u);
    if (!cmeta_lower_identifier(source, size, i, owner, sizeof(owner), &end))
        return 1;
    i = cmeta_lower_skip_space_comments(source, size, end);
    if (i >= size || source[i] != ',')
        return 1;
    i = cmeta_lower_skip_space_comments(source, size, i + 1u);
    if (!cmeta_lower_identifier(
            source, size, i, concrete, sizeof(concrete), &end))
        return 1;

    owner_id = cmeta_lower_container_owner_id(owner);
    if (owner_id == NULL)
        return 1;
    return cmeta_lower_add_type(context, i, owner_id, concrete);
}

static int cmeta_lower_parse_lifecycle(
    cmeta_lower_context *context, size_t marker_end) {
    const char *source = context->source;
    size_t size = context->source_size;
    size_t i = cmeta_lower_skip_space_comments(source, size, marker_end);
    size_t end;
    char concrete[128];
    char accessor[160];

    if (i >= size || source[i] != '(') {
        cmeta_lower_set_error(
            context, marker_end,
            "CMETA_LIFECYCLE requires (Type, accessor)");
        return 0;
    }
    i = cmeta_lower_skip_space_comments(source, size, i + 1u);
    if (!cmeta_lower_identifier(
            source, size, i, concrete, sizeof(concrete), &end)) {
        cmeta_lower_set_error(
            context, marker_end,
            "CMETA_LIFECYCLE requires a simple named type");
        return 0;
    }
    i = cmeta_lower_skip_space_comments(source, size, end);
    if (i >= size || source[i] != ',') {
        cmeta_lower_set_error(
            context, marker_end,
            "CMETA_LIFECYCLE requires an explicit accessor");
        return 0;
    }
    i = cmeta_lower_skip_space_comments(source, size, i + 1u);
    if (!cmeta_lower_identifier(
            source, size, i, accessor, sizeof(accessor), &end)) {
        cmeta_lower_set_error(
            context, marker_end,
            "CMETA_LIFECYCLE accessor must be a simple identifier");
        return 0;
    }
    i = cmeta_lower_skip_space_comments(source, size, end);
    if (i >= size || source[i] != ')') {
        cmeta_lower_set_error(
            context, marker_end,
            "CMETA_LIFECYCLE requires exactly Type and accessor");
        return 0;
    }

    return cmeta_lower_bind_lifecycle(
        context, marker_end, concrete, accessor);
}

static int cmeta_lower_collect_types(cmeta_lower_context *context) {
    const char *source = context->source;
    size_t size = context->source_size;
    size_t i = 0u;

    while (i < size) {
        size_t end;
        char ident[128];

        if (cmeta_lower_preprocessor_start(source, i)) {
            i = cmeta_lower_skip_preprocessor(source, size, i);
            continue;
        }
        if (i + 1u < size && source[i] == '/' && source[i + 1u] == '/') {
            i = cmeta_lower_skip_line_comment(source, size, i);
            continue;
        }
        if (i + 1u < size && source[i] == '/' && source[i + 1u] == '*') {
            i = cmeta_lower_skip_block_comment(source, size, i);
            continue;
        }
        if (source[i] == '"' || source[i] == '\'') {
            i = cmeta_lower_skip_quoted(source, size, i, source[i]);
            continue;
        }
        if (cmeta_lower_ident_start(source[i]) &&
            cmeta_lower_identifier(source, size, i, ident, sizeof(ident), &end)) {
            if (strcmp(ident, "typed") == 0 &&
                !cmeta_lower_parse_typed(context, end))
                return 0;
            if (strcmp(ident, "CMETA_LIFECYCLE") == 0 &&
                !cmeta_lower_parse_lifecycle(context, end))
                return 0;
            i = end;
            continue;
        }
        ++i;
    }
    return context->error[0] == '\0';
}

static const cmeta_lower_symbol *
cmeta_lower_find_symbol(const cmeta_lower_context *context, const char *name) {
    size_t i;
    for (i = context->symbol_count; i != 0u; --i)
        if (strcmp(context->symbols[i - 1u].name, name) == 0)
            return &context->symbols[i - 1u];
    return NULL;
}

static cmeta_lower_symbol *
cmeta_lower_find_symbol_mutable(cmeta_lower_context *context, const char *name) {
    size_t i;
    for (i = context->symbol_count; i != 0u; --i)
        if (strcmp(context->symbols[i - 1u].name, name) == 0)
            return &context->symbols[i - 1u];
    return NULL;
}

static int cmeta_lower_add_symbol(
    cmeta_lower_context *context, size_t offset,
    const char *name, const char *concrete, unsigned depth,
    cmeta_lower_ownership_state ownership) {
    cmeta_lower_symbol *next;
    size_t capacity;

    if (context->symbol_count == context->symbol_capacity) {
        capacity = context->symbol_capacity == 0u ? 32u : context->symbol_capacity * 2u;
        next = (cmeta_lower_symbol *)realloc(
            context->symbols, capacity * sizeof(*next));
        if (next == NULL) {
            cmeta_lower_set_error(context, offset, "out of memory");
            return 0;
        }
        context->symbols = next;
        context->symbol_capacity = capacity;
    }

    (void)snprintf(
        context->symbols[context->symbol_count].name,
        sizeof(context->symbols[context->symbol_count].name), "%s", name);
    (void)snprintf(
        context->symbols[context->symbol_count].concrete,
        sizeof(context->symbols[context->symbol_count].concrete),
        "%s", concrete);
    context->symbols[context->symbol_count].lifecycle_accessor[0] = '\0';
    context->symbols[context->symbol_count].depth = depth;
    context->symbols[context->symbol_count].ownership = ownership;
    ++context->symbol_count;
    return 1;
}

static void cmeta_lower_leave_scope(
    cmeta_lower_context *context, unsigned depth) {
    while (context->symbol_count != 0u &&
           context->symbols[context->symbol_count - 1u].depth >= depth)
        --context->symbol_count;
}

static const cmeta_lower_symbol *
cmeta_lower_find_live_owned_from_depth(
    const cmeta_lower_context *context, unsigned minimum_depth) {
    size_t i;
    for (i = context->symbol_count; i != 0u; --i) {
        const cmeta_lower_symbol *symbol = &context->symbols[i - 1u];
        if (symbol->depth < minimum_depth)
            break;
        if (symbol->ownership == CMETA_LOWER_OWNERSHIP_LIVE_OWNED)
            return symbol;
    }
    return NULL;
}

static int cmeta_lower_emit_symbol_cleanup(
    cmeta_lower_context *context, cmeta_lower_buffer *output,
    const cmeta_lower_symbol *symbol, size_t offset) {
    char cleanup[512];

    if (symbol == NULL ||
        symbol->ownership != CMETA_LOWER_OWNERSHIP_LIVE_OWNED)
        return 1;
    if (symbol->lifecycle_accessor[0] == '\0') {
        cmeta_lower_set_errorf(
            context, offset, "owned value '", symbol->name,
            "' has no canonical lifecycle binding");
        return 0;
    }

    (void)snprintf(
        cleanup, sizeof(cleanup),
        "cmeta_data_value_destroy(%s(),&%s);",
        symbol->lifecycle_accessor, symbol->name);
    if (!cmeta_lower_buffer_puts(output, cleanup)) {
        cmeta_lower_set_error(context, offset, "out of memory");
        return 0;
    }
    return 1;
}

static int cmeta_lower_emit_cleanup_from_depth(
    cmeta_lower_context *context, cmeta_lower_buffer *output,
    unsigned minimum_depth, size_t offset) {
    size_t i = context->symbol_count;

    while (i != 0u) {
        const cmeta_lower_symbol *symbol = &context->symbols[i - 1u];
        if (symbol->depth < minimum_depth)
            break;
        if (!cmeta_lower_emit_symbol_cleanup(
                context, output, symbol, offset))
            return 0;
        --i;
    }
    return 1;
}

static int cmeta_lower_control_keyword(const char *ident) {
    return strcmp(ident, "if") == 0 ||
           strcmp(ident, "else") == 0 ||
           strcmp(ident, "for") == 0 ||
           strcmp(ident, "while") == 0 ||
           strcmp(ident, "do") == 0 ||
           strcmp(ident, "switch") == 0 ||
           strcmp(ident, "case") == 0 ||
           strcmp(ident, "default") == 0 ||
           strcmp(ident, "goto") == 0 ||
           strcmp(ident, "break") == 0 ||
           strcmp(ident, "continue") == 0 ||
           strcmp(ident, "return") == 0;
}

static int cmeta_lower_reject_live_owned_control(
    cmeta_lower_context *context, size_t offset, const char *ident) {
    const cmeta_lower_symbol *owned =
        cmeta_lower_find_live_owned_from_depth(context, 1u);
    char message[256];

    if (owned == NULL || !cmeta_lower_control_keyword(ident))
        return 1;

    if (strcmp(ident, "return") == 0) {
        (void)snprintf(
            message, sizeof(message),
            "early return exits live owned value '%s'", owned->name);
    } else if (strcmp(ident, "break") == 0 ||
               strcmp(ident, "continue") == 0 ||
               strcmp(ident, "goto") == 0) {
        (void)snprintf(
            message, sizeof(message),
            "%s exits or crosses live owned value '%s'",
            ident, owned->name);
    } else {
        (void)snprintf(
            message, sizeof(message),
            "owned value '%s' crosses unsupported control flow '%s'",
            owned->name, ident);
    }
    cmeta_lower_set_error(context, offset, message);
    return 0;
}

static int cmeta_lower_register_declaration(
    cmeta_lower_context *context, size_t type_end,
    const char *concrete, unsigned depth) {
    const char *source = context->source;
    size_t size = context->source_size;
    size_t i = cmeta_lower_skip_space_comments(source, size, type_end);
    size_t end;
    char name[128];

    if (!cmeta_lower_identifier(source, size, i, name, sizeof(name), &end))
        return 1;
    return cmeta_lower_add_symbol(
        context, i, name, concrete, depth, CMETA_LOWER_OWNERSHIP_NONE);
}

static int cmeta_lower_qualifier(const char *ident) {
    return strcmp(ident, "const") == 0 ||
           strcmp(ident, "volatile") == 0 ||
           strcmp(ident, "static") == 0 ||
           strcmp(ident, "register") == 0 ||
           strcmp(ident, "auto") == 0;
}

static int cmeta_lower_try_owned_declaration(
    cmeta_lower_context *context, cmeta_lower_buffer *output,
    size_t ident_start, size_t ident_end, unsigned depth,
    size_t *next_offset) {
    const char *source = context->source;
    size_t size = context->source_size;
    size_t i = cmeta_lower_skip_space_comments(source, size, ident_end);
    size_t type_end;
    size_t name_start;
    size_t name_end;
    size_t suffix;
    size_t semicolon;
    char type_name[128];
    char name[128];
    char declaration[320];
    const cmeta_lower_type *owned_type;
    cmeta_lower_symbol *owned_symbol;

    if (ident_end - ident_start != strlen("owned") ||
        strncmp(source + ident_start, "owned", strlen("owned")) != 0)
        return 0;
    if (i >= size || source[i] != '(') {
        cmeta_lower_set_error(
            context, ident_start, "owned declaration requires owned(Type)");
        return -1;
    }

    i = cmeta_lower_skip_space_comments(source, size, i + 1u);
    if (!cmeta_lower_identifier(
            source, size, i, type_name, sizeof(type_name), &type_end)) {
        cmeta_lower_set_error(
            context, ident_start, "owned(...) requires a simple named type");
        return -1;
    }

    i = cmeta_lower_skip_space_comments(source, size, type_end);
    if (i >= size || source[i] != ')') {
        cmeta_lower_set_error(
            context, ident_start, "owned(...) requires exactly one named type");
        return -1;
    }

    name_start = cmeta_lower_skip_space_comments(source, size, i + 1u);
    if (!cmeta_lower_identifier(
            source, size, name_start, name, sizeof(name), &name_end)) {
        cmeta_lower_set_error(
            context, ident_start, "owned(...) declaration requires a variable name");
        return -1;
    }

    suffix = cmeta_lower_skip_space_comments(source, size, name_end);
    if (suffix >= size) {
        cmeta_lower_set_error(
            context, ident_start, "unterminated owned declaration");
        return -1;
    }

    if (source[suffix] == ';') {
        semicolon = suffix;
    } else if (source[suffix] == '=') {
        size_t zero = cmeta_lower_skip_space_comments(source, size, suffix + 1u);
        if (zero >= size || source[zero] != '{') {
            cmeta_lower_set_error(
                context, ident_start,
                "owned initializer must be canonical zero {0}");
            return -1;
        }
        zero = cmeta_lower_skip_space_comments(source, size, zero + 1u);
        if (zero >= size || source[zero] != '0') {
            cmeta_lower_set_error(
                context, ident_start,
                "owned initializer must be canonical zero {0}");
            return -1;
        }
        zero = cmeta_lower_skip_space_comments(source, size, zero + 1u);
        if (zero >= size || source[zero] != '}') {
            cmeta_lower_set_error(
                context, ident_start,
                "owned initializer must be canonical zero {0}");
            return -1;
        }
        semicolon = cmeta_lower_skip_space_comments(source, size, zero + 1u);
        if (semicolon >= size || source[semicolon] != ';') {
            cmeta_lower_set_error(
                context, ident_start,
                "owned initializer must end after canonical zero {0}");
            return -1;
        }
    } else {
        cmeta_lower_set_error(
            context, ident_start,
            "owned declaration requires canonical zero initialization");
        return -1;
    }

    owned_type = cmeta_lower_find_type(context, type_name);
    if (owned_type == NULL || owned_type->lifecycle_accessor[0] == '\0') {
        cmeta_lower_set_errorf(
            context, ident_start, "owned type '", type_name,
            "' has no canonical lifecycle binding");
        return -1;
    }

    if (!cmeta_lower_add_symbol(
            context, name_start, name, type_name, depth,
            CMETA_LOWER_OWNERSHIP_LIVE_OWNED))
        return -1;

    owned_symbol = cmeta_lower_find_symbol_mutable(context, name);
    if (owned_symbol == NULL) {
        cmeta_lower_set_error(
            context, ident_start, "internal ownership binding error");
        return -1;
    }
    (void)snprintf(
        owned_symbol->lifecycle_accessor,
        sizeof(owned_symbol->lifecycle_accessor), "%s",
        owned_type->lifecycle_accessor);

    (void)snprintf(
        declaration, sizeof(declaration),
        "%s %s = {0}", type_name, name);
    if (!cmeta_lower_buffer_puts(output, declaration)) {
        cmeta_lower_set_error(context, ident_start, "out of memory");
        return -1;
    }

    *next_offset = semicolon;
    return 1;
}

static int cmeta_lower_try_move(
    cmeta_lower_context *context, cmeta_lower_buffer *output,
    size_t ident_start, size_t ident_end, const char *ident,
    size_t *next_offset) {
    const char *source = context->source;
    size_t size = context->source_size;
    size_t i;
    size_t value_end;
    char value_name[128];
    cmeta_lower_symbol *symbol;

    if (strcmp(ident, "move") != 0)
        return 0;

    i = cmeta_lower_skip_space_comments(source, size, ident_end);
    if (i >= size || source[i] != '(')
        return 0;
    i = cmeta_lower_skip_space_comments(source, size, i + 1u);
    if (!cmeta_lower_identifier(
            source, size, i, value_name, sizeof(value_name), &value_end)) {
        cmeta_lower_set_error(
            context, ident_start, "move(...) requires one owned variable");
        return -1;
    }
    i = cmeta_lower_skip_space_comments(source, size, value_end);
    if (i >= size || source[i] != ')') {
        cmeta_lower_set_error(
            context, ident_start, "move(...) requires exactly one owned variable");
        return -1;
    }

    symbol = cmeta_lower_find_symbol_mutable(context, value_name);
    if (symbol == NULL ||
        symbol->ownership == CMETA_LOWER_OWNERSHIP_NONE) {
        cmeta_lower_set_errorf(
            context, ident_start, "move requires owned value '",
            value_name, "'");
        return -1;
    }
    if (symbol->ownership == CMETA_LOWER_OWNERSHIP_MOVED) {
        cmeta_lower_set_errorf(
            context, ident_start, "owned value '",
            value_name, "' moved more than once");
        return -1;
    }

    symbol->ownership = CMETA_LOWER_OWNERSHIP_MOVED;
    if (!cmeta_lower_buffer_puts(output, value_name)) {
        cmeta_lower_set_error(context, ident_start, "out of memory");
        return -1;
    }
    *next_offset = i + 1u;
    return 1;
}

static int cmeta_lower_validate_symbol_use(
    cmeta_lower_context *context, size_t offset, const char *ident) {
    const cmeta_lower_symbol *symbol =
        cmeta_lower_find_symbol(context, ident);
    if (symbol == NULL ||
        symbol->ownership != CMETA_LOWER_OWNERSHIP_MOVED)
        return 1;
    cmeta_lower_set_errorf(
        context, offset, "use of moved owned value '", ident, "'");
    return 0;
}

static int cmeta_lower_split_operation(
    const char *ident, char *owner, size_t owner_capacity,
    char *method, size_t method_capacity) {
    const char *separator = strchr(ident, '_');
    size_t owner_length;
    size_t method_length;

    if (separator == NULL || separator == ident || separator[1] == '\0')
        return 0;
    owner_length = (size_t)(separator - ident);
    method_length = strlen(separator + 1u);
    if (owner_length + 1u > owner_capacity ||
        method_length + 1u > method_capacity)
        return 0;
    memcpy(owner, ident, owner_length);
    owner[owner_length] = '\0';
    memcpy(method, separator + 1u, method_length + 1u);
    return 1;
}

static int cmeta_lower_try_generic_call(
    cmeta_lower_context *context, cmeta_lower_buffer *output,
    size_t ident_start, size_t ident_end, const char *ident,
    size_t *next_offset) {
    const char *source = context->source;
    size_t size = context->source_size;
    size_t i;
    size_t receiver_end;
    char owner[64];
    char method[128];
    char receiver[128];
    char replacement[320];
    const cmeta_lower_symbol *symbol;
    const cmeta_lower_type *type;
    const char *owner_id;

    (void)next_offset;
    if (!cmeta_lower_split_operation(
            ident, owner, sizeof(owner), method, sizeof(method)))
        return 0;
    owner_id = cmeta_lower_container_owner_id(owner);
    if (owner_id == NULL || !cmeta_lower_owner_registered(context, owner_id))
        return 0;

    i = cmeta_lower_skip_space(source, size, ident_end);
    if (i >= size || source[i] != '(')
        return 0;
    i = cmeta_lower_skip_space(source, size, i + 1u);
    if (i >= size || source[i] != '&')
        return 0;
    i = cmeta_lower_skip_space(source, size, i + 1u);
    if (!cmeta_lower_identifier(
            source, size, i, receiver, sizeof(receiver), &receiver_end))
        return 0;

    symbol = cmeta_lower_find_symbol(context, receiver);
    if (symbol == NULL) {
        cmeta_lower_set_errorf(
            context, ident_start, "unknown typed receiver '", receiver, "'");
        return -1;
    }
    type = cmeta_lower_find_type(context, symbol->concrete);
    if (type == NULL) {
        cmeta_lower_set_errorf(
            context, ident_start, "receiver type '", symbol->concrete,
            "' has no typed owner");
        return -1;
    }
    if (strcmp(type->owner_id, owner_id) != 0) {
        char message[256];
        (void)snprintf(
            message, sizeof(message),
            "generic owner identity '%s' does not match %s owner identity '%s'",
            owner_id, symbol->concrete, type->owner_id);
        cmeta_lower_set_error(context, ident_start, message);
        return -1;
    }

    (void)snprintf(
        replacement, sizeof(replacement), "%s_%s",
        symbol->concrete, method);
    if (!cmeta_lower_buffer_puts(output, replacement)) {
        cmeta_lower_set_error(context, ident_start, "out of memory");
        return -1;
    }
    return 1;
}

static int cmeta_lower_try_receiver_call(
    cmeta_lower_context *context, cmeta_lower_buffer *output,
    size_t ident_start, size_t ident_end, const char *ident,
    size_t *next_offset) {
    const char *source = context->source;
    size_t size = context->source_size;
    const cmeta_lower_symbol *symbol = cmeta_lower_find_symbol(context, ident);
    const cmeta_lower_type *type;
    size_t i;
    size_t method_end;
    size_t after_open;
    size_t after_space;
    char method[128];
    char replacement[384];

    if (symbol == NULL)
        return 0;
    type = cmeta_lower_find_type(context, symbol->concrete);
    if (type == NULL || type->owner_id[0] == '\0')
        return 0;

    i = cmeta_lower_skip_space(source, size, ident_end);
    if (i >= size || source[i] != '.')
        return 0;
    i = cmeta_lower_skip_space(source, size, i + 1u);
    if (!cmeta_lower_identifier(
            source, size, i, method, sizeof(method), &method_end))
        return 0;
    i = cmeta_lower_skip_space(source, size, method_end);
    if (i >= size || source[i] != '(')
        return 0;

    after_open = i + 1u;
    after_space = cmeta_lower_skip_space(source, size, after_open);
    if (after_space < size && source[after_space] == ')') {
        (void)snprintf(
            replacement, sizeof(replacement), "%s_%s(&%s",
            symbol->concrete, method, ident);
    } else {
        (void)snprintf(
            replacement, sizeof(replacement), "%s_%s(&%s,",
            symbol->concrete, method, ident);
    }

    if (!cmeta_lower_buffer_puts(output, replacement)) {
        cmeta_lower_set_error(context, ident_start, "out of memory");
        return -1;
    }
    *next_offset = after_open;
    return 1;
}

static int cmeta_lower_transform(
    cmeta_lower_context *context, cmeta_lower_buffer *output) {
    const char *source = context->source;
    size_t size = context->source_size;
    size_t i = 0u;
    unsigned depth = 0u;
    int statement_start = 1;

    while (i < size) {
        size_t end;
        char ident[128];

        if (cmeta_lower_preprocessor_start(source, i)) {
            end = cmeta_lower_skip_preprocessor(source, size, i);
            if (!cmeta_lower_buffer_append(output, source + i, end - i))
                goto oom;
            i = end;
            continue;
        }

        if (i + 1u < size && source[i] == '/' && source[i + 1u] == '/') {
            end = cmeta_lower_skip_line_comment(source, size, i);
            if (!cmeta_lower_buffer_append(output, source + i, end - i))
                goto oom;
            i = end;
            continue;
        }
        if (i + 1u < size && source[i] == '/' && source[i + 1u] == '*') {
            end = cmeta_lower_skip_block_comment(source, size, i);
            if (!cmeta_lower_buffer_append(output, source + i, end - i))
                goto oom;
            i = end;
            continue;
        }
        if (source[i] == '"' || source[i] == '\'') {
            end = cmeta_lower_skip_quoted(source, size, i, source[i]);
            if (!cmeta_lower_buffer_append(output, source + i, end - i))
                goto oom;
            i = end;
            continue;
        }

        if (cmeta_lower_ident_start(source[i]) &&
            cmeta_lower_identifier(source, size, i, ident, sizeof(ident), &end)) {
            const cmeta_lower_type *known_type;
            size_t next = end;
            int rewrite;

            if (statement_start && strcmp(ident, "owned") == 0) {
                rewrite = cmeta_lower_try_owned_declaration(
                    context, output, i, end, depth, &next);
                if (rewrite < 0)
                    return 0;
                if (rewrite > 0) {
                    statement_start = 0;
                    i = next;
                    continue;
                }
            }

            if (!cmeta_lower_reject_live_owned_control(context, i, ident))
                return 0;

            if (statement_start) {
                known_type = cmeta_lower_find_type(context, ident);
                if (known_type != NULL) {
                    if (!cmeta_lower_register_declaration(
                            context, end, ident, depth))
                        return 0;
                    statement_start = 0;
                } else if (!cmeta_lower_qualifier(ident)) {
                    statement_start = 0;
                }
            }

            rewrite = cmeta_lower_try_move(
                context, output, i, end, ident, &next);
            if (rewrite < 0)
                return 0;
            if (rewrite > 0) {
                i = next;
                continue;
            }

            if (!cmeta_lower_validate_symbol_use(context, i, ident))
                return 0;

            rewrite = cmeta_lower_try_receiver_call(
                context, output, i, end, ident, &next);
            if (rewrite < 0)
                return 0;
            if (rewrite > 0) {
                i = next;
                continue;
            }

            rewrite = cmeta_lower_try_generic_call(
                context, output, i, end, ident, &next);
            if (rewrite < 0)
                return 0;
            if (rewrite > 0) {
                i = end;
                continue;
            }

            if (!cmeta_lower_buffer_append(output, source + i, end - i))
                goto oom;
            i = end;
            continue;
        }

        if (cmeta_lower_find_live_owned_from_depth(context, 1u) != NULL) {
            if (source[i] == '?' ||
                (source[i] == '&' && i + 1u < size && source[i + 1u] == '&') ||
                (source[i] == '|' && i + 1u < size && source[i + 1u] == '|')) {
                cmeta_lower_set_error(
                    context, i,
                    "live owned value crosses unsupported conditional expression");
                return 0;
            }
        }

        if (source[i] == '}' && depth != 0u) {
            if (!cmeta_lower_emit_cleanup_from_depth(
                    context, output, depth, i))
                return 0;
        }

        if (!cmeta_lower_buffer_append(output, source + i, 1u))
            goto oom;

        if (source[i] == '{') {
            ++depth;
            statement_start = 1;
        } else if (source[i] == '}') {
            if (depth != 0u) {
                cmeta_lower_leave_scope(context, depth);
                --depth;
            }
            statement_start = 0;
        } else if (source[i] == ';') {
            statement_start = 1;
        } else if (!isspace((unsigned char)source[i]) && source[i] != ',') {
            if (statement_start && source[i] != '(')
                statement_start = 0;
        }
        ++i;
    }

    return context->error[0] == '\0';

oom:
    cmeta_lower_set_error(context, i, "out of memory");
    return 0;
}

static int cmeta_lower_read_file(
    const char *path, char **out_data, size_t *out_size) {
    FILE *file;
    long length;
    size_t size;
    char *data;

    file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    length = ftell(file);
    if (length < 0) {
        fclose(file);
        return 0;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    size = (size_t)length;
    data = (char *)malloc(size + 1u);
    if (data == NULL) {
        fclose(file);
        return 0;
    }
    if (size != 0u && fread(data, 1u, size, file) != size) {
        free(data);
        fclose(file);
        return 0;
    }
    if (fclose(file) != 0) {
        free(data);
        return 0;
    }
    data[size] = '\0';
    *out_data = data;
    *out_size = size;
    return 1;
}

static int cmeta_lower_write_file(
    const char *path, const char *data, size_t size) {
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return 0;
    if (size != 0u && fwrite(data, 1u, size, file) != size) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static void cmeta_lower_report_error(const cmeta_lower_context *context) {
    size_t i;
    size_t line = 1u;
    size_t column = 1u;

    for (i = 0u; i < context->error_offset && i < context->source_size; ++i) {
        if (context->source[i] == '\n') {
            ++line;
            column = 1u;
        } else {
            ++column;
        }
    }
    fprintf(
        stderr, "%s:%zu:%zu: error: %s\n",
        context->input_path, line, column,
        context->error[0] == '\0' ? "lowering failed" : context->error);
}

static int cmeta_lower_run(const char *input_path, const char *output_path) {
    cmeta_lower_context context;
    cmeta_lower_buffer output;
    char *source = NULL;
    size_t source_size = 0u;
    int ok = 0;

    memset(&context, 0, sizeof(context));
    memset(&output, 0, sizeof(output));

    if (!cmeta_lower_read_file(input_path, &source, &source_size)) {
        fprintf(stderr, "%s: error: cannot read input: %s\n",
                input_path, strerror(errno));
        goto done;
    }

    context.source = source;
    context.source_size = source_size;
    context.input_path = input_path;

    if (!cmeta_lower_collect_types(&context) ||
        !cmeta_lower_transform(&context, &output)) {
        cmeta_lower_report_error(&context);
        goto done;
    }

    if (!cmeta_lower_write_file(output_path, output.data, output.size)) {
        fprintf(stderr, "%s: error: cannot write output: %s\n",
                output_path, strerror(errno));
        goto done;
    }
    ok = 1;

done:
    free(output.data);
    free(context.types);
    free(context.symbols);
    free(source);
    return ok;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("cmeta-lower 6");
        return 0;
    }
    if (argc != 3) {
        fprintf(stderr, "usage: %s INPUT OUTPUT\n", argv[0]);
        return 2;
    }
    return cmeta_lower_run(argv[1], argv[2]) ? 0 : 1;
}
