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

static int parse_call(
    const char *source, char *receiver_name, size_t receiver_cap,
    char *method_name, size_t method_cap, poc_arg args[2], size_t *arg_count) {
    poc_cursor cursor = { source };
    size_t count = 0u;

    if (!identifier(&cursor, receiver_name, receiver_cap) ||
        !punctuation(&cursor, '.') ||
        !identifier(&cursor, method_name, method_cap) ||
        !punctuation(&cursor, '('))
        return 0;

    skip_space(&cursor);
    if (*cursor.p != ')') {
        if (!argument(&cursor, &args[count++])) return 0;
        skip_space(&cursor);
        if (*cursor.p == ',') {
            ++cursor.p;
            if (count == 2u || !argument(&cursor, &args[count++]))
                return 0;
        }
    }

    if (!punctuation(&cursor, ')') || !punctuation(&cursor, ';'))
        return 0;
    skip_space(&cursor);
    if (*cursor.p != '\0') return 0;

    *arg_count = count;
    return 1;
}

static int method_signature_ok(
    const receiver_symbol *symbol, const cmeta_function_desc *method,
    const cmeta_function_abi_desc *abi, const poc_arg args[2],
    size_t arg_count) {
    const cmeta_param_desc *receiver;
    size_t i;

    if (!cmeta_function_desc_valid(method) ||
        !cmeta_function_abi_desc_valid(abi) ||
        method->param_count != arg_count + 1u)
        return 0;

    receiver = cmeta_function_receiver(method);
    if (receiver == NULL || receiver->type == NULL ||
        receiver->type->kind != CMETA_T_POINTER ||
        !cmeta_type_equal(receiver->type->pointee, symbol->type))
        return 0;

    for (i = 1u; i < method->param_count; ++i)
        if (!cmeta_type_equal(method->params[i].type, argument_type(&args[i - 1u])))
            return 0;

    return 1;
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

static int emit_program(
    FILE *out, const receiver_symbol *symbol,
    const cmeta_function_desc *method, const poc_arg args[2],
    size_t arg_count) {
    write_prelude(out);
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
            method->name, args[0].integer, args[1].integer, args[0].integer, args[1].integer);
        break;
    default:
        return 0;
    }

    fputs("    return 0;\n}\n", out);
    return 1;
}

static int lower(const char *source, const char *output_path) {
    char receiver_name[64];
    char method_name[64];
    poc_arg args[2] = {{POC_ARG_INT, 0}, {POC_ARG_INT, 0}};
    size_t arg_count = 0u;
    const receiver_symbol *symbol;
    const cmeta_receiver_method_set *method_set;
    const cmeta_receiver_method *method_entry;
    const cmeta_function_desc *method;
    const cmeta_function_abi_desc *method_abi;
    FILE *out;

    if (!parse_call(source, receiver_name, sizeof(receiver_name),
                    method_name, sizeof(method_name), args, &arg_count)) {
        fprintf(stderr, "unsupported method-call syntax\n");
        return 0;
    }

    symbol = find_receiver(receiver_name);
    if (symbol == NULL) {
        fprintf(stderr, "unknown receiver '%s'\n", receiver_name);
        return 0;
    }

    method_set = symbol->method_set();
    if (!cmeta_receiver_method_set_valid(method_set) ||
        !cmeta_type_equal(method_set->receiver_type, symbol->type)) {
        fprintf(stderr, "%s has invalid receiver method metadata\n",
                symbol->type_name);
        return 0;
    }

    method_entry = cmeta_receiver_method_find(method_set, method_name);
    if (method_entry == NULL) {
        fprintf(stderr, "%s has no receiver method '%s'\n",
                symbol->type_name, method_name);
        return 0;
    }
    method = method_entry->function;
    method_abi = method_entry->abi;

    if (!method_signature_ok(symbol, method, method_abi, args, arg_count)) {
        fprintf(stderr, "%s.%s arguments do not match reflected signature\n",
                receiver_name, method_name);
        return 0;
    }

    out = fopen(output_path, "wb");
    if (out == NULL) {
        perror("open generated output");
        return 0;
    }
    if (!emit_program(out, symbol, method, args, arg_count)) {
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
