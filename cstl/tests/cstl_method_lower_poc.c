#include <cmeta/function.h>
#include <cmeta/method.h>
#include <cstl/typed.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typed(Vec, IntVec, int);
typed(List, IntList, int);
typed(Set, IntSet, int);
typed(Map, IntMap, int, int);

typedef struct poc_cursor {
    const char *p;
} poc_cursor;

typedef enum poc_arg_kind {
    POC_ARG_INT,
    POC_ARG_STRING
} poc_arg_kind;

typedef struct poc_arg {
    poc_arg_kind kind;
    int integer;
} poc_arg;

typedef enum poc_call_style {
    POC_CALL_RECEIVER,
    POC_CALL_GENERIC
} poc_call_style;

typedef struct poc_call {
    poc_call_style style;
    char receiver_name[64];
    char owner_name[64];
    char method_name[64];
    poc_arg args[2];
    size_t arg_count;
} poc_call;

static const cmeta_type_desc poc_string_literal_type = {
    "const char *", sizeof(const char *), _Alignof(const char *),
    CMETA_T_POINTER, &cmeta_type_int8, NULL, NULL
};

typedef const cmeta_receiver_method_set *(*receiver_method_set_fn)(void);

typedef enum receiver_kind {
    RECEIVER_VEC,
    RECEIVER_LIST,
    RECEIVER_SET,
    RECEIVER_MAP
} receiver_kind;

typedef struct receiver_symbol {
    const char *name;
    const char *type_name;
    const cmeta_type_desc *type;
    receiver_method_set_fn method_set;
    receiver_kind kind;
} receiver_symbol;

static const receiver_symbol receiver_symbols[] = {
    {"vec", "IntVec", &IntVec_cmeta_type, IntVec_receiver_method_set, RECEIVER_VEC},
    {"list", "IntList", &IntList_cmeta_type, IntList_receiver_method_set, RECEIVER_LIST},
    {"set", "IntSet", &IntSet_cmeta_type, IntSet_receiver_method_set, RECEIVER_SET},
    {"map", "IntMap", &IntMap_cmeta_type, IntMap_receiver_method_set, RECEIVER_MAP}
};

static void skip_space(poc_cursor *c) {
    while (isspace((unsigned char)*c->p)) ++c->p;
}

static int identifier(poc_cursor *c, char *out, size_t cap) {
    size_t n = 0u;
    skip_space(c);
    if (!(isalpha((unsigned char)*c->p) || *c->p == '_')) return 0;
    while (isalnum((unsigned char)*c->p) || *c->p == '_') {
        if (n + 1u >= cap) return 0;
        out[n++] = *c->p++;
    }
    out[n] = '\0';
    return 1;
}

static int punctuation(poc_cursor *c, char token) {
    skip_space(c);
    if (*c->p != token) return 0;
    ++c->p;
    return 1;
}

static int integer_literal(poc_cursor *c, int *out) {
    char *end = NULL;
    long value;
    skip_space(c);
    errno = 0;
    value = strtol(c->p, &end, 10);
    if (end == c->p || errno == ERANGE || value < INT_MIN || value > INT_MAX)
        return 0;
    c->p = end;
    *out = (int)value;
    return 1;
}

static int argument(poc_cursor *c, poc_arg *out) {
    skip_space(c);
    if (*c->p == '"') {
        ++c->p;
        while (*c->p != '\0' && *c->p != '"') {
            if (*c->p == '\\' && c->p[1] != '\0') ++c->p;
            ++c->p;
        }
        if (*c->p != '"') return 0;
        ++c->p;
        out->kind = POC_ARG_STRING;
        out->integer = 0;
        return 1;
    }
    out->kind = POC_ARG_INT;
    return integer_literal(c, &out->integer);
}

static const cmeta_type_desc *argument_type(const poc_arg *arg) {
    return arg->kind == POC_ARG_INT ? &cmeta_type_int
                                    : &poc_string_literal_type;
}

