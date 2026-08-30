#include "cmeta_expr.h"

#include <query_vm.h>

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCXML_EXPR_DEFAULT_SOURCE_BYTES (64u * 1024u)
#define SCXML_EXPR_DEFAULT_INSTRUCTIONS 4096u
#define SCXML_EXPR_DEFAULT_OPERANDS 2048u
#define SCXML_EXPR_DEFAULT_DEPTH 64u
#define SCXML_EXPR_DEFAULT_PATH_DEPTH 32u
#define SCXML_EXPR_DEFAULT_LITERAL_BYTES (64u * 1024u)
#define SCXML_EXPR_DEFAULT_STRING_BYTES (64u * 1024u)
#define SCXML_EXPR_SINT64_UPPER_BOUND 9223372036854775808.0
#define SCXML_EXPR_UINT64_UPPER_BOUND 18446744073709551616.0

typedef enum expr_token_kind {
    EXPR_TOKEN_END = 0,
    EXPR_TOKEN_IDENT,
    EXPR_TOKEN_NUMBER,
    EXPR_TOKEN_STRING,
    EXPR_TOKEN_LPAREN,
    EXPR_TOKEN_RPAREN,
    EXPR_TOKEN_DOT,
    EXPR_TOKEN_NOT,
    EXPR_TOKEN_EQ,
    EXPR_TOKEN_NE,
    EXPR_TOKEN_LT,
    EXPR_TOKEN_LE,
    EXPR_TOKEN_GT,
    EXPR_TOKEN_GE,
    EXPR_TOKEN_AND,
    EXPR_TOKEN_OR,
    EXPR_TOKEN_INVALID
} expr_token_kind;

typedef enum expr_value_kind {
    EXPR_VALUE_BOOL = 1,
    EXPR_VALUE_SINT,
    EXPR_VALUE_UINT,
    EXPR_VALUE_FLOAT,
    EXPR_VALUE_STRING
} expr_value_kind;

typedef enum expr_operand_kind {
    EXPR_OPERAND_LOCATION = 1,
    EXPR_OPERAND_SINT,
    EXPR_OPERAND_UINT,
    EXPR_OPERAND_FLOAT,
    EXPR_OPERAND_STRING,
    EXPR_OPERAND_STATE,
    EXPR_OPERAND_SYSTEM_NAME,
    EXPR_OPERAND_SYSTEM_SESSION_ID
} expr_operand_kind;

typedef struct expr_token {
    expr_token_kind kind;
    size_t offset;
    size_t size;
} expr_token;

typedef struct expr_operand {
    expr_operand_kind kind;
    expr_value_kind value_kind;
    const cmeta_data_desc *data;
    size_t offset;
    union {
        int64_t sint;
        uint64_t uint;
        double number;
        struct {
            const char *data;
            size_t size;
        } string;
        cflow_machine_state_id state;
    } value;
} expr_operand;

typedef struct cflow_scxml_cmeta_expr_program_impl {
    const cmeta_data_desc *root;
    qvm_instruction_t *instructions;
    expr_operand *operands;
    char *literal_storage;
    uint32_t instruction_count;
    uint32_t operand_count;
    uint32_t register_count;
    expr_value_kind result_kind;
    size_t max_string_bytes;
    qvm_limits_t qvm_limits;
} cflow_scxml_cmeta_expr_program_impl;

typedef struct expr_node {
    uint16_t reg;
    expr_value_kind kind;
} expr_node;

typedef struct expr_parser {
    const char *source;
    size_t source_size;
    size_t cursor;
    expr_token token;
    const cmeta_data_desc *root;
    cflow_scxml_cmeta_expr_resolve_state_fn resolve_state;
    void *resolve_user;
    cflow_scxml_cmeta_expr_limits limits;
    cflow_scxml_cmeta_expr_diagnostic *diagnostic;
    qvm_instruction_t *instructions;
    expr_operand *operands;
    size_t instruction_count;
    size_t operand_count;
    size_t literal_bytes;
    size_t retained_string_bytes;
    char *literal_storage;
    size_t literal_storage_capacity;
    size_t literal_storage_index;
    size_t expression_depth;
    size_t max_register;
    bool emit;
    cflow_scxml_cmeta_expr_status status;
} expr_parser;

typedef struct expr_eval_context {
    const cflow_scxml_cmeta_expr_program_impl *program;
    const unsigned char *root;
    cflow_scxml_cmeta_expr_is_active_fn is_active;
    void *active_user;
    const cflow_scxml_cmeta_expr_system_values *system_values;
    bool failed;
} expr_eval_context;

static void expr_clear_diagnostic(
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    if (diagnostic == NULL) return;
    memset(diagnostic, 0, sizeof(*diagnostic));
    diagnostic->status = CFLOW_SCXML_CMETA_EXPR_OK;
}

static cflow_scxml_cmeta_expr_status expr_report(
    cflow_scxml_cmeta_expr_diagnostic *diagnostic,
    cflow_scxml_cmeta_expr_status status, size_t offset,
    const char *message) {
    if (diagnostic != NULL) {
        memset(diagnostic, 0, sizeof(*diagnostic));
        diagnostic->status = status;
        diagnostic->byte_offset = offset;
        if (message != NULL)
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "%s", message);
    }
    return status;
}

static bool parser_fail(expr_parser *parser,
                        cflow_scxml_cmeta_expr_status status,
                        size_t offset, const char *message) {
    if (parser->status == CFLOW_SCXML_CMETA_EXPR_OK) {
        parser->status = status;
        (void)expr_report(parser->diagnostic, status, offset, message);
    }
    return false;
}

static bool expr_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static bool expr_ident_start(char value) {
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z') || value == '_';
}

static bool expr_ident_continue(char value) {
    return expr_ident_start(value) || (value >= '0' && value <= '9');
}

static bool token_text_equal(const expr_parser *parser, const char *text) {
    const size_t size = strlen(text);
    return parser->token.size == size &&
           memcmp(parser->source + parser->token.offset, text, size) == 0;
}

