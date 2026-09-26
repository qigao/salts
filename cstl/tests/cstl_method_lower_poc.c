#include <cmeta/function.h>
#include <cstl/typed.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typed(List, IntList, int);

static const cmeta_type_desc poc_list_ptr_type_desc = {
    "IntList *", sizeof(IntList *), _Alignof(IntList *),
    CMETA_T_POINTER, &IntList_cmeta_type, NULL, NULL
};

static const cmeta_param_desc poc_list_add_params[] = {
    {
        sizeof(cmeta_param_desc), "self", &poc_list_ptr_type_desc,
        CMETA_PARAM_INOUT | CMETA_PARAM_BORROWED | CMETA_PARAM_RECEIVER
    },
    {
        sizeof(cmeta_param_desc), "value", &cmeta_type_int, CMETA_PARAM_IN
    }
};

static const cmeta_function_desc poc_list_add = {
    sizeof(cmeta_function_desc), "IntList_add", &cmeta_type_int,
    poc_list_add_params, 2u,
    CMETA_EFFECT_STATEFUL | CMETA_EFFECT_MAY_FAIL, CMETA_PROP_NONE
};

typedef struct poc_cursor {
    const char *p;
} poc_cursor;

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

static int lower(const char *source, const char *output_path) {
    char receiver_name[64];
    char method_name[64];
    int value;
    poc_cursor cursor = { source };
    const cmeta_param_desc *receiver;
    FILE *out;

    if (!identifier(&cursor, receiver_name, sizeof(receiver_name)) ||
        !punctuation(&cursor, '.') ||
        !identifier(&cursor, method_name, sizeof(method_name)) ||
        !punctuation(&cursor, '(') ||
        !integer_literal(&cursor, &value) ||
        !punctuation(&cursor, ')') ||
        !punctuation(&cursor, ';')) {
        fprintf(stderr, "unsupported method-call syntax\n");
        return 0;
    }
    skip_space(&cursor);
    if (*cursor.p != '\0') {
        fprintf(stderr, "trailing input after method call\n");
        return 0;
    }

    if (strcmp(receiver_name, "list") != 0) {
        fprintf(stderr, "unknown receiver '%s'\n", receiver_name);
        return 0;
    }
    if (strcmp(method_name, "add") != 0) {
        fprintf(stderr, "IntList has no receiver method '%s'\n", method_name);
        return 0;
    }
    if (!cmeta_function_desc_valid(&poc_list_add)) {
        fprintf(stderr, "invalid CMeta method descriptor\n");
        return 0;
    }

    receiver = cmeta_function_receiver(&poc_list_add);
    if (receiver == NULL || receiver->type == NULL ||
        receiver->type->kind != CMETA_T_POINTER ||
        !cmeta_type_equal(receiver->type->pointee, &IntList_cmeta_type)) {
        fprintf(stderr, "method receiver metadata does not match IntList\n");
        return 0;
    }

    out = fopen(output_path, "wb");
    if (out == NULL) {
        perror("open generated output");
        return 0;
    }

    fprintf(out,
        "#include <cstl/typed.h>\n"
        "\n"
        "typed(List, IntList, int);\n"
        "\n"
        "int main(void) {\n"
        "    IntList list = {0};\n"
        "    int rc;\n"
        "    if (IntList_init(&list, 4u) != STL_OK) return 10;\n"
        "    rc = %s(&list, %d);\n"
        "    if (rc != STL_OK) { IntList_destroy(&list); return 11; }\n"
        "    if (IntList_size(&list) != 1u) { IntList_destroy(&list); return 12; }\n"
        "    if (IntList_front_const(&list) == NULL ||\n"
        "        *IntList_front_const(&list) != %d) {\n"
        "        IntList_destroy(&list);\n"
        "        return 13;\n"
        "    }\n"
        "    IntList_destroy(&list);\n"
        "    return 0;\n"
        "}\n",
        poc_list_add.name, value, value);

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