static int parse_arguments(
    poc_cursor *cursor, poc_arg args[2], size_t *arg_count) {
    size_t count = 0u;

    skip_space(cursor);
    if (*cursor->p == ')') {
        *arg_count = 0u;
        return 1;
    }

    if (!argument(cursor, &args[count++])) return 0;
    skip_space(cursor);
    if (*cursor->p == ',') {
        ++cursor->p;
        if (count == 2u || !argument(cursor, &args[count++]))
            return 0;
    }
    skip_space(cursor);
    if (*cursor->p == ',') return 0;

    *arg_count = count;
    return 1;
}

static int finish_call(poc_cursor *cursor) {
    if (!punctuation(cursor, ')') || !punctuation(cursor, ';'))
        return 0;
    skip_space(cursor);
    return *cursor->p == '\0';
}

static int split_generic_operation(
    const char *operation, char *owner, size_t owner_cap,
    char *method, size_t method_cap) {
    const char *separator = strchr(operation, '_');
    size_t owner_len;
    size_t method_len;

    if (separator == NULL || separator == operation || separator[1] == '\0')
        return 0;

    owner_len = (size_t)(separator - operation);
    method_len = strlen(separator + 1);
    if (owner_len + 1u > owner_cap || method_len + 1u > method_cap)
        return 0;

    memcpy(owner, operation, owner_len);
    owner[owner_len] = '\0';
    memcpy(method, separator + 1, method_len + 1u);
    return 1;
}

static int parse_receiver_call(const char *source, poc_call *call) {
    poc_cursor cursor = {source};

    memset(call, 0, sizeof(*call));
    call->style = POC_CALL_RECEIVER;

    if (!identifier(&cursor, call->receiver_name, sizeof(call->receiver_name)) ||
        !punctuation(&cursor, '.') ||
        !identifier(&cursor, call->method_name, sizeof(call->method_name)) ||
        !punctuation(&cursor, '(') ||
        !parse_arguments(&cursor, call->args, &call->arg_count) ||
        !finish_call(&cursor))
        return 0;

    return 1;
}

static int parse_generic_call(const char *source, poc_call *call) {
    poc_cursor cursor = {source};
    char operation[128];

    memset(call, 0, sizeof(*call));
    call->style = POC_CALL_GENERIC;

    if (!identifier(&cursor, operation, sizeof(operation)) ||
        !split_generic_operation(
            operation, call->owner_name, sizeof(call->owner_name),
            call->method_name, sizeof(call->method_name)) ||
        !punctuation(&cursor, '(') ||
        !punctuation(&cursor, '&') ||
        !identifier(&cursor, call->receiver_name, sizeof(call->receiver_name)))
        return 0;

    skip_space(&cursor);
    if (*cursor.p == ',') {
        ++cursor.p;
        if (!parse_arguments(&cursor, call->args, &call->arg_count))
            return 0;
    } else {
        call->arg_count = 0u;
    }

    return finish_call(&cursor);
}

static int parse_call(const char *source, poc_call *call) {
    if (parse_receiver_call(source, call))
        return 1;
    return parse_generic_call(source, call);
}

static int read_text(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "rb");
    size_t n;
    if (f == NULL) return 0;
    n = fread(out, 1u, cap - 1u, f);
    if (ferror(f) || !feof(f)) {
        fclose(f);
        return 0;
    }
    fclose(f);
    out[n] = '\0';
    return 1;
}

static const receiver_symbol *find_receiver(const char *name) {
    size_t i;
    for (i = 0u; i < sizeof(receiver_symbols) / sizeof(receiver_symbols[0]); ++i)
        if (strcmp(receiver_symbols[i].name, name) == 0)
            return &receiver_symbols[i];
    return NULL;
}