static void parser_next(expr_parser *parser) {
    size_t begin;
    char value;
    while (parser->cursor < parser->source_size &&
           expr_space(parser->source[parser->cursor]))
        ++parser->cursor;
    begin = parser->cursor;
    parser->token.offset = begin;
    parser->token.size = 0u;
    if (begin == parser->source_size) {
        parser->token.kind = EXPR_TOKEN_END;
        return;
    }
    value = parser->source[parser->cursor++];
    if (expr_ident_start(value)) {
        while (parser->cursor < parser->source_size &&
               expr_ident_continue(parser->source[parser->cursor]))
            ++parser->cursor;
        parser->token.kind = EXPR_TOKEN_IDENT;
    } else if ((value >= '0' && value <= '9') ||
               (value == '-' && parser->cursor < parser->source_size &&
                parser->source[parser->cursor] >= '0' &&
                parser->source[parser->cursor] <= '9')) {
        bool exponent = false;
        while (parser->cursor < parser->source_size) {
            const char next = parser->source[parser->cursor];
            if (next >= '0' && next <= '9') {
                ++parser->cursor;
            } else if (next == '.' && !exponent) {
                ++parser->cursor;
            } else if ((next == 'e' || next == 'E') && !exponent) {
                exponent = true;
                ++parser->cursor;
                if (parser->cursor < parser->source_size &&
                    (parser->source[parser->cursor] == '+' ||
                     parser->source[parser->cursor] == '-'))
                    ++parser->cursor;
            } else {
                break;
            }
        }
        if (parser->cursor < parser->source_size &&
            (parser->source[parser->cursor] == 'u' ||
             parser->source[parser->cursor] == 'U'))
            ++parser->cursor;
        parser->token.kind = EXPR_TOKEN_NUMBER;
    } else if (value == '"') {
        begin = parser->cursor;
        while (parser->cursor < parser->source_size &&
               parser->source[parser->cursor] != '"' &&
               parser->source[parser->cursor] != '\\')
            ++parser->cursor;
        if (parser->cursor >= parser->source_size ||
            parser->source[parser->cursor] != '"') {
            parser->token.kind = EXPR_TOKEN_INVALID;
            parser->token.offset = begin - 1u;
            parser->token.size = parser->cursor - (begin - 1u);
            return;
        }
        parser->token.kind = EXPR_TOKEN_STRING;
        parser->token.offset = begin;
        parser->token.size = parser->cursor - begin;
        ++parser->cursor;
        return;
    } else {
        parser->token.kind = EXPR_TOKEN_INVALID;
        switch (value) {
            case '(': parser->token.kind = EXPR_TOKEN_LPAREN; break;
            case ')': parser->token.kind = EXPR_TOKEN_RPAREN; break;
            case '.': parser->token.kind = EXPR_TOKEN_DOT; break;
            case '!':
                if (parser->cursor < parser->source_size &&
                    parser->source[parser->cursor] == '=') {
                    ++parser->cursor;
                    parser->token.kind = EXPR_TOKEN_NE;
                } else parser->token.kind = EXPR_TOKEN_NOT;
                break;
            case '=':
                if (parser->cursor < parser->source_size &&
                    parser->source[parser->cursor] == '=') {
                    ++parser->cursor;
                    parser->token.kind = EXPR_TOKEN_EQ;
                }
                break;
            case '<':
                if (parser->cursor < parser->source_size &&
                    parser->source[parser->cursor] == '=') {
                    ++parser->cursor;
                    parser->token.kind = EXPR_TOKEN_LE;
                } else parser->token.kind = EXPR_TOKEN_LT;
                break;
            case '>':
                if (parser->cursor < parser->source_size &&
                    parser->source[parser->cursor] == '=') {
                    ++parser->cursor;
                    parser->token.kind = EXPR_TOKEN_GE;
                } else parser->token.kind = EXPR_TOKEN_GT;
                break;
            case '&':
                if (parser->cursor < parser->source_size &&
                    parser->source[parser->cursor] == '&') {
                    ++parser->cursor;
                    parser->token.kind = EXPR_TOKEN_AND;
                }
                break;
            case '|':
                if (parser->cursor < parser->source_size &&
                    parser->source[parser->cursor] == '|') {
                    ++parser->cursor;
                    parser->token.kind = EXPR_TOKEN_OR;
                }
                break;
        }
    }
    parser->token.offset = begin;
    parser->token.size = parser->cursor - begin;
}

static bool parser_add_literal_bytes(expr_parser *parser, size_t count) {
    if (count > parser->limits.max_literal_bytes - parser->literal_bytes)
        return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "CMeta expression literal byte limit exceeded");
    parser->literal_bytes += count;
    return true;
}

static bool parser_retain_string(expr_parser *parser, size_t offset,
                                 size_t size, expr_operand *operand) {
    if (!parser_add_literal_bytes(parser, size))
        return false;
    if (size > SIZE_MAX - parser->retained_string_bytes)
        return parser_fail(parser,
                           CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED,
                           offset,
                           "CMeta retained string byte count overflow");
    operand->kind = EXPR_OPERAND_STRING;
    operand->value_kind = EXPR_VALUE_STRING;
    operand->value.string.size = size;
    if (parser->emit) {
        if (parser->literal_storage_index > parser->literal_storage_capacity ||
            size > parser->literal_storage_capacity -
                       parser->literal_storage_index)
            return parser_fail(parser,
                               CFLOW_SCXML_CMETA_EXPR_EVALUATION_ERROR,
                               offset,
                               "CMeta string literal storage invariant failed");
        if (size != 0u) {
            operand->value.string.data =
                parser->literal_storage + parser->literal_storage_index;
            memcpy((char *)operand->value.string.data,
                   parser->source + offset, size);
        }
        parser->literal_storage_index += size;
    }
    parser->retained_string_bytes += size;
    return true;
}

static bool parser_emit_instruction(expr_parser *parser, qvm_opcode_t op,
                                    uint16_t dst, uint32_t arg,
                                    uint32_t src1, uint32_t src2) {
    if (parser->instruction_count >= parser->limits.max_instructions)
        return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "CMeta expression instruction limit exceeded");
    if (parser->emit)
        parser->instructions[parser->instruction_count] =
            (qvm_instruction_t){(uint8_t)op, 0u, dst, arg, src1, src2};
    ++parser->instruction_count;
    if ((size_t)dst + 1u > parser->max_register)
        parser->max_register = (size_t)dst + 1u;
    return true;
}

static bool parser_add_operand(expr_parser *parser, expr_operand operand,
                               uint32_t *out_index) {
    if (parser->operand_count >= parser->limits.max_operands ||
        parser->operand_count > UINT32_MAX)
        return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "CMeta expression operand limit exceeded");
    *out_index = (uint32_t)parser->operand_count;
    if (parser->emit) parser->operands[parser->operand_count] = operand;
    ++parser->operand_count;
    return true;
}

static bool desc_scalar_kind(const cmeta_data_desc *desc,
                             expr_value_kind *out_kind) {
    size_t expected;
    if (!cmeta_data_desc_valid(desc) || desc->storage_type == NULL)
        return false;
    switch (desc->kind) {
        case CMETA_DATA_BOOL:
            if (desc->storage_type->size != sizeof(bool)) return false;
            *out_kind = EXPR_VALUE_BOOL;
            return true;
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
            expected = ((const cmeta_data_integer_shape *)desc->shape)->bits / 8u;
            if (desc->storage_type->size != expected) return false;
            *out_kind = desc->kind == CMETA_DATA_SINT
                            ? EXPR_VALUE_SINT : EXPR_VALUE_UINT;
            return true;
        case CMETA_DATA_FLOAT:
            expected = ((const cmeta_data_float_shape *)desc->shape)->bits / 8u;
            if (desc->storage_type->size != expected) return false;
            *out_kind = EXPR_VALUE_FLOAT;
            return true;
        case CMETA_DATA_ENUM:
            if (cmeta_data_enum_ops_of(desc) == NULL) return false;
            *out_kind = EXPR_VALUE_SINT;
            return true;
        case CMETA_DATA_STRING: {
            const cmeta_data_buffer_ops *ops =
                cmeta_data_buffer_ops_of(desc);
            if (ops == NULL ||
                ops->struct_size <
                    offsetof(cmeta_data_buffer_ops, read) +
                        sizeof(ops->read) ||
                ops->read == NULL)
                return false;
            *out_kind = EXPR_VALUE_STRING;
            return true;
        }
        default: return false;
    }
}

static const cmeta_data_field_desc *find_field_view(
    const cmeta_data_struct_shape *shape, const char *name, size_t name_size) {
    size_t index;
    if (shape == NULL || name == NULL) return NULL;
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        if (field->name != NULL && strlen(field->name) == name_size &&
            memcmp(field->name, name, name_size) == 0)
            return field;
    }
    return NULL;
}

static bool parser_parse_or(expr_parser *, uint16_t, expr_node *);

static bool parser_parse_location(expr_parser *parser, uint16_t target,
                                  expr_node *out) {
    const cmeta_data_desc *desc = parser->root;
    size_t offset = 0u;
    size_t depth = 0u;
    expr_operand operand = {0};
    uint32_t operand_index;
    for (;;) {
        const cmeta_data_struct_shape *shape;
        const cmeta_data_field_desc *field;
        size_t next_offset;
        if (depth >= parser->limits.max_path_depth)
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED,
                               parser->token.offset,
                               "CMeta location path depth limit exceeded");
        if (!cmeta_data_desc_valid(desc) || desc->kind != CMETA_DATA_STRUCT)
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_UNKNOWN_LOCATION,
                               parser->token.offset,
                               "CMeta location traverses a non-struct value");
        shape = (const cmeta_data_struct_shape *)desc->shape;
        field = find_field_view(shape, parser->source + parser->token.offset,
                                parser->token.size);
        if (field == NULL)
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_UNKNOWN_LOCATION,
                               parser->token.offset,
                               "CMeta location field is unknown");
        if (!cmeta_data_desc_valid(field->value) ||
            field->value->storage_type == NULL ||
            field->offset > desc->storage_type->size ||
            field->value->storage_type->size >
                desc->storage_type->size - field->offset ||
            offset > SIZE_MAX - field->offset)
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "CMeta location descriptor bounds are invalid");
        next_offset = offset + field->offset;
        if (next_offset > parser->root->storage_type->size ||
            field->value->storage_type->size >
                parser->root->storage_type->size - next_offset)
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "CMeta location exceeds root storage");
        offset = next_offset;
        desc = field->value;
        ++depth;
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_DOT) break;
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_IDENT)
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "CMeta location requires a field after '.'");
    }
    if (!desc_scalar_kind(desc, &out->kind))
        return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
                           parser->token.offset,
                           "CMeta location is not a readable scalar");
    operand.kind = EXPR_OPERAND_LOCATION;
    operand.value_kind = out->kind;
    operand.data = desc;
    operand.offset = offset;
    if (!parser_add_operand(parser, operand, &operand_index) ||
        !parser_emit_instruction(parser, QVM_OP_LOAD_PATH, target, 0u,
                                 operand_index, 0u))
        return false;
    out->reg = target;
    return true;
}

static bool parse_number_operand(expr_parser *parser, expr_operand *operand) {
    char text[128];
    const char *begin = parser->source + parser->token.offset;
    size_t size = parser->token.size;
    bool unsigned_suffix = false;
    bool floating = false;
    char *end = NULL;
    size_t index;
    if (size == 0u || size >= sizeof(text))
        return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "numeric literal is too long");
    memcpy(text, begin, size);
    text[size] = '\0';
    if (text[size - 1u] == 'u' || text[size - 1u] == 'U') {
        unsigned_suffix = true;
        text[--size] = '\0';
    }
    for (index = 0u; index < size; ++index)
        if (text[index] == '.' || text[index] == 'e' || text[index] == 'E')
            floating = true;
    errno = 0;
    if (floating) {
        if (unsigned_suffix)
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "floating literal cannot use unsigned suffix");
        operand->kind = EXPR_OPERAND_FLOAT;
        operand->value_kind = EXPR_VALUE_FLOAT;
        operand->value.number = strtod(text, &end);
    } else if (unsigned_suffix) {
        if (text[0] == '-')
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "unsigned literal cannot be negative");
        operand->kind = EXPR_OPERAND_UINT;
        operand->value_kind = EXPR_VALUE_UINT;
        operand->value.uint = strtoull(text, &end, 10);
    } else {
        operand->kind = EXPR_OPERAND_SINT;
        operand->value_kind = EXPR_VALUE_SINT;
        operand->value.sint = strtoll(text, &end, 10);
    }
    if (errno == ERANGE || end == text || *end != '\0')
        return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR,
                           parser->token.offset,
                           "numeric literal is invalid or out of range");
    return parser_add_literal_bytes(parser, parser->token.size);
}