static void write_prelude(FILE *out) {
    fputs(
        "#include <cstl/typed.h>\n"
        "\n"
        "typed(Vec, IntVec, int);\n"
        "typed(List, IntList, int);\n"
        "typed(Set, IntSet, int);\n"
        "typed(Map, IntMap, int, int);\n"
        "\n",
        out);
}

static void write_canonical_operation(
    FILE *out, const poc_call *call, const char *owner_name) {
    size_t i;

    fprintf(out, "/* canonical operation: %s_%s(&%s",
            owner_name, call->method_name, call->receiver_name);
    for (i = 0u; i < call->arg_count; ++i) {
        if (call->args[i].kind == POC_ARG_INT)
            fprintf(out, ", %d", call->args[i].integer);
        else
            fputs(", <string>", out);
    }
    fputs(") */\n", out);
}

static int emit_program(
    FILE *out, const receiver_symbol *symbol, const poc_call *call,
    const cmeta_function_desc *method, const char *owner_name) {
    const poc_arg *args = call->args;
    size_t arg_count = call->arg_count;

    write_prelude(out);
    write_canonical_operation(out, call, owner_name);
    fputs("int main(void) {\n    int rc;\n", out);

    switch (symbol->kind) {
    case RECEIVER_VEC:
        fprintf(out,
            "    IntVec vec = {0};\n"
            "    if (IntVec_init(&vec, 4u) != STL_OK) return 10;\n"
            "    rc = %s(&vec, %d);\n"
            "    if (rc != STL_OK) { IntVec_destroy(&vec); return 11; }\n"
            "    if (IntVec_size(&vec) != 1u || IntVec_at_const(&vec, 0u) == NULL ||\n"
            "        *IntVec_at_const(&vec, 0u) != %d) { IntVec_destroy(&vec); return 12; }\n"
            "    IntVec_destroy(&vec);\n",
            method->name, args[0].integer, args[0].integer);
        break;
    case RECEIVER_LIST:
        fprintf(out,
            "    IntList list = {0};\n"
            "    if (IntList_init(&list, 4u) != STL_OK) return 20;\n"
            "    rc = %s(&list, %d);\n"
            "    if (rc != STL_OK) { IntList_destroy(&list); return 21; }\n"
            "    if (IntList_size(&list) != 1u || IntList_front_const(&list) == NULL ||\n"
            "        *IntList_front_const(&list) != %d) { IntList_destroy(&list); return 22; }\n"
            "    IntList_destroy(&list);\n",
            method->name, args[0].integer, args[0].integer);
        break;
    case RECEIVER_SET:
        fprintf(out,
            "    IntSet set = {0};\n"
            "    if (IntSet_init(&set, 4u) != STL_OK) return 30;\n"
            "    rc = %s(&set, %d);\n"
            "    if (rc != STL_OK) { IntSet_destroy(&set); return 31; }\n"
            "    if (IntSet_size(&set) != 1u || !IntSet_contains(&set, %d)) { IntSet_destroy(&set); return 32; }\n"
            "    IntSet_destroy(&set);\n",
            method->name, args[0].integer, args[0].integer);
        break;
    case RECEIVER_MAP:
        if (arg_count != 2u) return 0;
        fprintf(out,
            "    IntMap map = {0};\n"
            "    const int *stored;\n"
            "    if (IntMap_init(&map, 4u) != STL_OK) return 40;\n"
            "    rc = %s(&map, %d, %d);\n"
            "    if (rc != STL_OK) { IntMap_destroy(&map); return 41; }\n"
            "    stored = IntMap_get_const(&map, %d);\n"
            "    if (IntMap_size(&map) != 1u || stored == NULL || *stored != %d) { IntMap_destroy(&map); return 42; }\n"
            "    IntMap_destroy(&map);\n",
            method->name, args[0].integer, args[1].integer,
            args[0].integer, args[1].integer);
        break;
    default:
        return 0;
    }

    fputs("    return 0;\n}\n", out);
    return 1;
}