static bool parser_parse_primary(expr_parser *parser, uint16_t target,
                                 expr_node *out) {
    if (target >= QVM_MAX_REGISTERS)
        return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "CMeta expression register limit exceeded");
    if (parser->token.kind == EXPR_TOKEN_LPAREN) {
        bool ok;
        parser_next(parser);
        ok = parser_parse_or(parser, target, out);
        if (!ok) return false;
        if (parser->token.kind != EXPR_TOKEN_RPAREN)
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "CMeta expression requires ')'");
        parser_next(parser);
        return true;
    }
    if (parser->token.kind == EXPR_TOKEN_NUMBER) {
        expr_operand operand = {0};
        uint32_t operand_index;
        if (!parse_number_operand(parser, &operand)) return false;
        out->kind = operand.value_kind;
        out->reg = target;
        if (!parser_add_operand(parser, operand, &operand_index) ||
            !parser_emit_instruction(parser, QVM_OP_LOAD_CONST, target, 0u,
                                     operand_index, 0u))
            return false;
        parser_next(parser);
        return true;
    }
    if (parser->token.kind == EXPR_TOKEN_STRING) {
        expr_operand operand = {0};
        uint32_t operand_index;
        if (!parser_retain_string(parser, parser->token.offset,
                                  parser->token.size, &operand))
            return false;
        out->kind = EXPR_VALUE_STRING;
        out->reg = target;
        if (!parser_add_operand(parser, operand, &operand_index) ||
            !parser_emit_instruction(parser, QVM_OP_LOAD_CONST, target, 0u,
                                     operand_index, 0u))
            return false;
        parser_next(parser);
        return true;
    }
    if (parser->token.kind != EXPR_TOKEN_IDENT)
        return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR,
                           parser->token.offset,
                           "CMeta expression requires a value");
    if (token_text_equal(parser, "true") || token_text_equal(parser, "false")) {
        const bool value = token_text_equal(parser, "true");
        out->kind = EXPR_VALUE_BOOL;
        out->reg = target;
        parser_next(parser);
        return parser_emit_instruction(parser,
                                       value ? QVM_OP_TRUE : QVM_OP_FALSE,
                                       target, 0u, 0u, 0u);
    }
    if (token_text_equal(parser, "_name") ||
        token_text_equal(parser, "_sessionid")) {
        expr_operand operand = {0};
        uint32_t operand_index;
        operand.kind = token_text_equal(parser, "_name")
                           ? EXPR_OPERAND_SYSTEM_NAME
                           : EXPR_OPERAND_SYSTEM_SESSION_ID;
        operand.value_kind = EXPR_VALUE_STRING;
        out->kind = EXPR_VALUE_STRING;
        out->reg = target;
        parser_next(parser);
        return parser_add_operand(parser, operand, &operand_index) &&
               parser_emit_instruction(parser, QVM_OP_LOAD_CONST, target,
                                       0u, operand_index, 0u);
    }
    if (token_text_equal(parser, "In")) {
        expr_operand operand = {0};
        uint32_t operand_index;
        size_t name_offset;
        size_t name_size;
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_LPAREN)
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR,
                               parser->token.offset, "In requires '('");
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_STRING)
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "In requires one quoted state name");
        name_offset = parser->token.offset;
        name_size = parser->token.size;
        if (!parser_add_literal_bytes(parser, name_size)) return false;
        operand.kind = EXPR_OPERAND_STATE;
        operand.value_kind = EXPR_VALUE_BOOL;
        if (parser->emit &&
            !parser->resolve_state(parser->resolve_user,
                                   parser->source + name_offset, name_size,
                                   &operand.value.state))
            return parser_fail(parser,
                               CFLOW_SCXML_CMETA_EXPR_UNKNOWN_LOCATION,
                               name_offset, "In names an unknown state");
        parser_next(parser);
        if (parser->token.kind != EXPR_TOKEN_RPAREN)
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR,
                               parser->token.offset,
                               "In requires exactly one state name");
        parser_next(parser);
        if (!parser_add_operand(parser, operand, &operand_index) ||
            !parser_emit_instruction(parser, QVM_OP_LOAD_CONST, target, 0u,
                                     operand_index, 0u))
            return false;
        out->reg = target;
        out->kind = EXPR_VALUE_BOOL;
        return true;
    }
    return parser_parse_location(parser, target, out);
}

static bool parser_parse_unary(expr_parser *parser, uint16_t target,
                               expr_node *out) {
    bool negate = false;
    if (parser->expression_depth >= parser->limits.max_expression_depth)
        return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED,
                           parser->token.offset,
                           "CMeta expression depth limit exceeded");
    ++parser->expression_depth;
    if (parser->token.kind == EXPR_TOKEN_NOT) {
        negate = true;
        parser_next(parser);
        if (!parser_parse_unary(parser, target, out)) {
            --parser->expression_depth;
            return false;
        }
    } else if (!parser_parse_primary(parser, target, out)) {
        --parser->expression_depth;
        return false;
    }
    --parser->expression_depth;
    if (negate) {
        if (out->kind != EXPR_VALUE_BOOL)
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "logical not requires a Boolean operand");
        return parser_emit_instruction(parser, QVM_OP_NOT, target, 0u,
                                       target, 0u);
    }
    return true;
}

static bool value_is_numeric(expr_value_kind kind) {
    return kind == EXPR_VALUE_SINT || kind == EXPR_VALUE_UINT ||
           kind == EXPR_VALUE_FLOAT;
}

static bool parser_parse_compare(expr_parser *parser, uint16_t target,
                                 expr_node *out) {
    expr_token_kind operation;
    expr_node right;
    uint32_t comparison;
    if (!parser_parse_unary(parser, target, out)) return false;
    operation = parser->token.kind;
    if (operation < EXPR_TOKEN_EQ || operation > EXPR_TOKEN_GE) return true;
    parser_next(parser);
    if (!parser_parse_unary(parser, (uint16_t)(target + 1u), &right))
        return false;
    if (operation == EXPR_TOKEN_EQ || operation == EXPR_TOKEN_NE) {
        if (!((out->kind == EXPR_VALUE_BOOL &&
               right.kind == EXPR_VALUE_BOOL) ||
              (out->kind == EXPR_VALUE_STRING &&
               right.kind == EXPR_VALUE_STRING) ||
              (value_is_numeric(out->kind) && value_is_numeric(right.kind))))
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "equality operands have incompatible types");
    } else if (!((out->kind == EXPR_VALUE_STRING &&
                  right.kind == EXPR_VALUE_STRING) ||
                 (value_is_numeric(out->kind) &&
                  value_is_numeric(right.kind)))) {
        return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
                           parser->token.offset,
                           "ordered comparison requires numeric or string operands");
    }
    comparison = operation == EXPR_TOKEN_EQ ? 0u :
                 operation == EXPR_TOKEN_NE ? 1u :
                 operation == EXPR_TOKEN_LT ? 2u :
                 operation == EXPR_TOKEN_LE ? 3u :
                 operation == EXPR_TOKEN_GT ? 4u : 5u;
    if (!parser_emit_instruction(parser, QVM_OP_CMP, target, comparison,
                                 target, right.reg))
        return false;
    out->kind = EXPR_VALUE_BOOL;
    return true;
}

static bool parser_parse_and(expr_parser *parser, uint16_t target,
                             expr_node *out) {
    if (!parser_parse_compare(parser, target, out)) return false;
    while (parser->token.kind == EXPR_TOKEN_AND) {
        expr_node right;
        size_t jump_index;
        if (out->kind != EXPR_VALUE_BOOL)
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "logical and requires Boolean operands");
        jump_index = parser->instruction_count;
        if (!parser_emit_instruction(parser, QVM_OP_JMP_FALSE, 0u, 0u,
                                     target, 0u))
            return false;
        parser_next(parser);
        if (!parser_parse_compare(parser, (uint16_t)(target + 1u), &right))
            return false;
        if (right.kind != EXPR_VALUE_BOOL)
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "logical and requires Boolean operands");
        if (!parser_emit_instruction(parser, QVM_OP_BAND, target, 0u,
                                     target, right.reg))
            return false;
        if (parser->emit)
            parser->instructions[jump_index].arg =
                (uint32_t)parser->instruction_count;
        out->kind = EXPR_VALUE_BOOL;
    }
    return true;
}

static bool parser_parse_or(expr_parser *parser, uint16_t target,
                            expr_node *out) {
    if (!parser_parse_and(parser, target, out)) return false;
    while (parser->token.kind == EXPR_TOKEN_OR) {
        expr_node right;
        size_t jump_index;
        if (out->kind != EXPR_VALUE_BOOL)
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "logical or requires Boolean operands");
        jump_index = parser->instruction_count;
        if (!parser_emit_instruction(parser, QVM_OP_JMP_TRUE, 0u, 0u,
                                     target, 0u))
            return false;
        parser_next(parser);
        if (!parser_parse_and(parser, (uint16_t)(target + 1u), &right))
            return false;
        if (right.kind != EXPR_VALUE_BOOL)
            return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
                               parser->token.offset,
                               "logical or requires Boolean operands");
        if (!parser_emit_instruction(parser, QVM_OP_BOR, target, 0u,
                                     target, right.reg))
            return false;
        if (parser->emit)
            parser->instructions[jump_index].arg =
                (uint32_t)parser->instruction_count;
        out->kind = EXPR_VALUE_BOOL;
    }
    return true;
}

static bool parser_run(expr_parser *parser, bool require_boolean,
                       expr_value_kind *out_kind) {
    expr_node root;
    parser_next(parser);
    if (parser->token.kind == EXPR_TOKEN_INVALID)
        return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR,
                           parser->token.offset,
                           "CMeta expression contains an invalid token");
    if (!parser_parse_or(parser, 0u, &root)) return false;
    if (parser->token.kind != EXPR_TOKEN_END)
        return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR,
                           parser->token.offset,
                           "CMeta expression has trailing input");
    if (require_boolean && root.kind != EXPR_VALUE_BOOL)
        return parser_fail(parser, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
                           parser->source_size,
                           "CMeta condition result must be Boolean");
    if (out_kind != NULL) *out_kind = root.kind;
    return true;
}

cflow_scxml_cmeta_expr_limits cflow_scxml_cmeta_expr_default_limits(void) {
    const cflow_scxml_cmeta_expr_limits limits = {
        SCXML_EXPR_DEFAULT_SOURCE_BYTES,
        SCXML_EXPR_DEFAULT_INSTRUCTIONS,
        SCXML_EXPR_DEFAULT_OPERANDS,
        SCXML_EXPR_DEFAULT_DEPTH,
        SCXML_EXPR_DEFAULT_PATH_DEPTH,
        SCXML_EXPR_DEFAULT_LITERAL_BYTES,
        SCXML_EXPR_DEFAULT_STRING_BYTES};
    return limits;
}

bool cflow_scxml_cmeta_expr_limits_valid(
    const cflow_scxml_cmeta_expr_limits *limits) {
    return limits != NULL && limits->max_source_bytes != 0u &&
           limits->max_instructions != 0u &&
           limits->max_instructions <= UINT32_MAX &&
           limits->max_operands != 0u &&
           limits->max_operands <= UINT32_MAX &&
           limits->max_expression_depth != 0u &&
           limits->max_expression_depth <= QVM_MAX_REGISTERS &&
           limits->max_path_depth != 0u &&
           limits->max_literal_bytes != 0u &&
           limits->max_string_bytes != 0u;
}

static void expr_program_impl_destroy(
    cflow_scxml_cmeta_expr_program_impl *impl) {
    if (impl == NULL) return;
    free(impl->instructions);
    free(impl->operands);
    free(impl->literal_storage);
    free(impl);
}