static int report_resolution_error(
    cmeta_receiver_resolve_status status, const receiver_symbol *symbol,
    const poc_call *call, const cmeta_receiver_method_set *method_set,
    const cmeta_receiver_resolution *resolution) {
    switch (status) {
    case CMETA_RECEIVER_RESOLVE_INVALID_METHOD_SET:
        fprintf(stderr, "%s has invalid receiver method metadata\n",
                symbol->type_name);
        break;
    case CMETA_RECEIVER_RESOLVE_RECEIVER_TYPE_MISMATCH:
        fprintf(stderr, "%s receiver type does not match method set\n",
                symbol->type_name);
        break;
    case CMETA_RECEIVER_RESOLVE_OWNER_MISMATCH:
        fprintf(stderr, "generic owner '%s' does not match %s owner '%s'\n",
                call->owner_name, symbol->type_name, method_set->owner_name);
        break;
    case CMETA_RECEIVER_RESOLVE_METHOD_NOT_FOUND:
        fprintf(stderr, "%s has no receiver method '%s'\n",
                symbol->type_name, call->method_name);
        break;
    case CMETA_RECEIVER_RESOLVE_ARITY_MISMATCH:
        fprintf(stderr, "%s.%s arity does not match reflected signature\n",
                call->receiver_name, call->method_name);
        break;
    case CMETA_RECEIVER_RESOLVE_ARGUMENT_TYPE_MISMATCH:
        fprintf(stderr,
                "%s.%s argument %zu type does not match reflected signature\n",
                call->receiver_name, call->method_name,
                resolution->argument_index);
        break;
    case CMETA_RECEIVER_RESOLVE_INVALID_ARGUMENT:
    default:
        fprintf(stderr, "invalid receiver-call semantic input\n");
        break;
    }
    return 0;
}

static int lower(const char *source, const char *output_path) {
    poc_call call;
    const receiver_symbol *symbol;
    const cmeta_receiver_method_set *method_set;
    const cmeta_type_desc *argument_types[2] = {NULL, NULL};
    cmeta_receiver_resolution resolution = CMETA_RECEIVER_RESOLUTION_INIT;
    cmeta_receiver_resolve_status resolve_status;
    const char *owner_name;
    size_t i;
    FILE *out;

    if (!parse_call(source, &call)) {
        fprintf(stderr, "unsupported method-call syntax\n");
        return 0;
    }

    symbol = find_receiver(call.receiver_name);
    if (symbol == NULL) {
        fprintf(stderr, "unknown receiver '%s'\n", call.receiver_name);
        return 0;
    }

    method_set = symbol->method_set();
    for (i = 0u; i < call.arg_count; ++i)
        argument_types[i] = argument_type(&call.args[i]);

    owner_name = call.style == POC_CALL_GENERIC ? call.owner_name : NULL;
    resolve_status = cmeta_receiver_method_resolve(
        method_set, symbol->type, owner_name, call.method_name,
        argument_types, call.arg_count, &resolution);
    if (resolve_status != CMETA_RECEIVER_RESOLVE_OK)
        return report_resolution_error(
            resolve_status, symbol, &call, method_set, &resolution);

    out = fopen(output_path, "wb");
    if (out == NULL) {
        perror("open generated output");
        return 0;
    }
    if (!emit_program(
            out, symbol, &call, resolution.method->function,
            method_set->owner_name)) {
        fclose(out);
        return 0;
    }
    if (fclose(out) != 0) return 0;
    return 1;
}

int main(int argc, char **argv) {
    char source[256];
    if (argc != 3) {
        fprintf(stderr, "usage: %s INPUT OUTPUT\n", argv[0]);
        return 2;
    }
    if (!read_text(argv[1], source, sizeof(source))) {
        fprintf(stderr, "cannot read method-call fixture\n");
        return 3;
    }
    return lower(source, argv[2]) ? 0 : 4;
}