static cflow_scxml_cmeta_expr_status expr_compile(
    cflow_scxml_cmeta_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    cflow_scxml_cmeta_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const cflow_scxml_cmeta_expr_limits *limits_or_null,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic,
    bool require_boolean) {
    const cflow_scxml_cmeta_expr_limits limits =
        limits_or_null != NULL ? *limits_or_null
                               : cflow_scxml_cmeta_expr_default_limits();
    expr_parser parser;
    cflow_scxml_cmeta_expr_program_impl *impl = NULL;
    qvm_diagnostic_t qvm_diagnostic;
    expr_value_kind admitted_kind = (expr_value_kind)0;
    expr_value_kind emitted_kind = (expr_value_kind)0;
    size_t retained_string_bytes;
    int qvm_status;
    expr_clear_diagnostic(diagnostic);
    if (out == NULL || out->impl != NULL || source == NULL || source_size == 0u ||
        !cmeta_data_desc_valid(root) || root->kind != CMETA_DATA_STRUCT ||
        root->storage_type == NULL || resolve_state == NULL ||
        !cflow_scxml_cmeta_expr_limits_valid(&limits))
        return expr_report(diagnostic, CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT,
                           0u, "invalid CMeta expression compile arguments");
    if (source_size > limits.max_source_bytes)
        return expr_report(diagnostic, CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED,
                           limits.max_source_bytes,
                           "CMeta expression source byte limit exceeded");
    memset(&parser, 0, sizeof(parser));
    parser.source = source;
    parser.source_size = source_size;
    parser.root = root;
    parser.resolve_state = resolve_state;
    parser.resolve_user = resolve_user;
    parser.limits = limits;
    parser.diagnostic = diagnostic;
    parser.status = CFLOW_SCXML_CMETA_EXPR_OK;
    if (!parser_run(&parser, require_boolean, &admitted_kind))
        return parser.status;
    retained_string_bytes = parser.retained_string_bytes;
    if (parser.instruction_count > SIZE_MAX / sizeof(*impl->instructions) ||
        parser.operand_count > SIZE_MAX / sizeof(*impl->operands))
        return expr_report(diagnostic,
                           CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED, 0u,
                           "CMeta expression storage size overflow");

    impl = (cflow_scxml_cmeta_expr_program_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return expr_report(diagnostic,
                           CFLOW_SCXML_CMETA_EXPR_ALLOCATION_FAILED, 0u,
                           "CMeta expression program allocation failed");
    impl->instructions = (qvm_instruction_t *)calloc(
        parser.instruction_count, sizeof(*impl->instructions));
    impl->operands = (expr_operand *)calloc(
        parser.operand_count, sizeof(*impl->operands));
    if (retained_string_bytes != 0u)
        impl->literal_storage =
            (char *)malloc(retained_string_bytes);
    if (impl->instructions == NULL ||
        (parser.operand_count != 0u && impl->operands == NULL) ||
        (retained_string_bytes != 0u &&
         impl->literal_storage == NULL)) {
        expr_program_impl_destroy(impl);
        return expr_report(diagnostic,
                           CFLOW_SCXML_CMETA_EXPR_ALLOCATION_FAILED, 0u,
                           "CMeta expression storage allocation failed");
    }

    memset(&parser, 0, sizeof(parser));
    parser.source = source;
    parser.source_size = source_size;
    parser.root = root;
    parser.resolve_state = resolve_state;
    parser.resolve_user = resolve_user;
    parser.limits = limits;
    parser.diagnostic = diagnostic;
    parser.instructions = impl->instructions;
    parser.operands = impl->operands;
    parser.literal_storage = impl->literal_storage;
    parser.literal_storage_capacity = retained_string_bytes;
    parser.emit = true;
    parser.status = CFLOW_SCXML_CMETA_EXPR_OK;
    if (!parser_run(&parser, require_boolean, &emitted_kind)) {
        expr_program_impl_destroy(impl);
        return parser.status;
    }
    if (parser.literal_storage_index != retained_string_bytes ||
        emitted_kind != admitted_kind) {
        expr_program_impl_destroy(impl);
        return expr_report(diagnostic,
                           CFLOW_SCXML_CMETA_EXPR_EVALUATION_ERROR, 0u,
                           "CMeta expression emission mismatched admission");
    }
    impl->root = root;
    impl->instruction_count = (uint32_t)parser.instruction_count;
    impl->operand_count = (uint32_t)parser.operand_count;
    impl->register_count = (uint32_t)parser.max_register;
    impl->result_kind = emitted_kind;
    impl->max_string_bytes = limits.max_string_bytes;
    impl->qvm_limits = qvm_default_limits();
    impl->qvm_limits.max_instructions = impl->instruction_count;
    impl->qvm_limits.max_operands = impl->operand_count;
    impl->qvm_limits.max_regexes = 0u;
    impl->qvm_limits.max_steps = impl->instruction_count;
    qvm_status = qvm_verify_slice_ex(
        impl->instructions, impl->instruction_count, 0u,
        impl->instruction_count, impl->register_count,
        impl->operand_count, 0u, &impl->qvm_limits, &qvm_diagnostic);
    if (qvm_status != QVM_STATUS_OK) {
        expr_program_impl_destroy(impl);
        return expr_report(diagnostic,
                           CFLOW_SCXML_CMETA_EXPR_EVALUATION_ERROR,
                           qvm_diagnostic.instruction,
                           qvm_diagnostic.message);
    }
    out->impl = impl;
    expr_clear_diagnostic(diagnostic);
    return CFLOW_SCXML_CMETA_EXPR_OK;
}

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_expr_compile(
    cflow_scxml_cmeta_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    cflow_scxml_cmeta_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const cflow_scxml_cmeta_expr_limits *limits,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    return expr_compile(out, source, source_size, root, resolve_state,
                        resolve_user, limits, diagnostic, true);
}

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_expr_compile_value(
    cflow_scxml_cmeta_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    cflow_scxml_cmeta_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const cflow_scxml_cmeta_expr_limits *limits,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    return expr_compile(out, source, source_size, root, resolve_state,
                        resolve_user, limits, diagnostic, false);
}

cflow_scxml_cmeta_expr_value_kind
cflow_scxml_cmeta_expr_program_value_kind(
    const cflow_scxml_cmeta_expr_program *program) {
    const cflow_scxml_cmeta_expr_program_impl *impl =
        program != NULL
            ? (const cflow_scxml_cmeta_expr_program_impl *)program->impl
            : NULL;
    return impl != NULL
               ? (cflow_scxml_cmeta_expr_value_kind)impl->result_kind
               : CFLOW_SCXML_CMETA_EXPR_VALUE_INVALID;
}

static void make_value(qvm_value_t *out, expr_value_kind kind) {
    memset(out, 0, sizeof(*out));
    out->type = (int)kind;
}

static bool read_integer(const cmeta_data_desc *desc, const void *object,
                         qvm_value_t *out) {
    const uint8_t bits =
        ((const cmeta_data_integer_shape *)desc->shape)->bits;
    if (desc->kind == CMETA_DATA_SINT) {
        make_value(out, EXPR_VALUE_SINT);
        switch (bits) {
            case 8: { int8_t v; memcpy(&v, object, sizeof(v)); out->integer = v; return true; }
            case 16: { int16_t v; memcpy(&v, object, sizeof(v)); out->integer = v; return true; }
            case 32: { int32_t v; memcpy(&v, object, sizeof(v)); out->integer = v; return true; }
            case 64: memcpy(&out->integer, object, sizeof(out->integer)); return true;
        }
    } else {
        make_value(out, EXPR_VALUE_UINT);
        switch (bits) {
            case 8: { uint8_t v; memcpy(&v, object, sizeof(v)); out->uinteger = v; return true; }
            case 16: { uint16_t v; memcpy(&v, object, sizeof(v)); out->uinteger = v; return true; }
            case 32: { uint32_t v; memcpy(&v, object, sizeof(v)); out->uinteger = v; return true; }
            case 64: memcpy(&out->uinteger, object, sizeof(out->uinteger)); return true;
        }
    }
    return false;
}

static bool read_location(const expr_eval_context *context,
                          const expr_operand *operand, qvm_value_t *out) {
    const void *object = context->root + operand->offset;
    switch (operand->data->kind) {
        case CMETA_DATA_BOOL: {
            bool value;
            memcpy(&value, object, sizeof(value));
            make_value(out, EXPR_VALUE_BOOL);
            out->boolean = value;
            return true;
        }
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
            return read_integer(operand->data, object, out);
        case CMETA_DATA_FLOAT: {
            const uint8_t bits =
                ((const cmeta_data_float_shape *)operand->data->shape)->bits;
            make_value(out, EXPR_VALUE_FLOAT);
            if (bits == 32u) {
                float value;
                memcpy(&value, object, sizeof(value));
                out->number = value;
                return true;
            }
            if (bits == 64u) {
                memcpy(&out->number, object, sizeof(out->number));
                return true;
            }
            return false;
        }
        case CMETA_DATA_ENUM:
            make_value(out, EXPR_VALUE_SINT);
            return cmeta_data_enum_read(operand->data, object,
                                        &out->integer) == CMETA_OK;
        case CMETA_DATA_STRING: {
            const unsigned char *data = NULL;
            size_t size = 0u;
            if (cmeta_data_buffer_read(
                    operand->data, object,
                    context->program->max_string_bytes,
                    &data, &size) != CMETA_OK)
                return false;
            make_value(out, EXPR_VALUE_STRING);
            out->str = (const char *)data;
            out->length = size;
            return true;
        }
        default: return false;
    }
}

static int expr_resolve(void *user, uint32_t index, qvm_value_t *out) {
    expr_eval_context *context = (expr_eval_context *)user;
    const expr_operand *operand;
    bool active;
    if (context == NULL || out == NULL ||
        index >= context->program->operand_count) {
        if (context != NULL) context->failed = true;
        return 0;
    }
    operand = &context->program->operands[index];
    switch (operand->kind) {
        case EXPR_OPERAND_LOCATION:
            if (!read_location(context, operand, out)) {
                context->failed = true;
                return 0;
            }
            return 1;
        case EXPR_OPERAND_SINT:
            make_value(out, EXPR_VALUE_SINT);
            out->integer = operand->value.sint;
            return 1;
        case EXPR_OPERAND_UINT:
            make_value(out, EXPR_VALUE_UINT);
            out->uinteger = operand->value.uint;
            return 1;
        case EXPR_OPERAND_FLOAT:
            make_value(out, EXPR_VALUE_FLOAT);
            out->number = operand->value.number;
            return 1;
        case EXPR_OPERAND_STRING:
            make_value(out, EXPR_VALUE_STRING);
            out->str = operand->value.string.data;
            out->length = operand->value.string.size;
            return 1;
        case EXPR_OPERAND_STATE:
            if (!context->is_active(context->active_user,
                                    operand->value.state, &active)) {
                context->failed = true;
                return 0;
            }
            make_value(out, EXPR_VALUE_BOOL);
            out->boolean = active;
            return 1;
        case EXPR_OPERAND_SYSTEM_NAME:
        case EXPR_OPERAND_SYSTEM_SESSION_ID: {
            const cflow_scxml_cmeta_expr_string_view *view;
            if (context->system_values == NULL) {
                context->failed = true;
                return 0;
            }
            view = operand->kind == EXPR_OPERAND_SYSTEM_NAME
                       ? &context->system_values->name
                       : &context->system_values->session_id;
            if (view->data == NULL ||
                view->size > context->program->max_string_bytes) {
                context->failed = true;
                return 0;
            }
            make_value(out, EXPR_VALUE_STRING);
            out->str = view->data;
            out->length = view->size;
            return 1;
        }
    }
    context->failed = true;
    return 0;
}

static int expr_truthy(void *user, const qvm_value_t *value) {
    (void)user;
    return value != NULL && value->type == EXPR_VALUE_BOOL &&
           value->boolean != 0;
}

static void compare_sint_float(int64_t integer, double number,
                               int *out_order, bool *out_unordered) {
    int64_t truncated;
    double integral;
    if (isnan(number)) {
        *out_unordered = true;
        *out_order = 0;
        return;
    }
    if (number >= SCXML_EXPR_SINT64_UPPER_BOUND) {
        *out_order = -1;
        return;
    }
    if (number < -SCXML_EXPR_SINT64_UPPER_BOUND) {
        *out_order = 1;
        return;
    }
    truncated = (int64_t)number;
    if (integer < truncated) {
        *out_order = -1;
        return;
    }
    if (integer > truncated) {
        *out_order = 1;
        return;
    }
    integral = (double)truncated;
    *out_order = integral < number ? -1 : integral > number ? 1 : 0;
}

static void compare_uint_float(uint64_t integer, double number,
                               int *out_order, bool *out_unordered) {
    uint64_t truncated;
    double integral;
    if (isnan(number)) {
        *out_unordered = true;
        *out_order = 0;
        return;
    }
    if (number >= SCXML_EXPR_UINT64_UPPER_BOUND) {
        *out_order = -1;
        return;
    }
    if (number < 0.0) {
        *out_order = 1;
        return;
    }
    truncated = (uint64_t)number;
    if (integer < truncated) {
        *out_order = -1;
        return;
    }
    if (integer > truncated) {
        *out_order = 1;
        return;
    }
    integral = (double)truncated;
    *out_order = integral < number ? -1 : integral > number ? 1 : 0;
}

static bool numeric_compare(const qvm_value_t *left,
                            const qvm_value_t *right, int *out_order,
                            bool *out_unordered) {
    *out_unordered = false;
    if ((left->type != EXPR_VALUE_SINT &&
         left->type != EXPR_VALUE_UINT &&
         left->type != EXPR_VALUE_FLOAT) ||
        (right->type != EXPR_VALUE_SINT &&
         right->type != EXPR_VALUE_UINT &&
         right->type != EXPR_VALUE_FLOAT))
        return false;
    if (left->type == EXPR_VALUE_FLOAT &&
        right->type == EXPR_VALUE_FLOAT) {
        if (isnan(left->number) || isnan(right->number)) {
            *out_unordered = true;
            *out_order = 0;
        } else {
            *out_order = left->number < right->number ? -1 :
                         left->number > right->number ? 1 : 0;
        }
        return true;
    }
    if (right->type == EXPR_VALUE_FLOAT) {
        if (left->type == EXPR_VALUE_SINT)
            compare_sint_float(left->integer, right->number,
                               out_order, out_unordered);
        else
            compare_uint_float(left->uinteger, right->number,
                               out_order, out_unordered);
        return true;
    }
    if (left->type == EXPR_VALUE_FLOAT) {
        if (right->type == EXPR_VALUE_SINT)
            compare_sint_float(right->integer, left->number,
                               out_order, out_unordered);
        else
            compare_uint_float(right->uinteger, left->number,
                               out_order, out_unordered);
        *out_order = -*out_order;
        return true;
    }
    if (left->type == EXPR_VALUE_SINT && right->type == EXPR_VALUE_SINT) {
        *out_order = left->integer < right->integer ? -1 :
                     left->integer > right->integer ? 1 : 0;
        return true;
    }
    if (left->type == EXPR_VALUE_UINT && right->type == EXPR_VALUE_UINT) {
        *out_order = left->uinteger < right->uinteger ? -1 :
                     left->uinteger > right->uinteger ? 1 : 0;
        return true;
    }
    if (left->type == EXPR_VALUE_SINT) {
        *out_order = left->integer < 0 ? -1 :
                     (uint64_t)left->integer < right->uinteger ? -1 :
                     (uint64_t)left->integer > right->uinteger ? 1 : 0;
        return true;
    }
    if (right->integer < 0) *out_order = 1;
    else *out_order = left->uinteger < (uint64_t)right->integer ? -1 :
                      left->uinteger > (uint64_t)right->integer ? 1 : 0;
    return true;
}

static int expr_binary(void *user, qvm_opcode_t op, uint32_t arg,
                       const qvm_value_t *left, const qvm_value_t *right,
                       qvm_value_t *out) {
    int order = 0;
    bool unordered = false;
    bool result;
    (void)user;
    if (op == QVM_OP_BAND || op == QVM_OP_BOR) {
        bool logical;
        if (left->type != EXPR_VALUE_BOOL || right->type != EXPR_VALUE_BOOL)
            return 0;
        logical = op == QVM_OP_BAND
                      ? (left->boolean && right->boolean)
                      : (left->boolean || right->boolean);
        make_value(out, EXPR_VALUE_BOOL);
        out->boolean = logical;
        return 1;
    }
    if (op != QVM_OP_CMP || arg > 5u) return 0;
    if (left->type == EXPR_VALUE_BOOL && right->type == EXPR_VALUE_BOOL) {
        if (arg > 1u) return 0;
        order = left->boolean == right->boolean ? 0 :
                left->boolean ? 1 : -1;
    } else if (left->type == EXPR_VALUE_STRING &&
               right->type == EXPR_VALUE_STRING) {
        const size_t common = left->length < right->length
                                  ? left->length : right->length;
        order = common != 0u ? memcmp(left->str, right->str, common) : 0;
        if (order == 0)
            order = left->length < right->length ? -1 :
                    left->length > right->length ? 1 : 0;
    } else if (!numeric_compare(left, right, &order, &unordered)) {
        return 0;
    }
    result = arg == 0u ? (!unordered && order == 0) :
             arg == 1u ? (unordered || order != 0) :
             arg == 2u ? (!unordered && order < 0) :
             arg == 3u ? (!unordered && order <= 0) :
             arg == 4u ? (!unordered && order > 0) :
                         (!unordered && order >= 0);
    make_value(out, EXPR_VALUE_BOOL);
    out->boolean = result;
    return 1;
}

static void expr_make_invalid(void *user, qvm_value_t *out) {
    (void)user;
    memset(out, 0, sizeof(*out));
}

static void expr_make_bool(void *user, int value, qvm_value_t *out) {
    (void)user;
    make_value(out, EXPR_VALUE_BOOL);
    out->boolean = value != 0;
}

static void expr_make_number(void *user, double value, qvm_value_t *out) {
    (void)user;
    make_value(out, EXPR_VALUE_FLOAT);
    out->number = value;
}

static void expr_make_string(void *user, const char *value, size_t size,
                             qvm_value_t *out) {
    (void)user;
    make_value(out, EXPR_VALUE_STRING);
    out->str = value;
    out->length = size;
}

static cflow_scxml_cmeta_expr_status expr_evaluate(
    const cflow_scxml_cmeta_expr_program *program,
    const void *root_object,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    qvm_value_t *out_value,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    const cflow_scxml_cmeta_expr_program_impl *impl =
        program != NULL
            ? (const cflow_scxml_cmeta_expr_program_impl *)program->impl
            : NULL;
    expr_eval_context context;
    qvm_exec_ops_t ops = {0};
    qvm_value_t result;
    qvm_diagnostic_t qvm_diagnostic;
    int status;
    expr_clear_diagnostic(diagnostic);
    if (impl == NULL || root_object == NULL || is_active == NULL ||
        out_value == NULL)
        return expr_report(diagnostic, CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT,
                           0u, "invalid CMeta expression evaluation arguments");
    context.program = impl;
    context.root = (const unsigned char *)root_object;
    context.is_active = is_active;
    context.active_user = active_user;
    context.system_values = system_values;
    context.failed = false;
    ops.resolve = expr_resolve;
    ops.truthy = expr_truthy;
    ops.binary = expr_binary;
    ops.make_invalid = expr_make_invalid;
    ops.make_bool = expr_make_bool;
    ops.make_number = expr_make_number;
    ops.make_string = expr_make_string;
    status = qvm_execute_ex(impl->instructions, impl->instruction_count,
                            0u, impl->instruction_count, &ops, &context,
                            NULL, &result, &impl->qvm_limits, &qvm_diagnostic);
    if (context.failed || status != QVM_STATUS_OK ||
        result.type != (int)impl->result_kind)
        return expr_report(diagnostic,
                           CFLOW_SCXML_CMETA_EXPR_EVALUATION_ERROR,
                           qvm_diagnostic.instruction,
                           context.failed
                               ? "CMeta expression operand resolution failed"
                               : status == QVM_STATUS_OK
                               ? "CMeta expression result type mismatched program"
                               : qvm_diagnostic.message);
    *out_value = result;
    return CFLOW_SCXML_CMETA_EXPR_OK;
}

static cflow_scxml_cmeta_expr_status expr_evaluate_condition(
    const cflow_scxml_cmeta_expr_program *program,
    const void *root_object,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    bool *out_value,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    const cflow_scxml_cmeta_expr_program_impl *impl =
        program != NULL
            ? (const cflow_scxml_cmeta_expr_program_impl *)program->impl
            : NULL;
    qvm_value_t result;
    cflow_scxml_cmeta_expr_status status;
    bool value;
    if (impl == NULL || impl->result_kind != EXPR_VALUE_BOOL ||
        out_value == NULL) {
        expr_clear_diagnostic(diagnostic);
        return expr_report(diagnostic, CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT,
                           0u, "invalid CMeta condition evaluation arguments");
    }
    status = expr_evaluate(program, root_object, is_active, active_user,
                           system_values, &result, diagnostic);
    if (status != CFLOW_SCXML_CMETA_EXPR_OK) return status;
    value = result.boolean != 0;
    *out_value = value;
    return CFLOW_SCXML_CMETA_EXPR_OK;
}

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_expr_evaluate(
    const cflow_scxml_cmeta_expr_program *program,
    const void *root_object,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    bool *out_value,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    return expr_evaluate_condition(program, root_object, is_active,
                                   active_user, NULL, out_value, diagnostic);
}

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_expr_evaluate_with_system(
    const cflow_scxml_cmeta_expr_program *program,
    const void *root_object,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    bool *out_value,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    return expr_evaluate_condition(program, root_object, is_active,
                                   active_user, system_values, out_value,
                                   diagnostic);
}

static cflow_scxml_cmeta_expr_status expr_evaluate_public_value(
    const cflow_scxml_cmeta_expr_program *program,
    const void *root_object,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    cflow_scxml_cmeta_expr_value *out_value,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    qvm_value_t result;
    cflow_scxml_cmeta_expr_value value;
    cflow_scxml_cmeta_expr_status status;
    if (out_value == NULL) {
        expr_clear_diagnostic(diagnostic);
        return expr_report(diagnostic, CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT,
                           0u, "invalid CMeta value evaluation arguments");
    }
    status = expr_evaluate(program, root_object, is_active, active_user,
                           system_values, &result, diagnostic);
    if (status != CFLOW_SCXML_CMETA_EXPR_OK) return status;
    memset(&value, 0, sizeof(value));
    value.kind = (cflow_scxml_cmeta_expr_value_kind)result.type;
    switch (value.kind) {
        case CFLOW_SCXML_CMETA_EXPR_VALUE_BOOL:
            value.data.boolean = result.boolean != 0;
            break;
        case CFLOW_SCXML_CMETA_EXPR_VALUE_SINT:
            value.data.sint = result.integer;
            break;
        case CFLOW_SCXML_CMETA_EXPR_VALUE_UINT:
            value.data.uint = result.uinteger;
            break;
        case CFLOW_SCXML_CMETA_EXPR_VALUE_FLOAT:
            value.data.number = result.number;
            break;
        case CFLOW_SCXML_CMETA_EXPR_VALUE_STRING:
            value.data.string.data = result.str;
            value.data.string.size = result.length;
            break;
        default:
            return expr_report(diagnostic,
                               CFLOW_SCXML_CMETA_EXPR_EVALUATION_ERROR, 0u,
                               "CMeta expression produced an invalid scalar");
    }
    *out_value = value;
    return CFLOW_SCXML_CMETA_EXPR_OK;
}

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_expr_evaluate_value(
    const cflow_scxml_cmeta_expr_program *program,
    const void *root_object,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    cflow_scxml_cmeta_expr_value *out_value,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    return expr_evaluate_public_value(program, root_object, is_active,
                                      active_user, NULL, out_value,
                                      diagnostic);
}

cflow_scxml_cmeta_expr_status
cflow_scxml_cmeta_expr_evaluate_value_with_system(
    const cflow_scxml_cmeta_expr_program *program,
    const void *root_object,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    cflow_scxml_cmeta_expr_value *out_value,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    return expr_evaluate_public_value(program, root_object, is_active,
                                      active_user, system_values, out_value,
                                      diagnostic);
}

void cflow_scxml_cmeta_expr_program_destroy(
    cflow_scxml_cmeta_expr_program *program) {
    cflow_scxml_cmeta_expr_program_impl *impl;
    if (program == NULL || program->impl == NULL) return;
    impl = (cflow_scxml_cmeta_expr_program_impl *)program->impl;
    expr_program_impl_destroy(impl);
    program->impl = NULL;
}
