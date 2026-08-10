#include "dsv_filter.h"
#include "dsv_filter_expr_parser.h"
#include <turbo_str.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

typedef struct {
    char *name;
    char *raw_name;
    enum { COL_UNUSED = 0, COL_NUMBER, COL_STRING, COL_DYNAMIC } type;
    size_t index;
} dsv_col_t;

typedef enum { DSV_OP_EQ = 0, DSV_OP_NE, DSV_OP_GT, DSV_OP_GE, DSV_OP_LT, DSV_OP_LE } dsv_op_t;
typedef enum { DSV_JOIN_AND = 0, DSV_JOIN_OR } dsv_join_t;

typedef struct {
    size_t col_idx;
    int col_type;
    int lhs_is_simple_col;
    int lhs_has_arith;
    dsv_expr_item_t *lhs_expr;
    size_t lhs_expr_count;
    dsv_op_t op;
    int rhs_is_string;
    double rhs_num;
    char *rhs_str;
    size_t rhs_str_len;
} dsv_clause_t;

static char *dsv_strndup(const char *s, size_t n) {
    size_t len = 0;
    while (len < n && s[len]) len++;
    char *new_s = (char *)malloc(len + 1);
    if (new_s) {
        memcpy(new_s, s, len);
        new_s[len] = '\0';
    }
    return new_s;
}

struct dsv_filter_s {
    const csv_doc_t *doc;
    size_t header_row;

    dsv_col_t *cols;
    size_t col_count;

    dsv_clause_t *clauses;
    dsv_join_t *joins;
    size_t clause_count;
    int compiled;
    csv_scan_predicate_t *scan_predicates;
    int scan_plan_compatible;

    char output_delim;
    char error_msg[256];
};

static void set_error(dsv_filter_t *filter, const char *msg) {
    if (!filter) return;
    if (!msg) {
        filter->error_msg[0] = '\0';
        return;
    }
    strncpy(filter->error_msg, msg, sizeof(filter->error_msg) - 1);
    filter->error_msg[sizeof(filter->error_msg) - 1] = '\0';
}

const char *dsv_filter_error(dsv_filter_t *filter) {
    if (!filter) return "";
    return filter->error_msg;
}

static bool dsv_ends_with(const char *str, const char *suffix) {
    size_t len = strlen(str);
    size_t slen = strlen(suffix);
    if (len < slen) return false;
    const char *end = str + len - slen;
    while (*suffix) {
        if (tolower((unsigned char)*end) != tolower((unsigned char)*suffix)) return false;
        end++;
        suffix++;
    }
    return true;
}

static inline double dsv_fast_atof(const char *s, size_t len) {
    if (!s || len == 0) return 0.0;

    const char *p = s;
    const char *end = s + len;
    int neg = 0;

    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') { p++; }

    unsigned long long mantissa = 0;
    int frac_digits = 0;

    while (p < end && (*p >= '0' && *p <= '9')) {
        mantissa = mantissa * 10ULL + (unsigned long long)(*p - '0');
        p++;
    }

    if (p < end && *p == '.') {
        p++;
        while (p < end && (*p >= '0' && *p <= '9')) {
            mantissa = mantissa * 10ULL + (unsigned long long)(*p - '0');
            frac_digits++;
            p++;
        }
    }

    {
        double result = (double)mantissa;
        static const double pow10[] = {
            1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,
            1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18
        };
        if (frac_digits > 0) {
            if (frac_digits < 19) result /= pow10[frac_digits];
            else {
                int i;
                for (i = 0; i < frac_digits; i++) result /= 10.0;
            }
        }
        return neg ? -result : result;
    }
}

static void free_clause_data(dsv_clause_t *clause) {
    size_t i;
    if (!clause) return;
    free(clause->rhs_str);
    if (clause->lhs_expr) {
        for (i = 0; i < clause->lhs_expr_count; i++) {
            free(clause->lhs_expr[i].ident);
        }
    }
    free(clause->lhs_expr);
    clause->rhs_str = NULL;
    clause->lhs_expr = NULL;
    clause->rhs_str_len = 0;
    clause->lhs_expr_count = 0;
    clause->lhs_is_simple_col = 0;
    clause->lhs_has_arith = 0;
}

static void free_plan(dsv_filter_t *filter) {
    size_t i;
    if (!filter) return;
    if (filter->clauses) {
        for (i = 0; i < filter->clause_count; i++) {
            free_clause_data(&filter->clauses[i]);
        }
    }
    free(filter->clauses);
    free(filter->joins);
    free(filter->scan_predicates);
    filter->clauses = NULL;
    filter->joins = NULL;
    filter->scan_predicates = NULL;
    filter->clause_count = 0;
    filter->compiled = 0;
    filter->scan_plan_compatible = 0;
}

static void skip_ws(const char **cur, const char *end) {
    while (*cur < end && isspace((unsigned char)**cur)) (*cur)++;
}

static int parse_identifier(const char **cur, const char *end, const char **start, size_t *len) {
    const char *p = *cur;
    if (p >= end || !(isalpha((unsigned char)*p) || *p == '_')) return 0;
    *start = p++;
    while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
    *len = (size_t)(p - *start);
    *cur = p;
    return 1;
}

static int parse_number_token(const char **cur, const char *end, const char **start, size_t *len) {
    const char *p = *cur;
    const char *s = p;
    int digits = 0;
    if (p < end && (*p == '+' || *p == '-')) p++;
    while (p < end && isdigit((unsigned char)*p)) { p++; digits++; }
    if (p < end && *p == '.') {
        p++;
        while (p < end && isdigit((unsigned char)*p)) { p++; digits++; }
    }
    if (digits == 0) return 0;
    *start = s;
    *len = (size_t)(p - s);
    *cur = p;
    return 1;
}

static int parse_op(const char **cur, const char *end, dsv_op_t *op) {
    const char *p = *cur;
    if (p >= end) return 0;
    if (*p == '=' && p + 1 < end && p[1] == '=') { *op = DSV_OP_EQ; *cur = p + 2; return 1; }
    if (*p == '!' && p + 1 < end && p[1] == '=') { *op = DSV_OP_NE; *cur = p + 2; return 1; }
    if (*p == '>' && p + 1 < end && p[1] == '=') { *op = DSV_OP_GE; *cur = p + 2; return 1; }
    if (*p == '<' && p + 1 < end && p[1] == '=') { *op = DSV_OP_LE; *cur = p + 2; return 1; }
    if (*p == '>') { *op = DSV_OP_GT; *cur = p + 1; return 1; }
    if (*p == '<') { *op = DSV_OP_LT; *cur = p + 1; return 1; }
    return 0;
}

static int parse_join(const char **cur, const char *end, dsv_join_t *join) {
    const char *p = *cur;
    const char *s;
    size_t len;
    if (p >= end || !isalpha((unsigned char)*p)) return 0;
    s = p;
    while (p < end && isalpha((unsigned char)*p)) p++;
    len = (size_t)(p - s);
    if (len == 3 &&
        tolower((unsigned char)s[0]) == 'a' &&
        tolower((unsigned char)s[1]) == 'n' &&
        tolower((unsigned char)s[2]) == 'd') {
        *join = DSV_JOIN_AND;
        *cur = p;
        return 1;
    }
    if (len == 2 &&
        tolower((unsigned char)s[0]) == 'o' &&
        tolower((unsigned char)s[1]) == 'r') {
        *join = DSV_JOIN_OR;
        *cur = p;
        return 1;
    }
    return 0;
}

static void trim_ws_range(const char **start, const char **end) {
    while (*start < *end && isspace((unsigned char)**start)) (*start)++;
    while (*end > *start && isspace((unsigned char)*((*end) - 1))) (*end)--;
}

static int parse_simple_identifier_range(const char *start, const char *end, const char **id_start, size_t *id_len) {
    const char *cur = start;
    if (!parse_identifier(&cur, end, id_start, id_len)) return 0;
    skip_ws(&cur, end);
    return cur == end;
}

static int find_top_level_cmp(const char *start, const char *end, const char **op_start, const char **after_op, dsv_op_t *op) {
    const char *p = start;
    int depth = 0;
    while (p < end) {
        if (*p == '(') {
            depth++;
            p++;
            continue;
        }
        if (*p == ')') {
            if (depth > 0) depth--;
            p++;
            continue;
        }
        if (depth == 0) {
            const char *tmp = p;
            dsv_op_t found_op;
            if (parse_op(&tmp, end, &found_op)) {
                *op_start = p;
                *after_op = tmp;
                *op = found_op;
                return 1;
            }
        }
        p++;
    }
    return 0;
}

static int dsv_is_arith_op_char(char ch) {
    return ch == '+' || ch == '-' || ch == '*' || ch == '/';
}

static int check_lhs_parentheses(const char *start, const char *end, const char **msg_out) {
    const char *p = start;
    int depth = 0;
    while (p < end) {
        if (*p == '(') {
            const char *q = p + 1;
            depth++;
            while (q < end && isspace((unsigned char)*q)) q++;
            if (q < end && *q == ')') {
                if (msg_out) *msg_out = "invalid filter: empty parentheses in arithmetic expression";
                return 0;
            }
        } else if (*p == ')') {
            if (depth == 0) {
                if (msg_out) *msg_out = "invalid filter: unbalanced parentheses in arithmetic expression";
                return 0;
            }
            depth--;
        }
        p++;
    }
    if (depth != 0) {
        if (msg_out) *msg_out = "invalid filter: unbalanced parentheses in arithmetic expression";
        return 0;
    }
    return 1;
}

static int has_trailing_arith_op(const char *start, const char *end) {
    const char *p = end;
    while (p > start && isspace((unsigned char)*(p - 1))) p--;
    if (p <= start) return 0;
    return dsv_is_arith_op_char(*(p - 1));
}

static int parse_string_lit(const char **cur, const char *end, char **out, size_t *out_len) {
    const char *p = *cur;
    size_t cap = 32;
    size_t len = 0;
    char *buf;
    if (p >= end || *p != '"') return 0;
    p++;
    buf = (char *)malloc(cap);
    if (!buf) return 0;

    while (p < end) {
        char ch = *p++;
        if (ch == '"') break;
        if (ch == '\\' && p < end) {
            char esc = *p++;
            if (esc == '"' || esc == '\\') ch = esc;
            else {
                if (len + 2 > cap) {
                    size_t ncap = cap * 2;
                    char *nb = (char *)realloc(buf, ncap);
                    if (!nb) { free(buf); return 0; }
                    buf = nb;
                    cap = ncap;
                }
                buf[len++] = '\\';
                ch = esc;
            }
        }
        if (len + 1 > cap) {
            size_t ncap = cap * 2;
            char *nb = (char *)realloc(buf, ncap);
            if (!nb) { free(buf); return 0; }
            buf = nb;
            cap = ncap;
        }
        buf[len++] = ch;
    }

    if (p > end || (p <= end && *(p - 1) != '"')) {
        free(buf);
        return 0;
    }

    if (len + 1 > cap) {
        char *nb = (char *)realloc(buf, len + 1);
        if (!nb) { free(buf); return 0; }
        buf = nb;
    }
    buf[len] = '\0';

    *out = buf;
    *out_len = len;
    *cur = p;
    return 1;
}

static int cmp_bytes(const char *a, size_t alen, const char *b, size_t blen) {
    size_t n = alen < blen ? alen : blen;
    int c = memcmp(a, b, n);
    if (c != 0) return c;
    if (alen < blen) return -1;
    if (alen > blen) return 1;
    return 0;
}

static int eval_num(dsv_op_t op, double lhs, double rhs) {
    switch (op) {
        case DSV_OP_EQ: return lhs == rhs;
        case DSV_OP_NE: return lhs != rhs;
        case DSV_OP_GT: return lhs > rhs;
        case DSV_OP_GE: return lhs >= rhs;
        case DSV_OP_LT: return lhs < rhs;
        case DSV_OP_LE: return lhs <= rhs;
        default: return 0;
    }
}

static int eval_str(dsv_op_t op, tstr_v lhs, const char *rhs, size_t rhs_len) {
    int c;
    if (!lhs.data) lhs = tstr_v_from_buf("", 0);
    c = cmp_bytes(lhs.data, lhs.len, rhs, rhs_len);
    switch (op) {
        case DSV_OP_EQ: return c == 0;
        case DSV_OP_NE: return c != 0;
        case DSV_OP_GT: return c > 0;
        case DSV_OP_GE: return c >= 0;
        case DSV_OP_LT: return c < 0;
        case DSV_OP_LE: return c <= 0;
        default: return 0;
    }
}

static int find_col(const dsv_filter_t *filter, const char *name, size_t len) {
    size_t i;
    for (i = 0; i < filter->col_count; i++) {
        const dsv_col_t *c = &filter->cols[i];
        if (c->type == COL_UNUSED) continue;
        if (c->name && strlen(c->name) == len && memcmp(c->name, name, len) == 0) return (int)i;
        if (c->raw_name && strlen(c->raw_name) == len && memcmp(c->raw_name, name, len) == 0) return (int)i;
    }
    return -1;
}

static int parse_lhs_numeric_expr(dsv_filter_t *filter, const char *start, const char *end, dsv_clause_t *clause) {
    dsv_expr_output_t out = {0};
    char expr_error[256] = {0};
    size_t i;
    int has_column = 0;
    const char *paren_msg = NULL;

    if (!check_lhs_parentheses(start, end, &paren_msg)) {
        set_error(filter, paren_msg);
        return 0;
    }

    if (!dsv_expr_parse(start, (size_t)(end - start), &out, expr_error, sizeof(expr_error))) {
        if (strstr(expr_error, "invalid character") != NULL) {
            set_error(filter, "invalid filter: invalid character in arithmetic expression");
        } else if (has_trailing_arith_op(start, end)) {
            set_error(filter, "invalid filter: expected column or number after +/-");
        } else {
            set_error(filter, "invalid filter: malformed arithmetic expression");
        }
        return 0;
    }
    if (out.count == 0) {
        dsv_expr_output_free(&out);
        set_error(filter, "invalid filter: expected column");
        return 0;
    }

    for (i = 0; i < out.count; i++) {
        dsv_expr_item_t *it = &out.items[i];
        if (it->kind == DSV_EXPR_ITEM_IDENT) {
            int col_idx = find_col(filter, it->ident, it->ident_len);
            if (col_idx < 0) {
                dsv_expr_output_free(&out);
                set_error(filter, "invalid filter: unknown column");
                return 0;
            }
            if (filter->cols[col_idx].type != COL_NUMBER) {
                dsv_expr_output_free(&out);
                set_error(filter, "invalid filter: arithmetic requires numeric columns");
                return 0;
            }
            it->col_idx = (size_t)col_idx;
            has_column = 1;
        }
    }

    if (!has_column) {
        dsv_expr_output_free(&out);
        set_error(filter, "invalid filter: expected column");
        return 0;
    }

    clause->col_type = COL_NUMBER;
    clause->lhs_is_simple_col = 0;
    clause->lhs_has_arith = 1;
    clause->lhs_expr = out.items;
    clause->lhs_expr_count = out.count;
    return 1;
}

static int eval_lhs_numeric_expr(const dsv_filter_t *filter, const dsv_clause_t *clause, size_t row_index, double *out_value) {
    size_t i;
    size_t sp = 0;
    size_t cap = 64;
    double local_stack[64];
    double *stack = local_stack;
    int ok = 0;

    if (clause->lhs_is_simple_col) {
        if (clause->col_idx >= filter->col_count) return 0;
        *out_value = csv_get_double(filter->doc, row_index, clause->col_idx, 0.0);
        return 1;
    }
    if (!clause->lhs_expr || clause->lhs_expr_count == 0) return 0;

    if (clause->lhs_expr_count > cap) {
        stack = (double *)malloc(sizeof(double) * clause->lhs_expr_count);
        if (!stack) return 0;
        cap = clause->lhs_expr_count;
    }

    for (i = 0; i < clause->lhs_expr_count; i++) {
        const dsv_expr_item_t *it = &clause->lhs_expr[i];
        if (it->kind == DSV_EXPR_ITEM_NUMBER) {
            if (sp >= cap) goto done;
            stack[sp++] = it->num;
        } else if (it->kind == DSV_EXPR_ITEM_IDENT) {
            if (it->col_idx >= filter->col_count) goto done;
            if (sp >= cap) goto done;
            stack[sp++] = csv_get_double(filter->doc, row_index, it->col_idx, 0.0);
        } else if (it->kind == DSV_EXPR_ITEM_NEG) {
            if (sp < 1) goto done;
            stack[sp - 1] = -stack[sp - 1];
        } else {
            double rhs, lhs, v;
            if (sp < 2) goto done;
            rhs = stack[--sp];
            lhs = stack[--sp];
            switch (it->kind) {
                case DSV_EXPR_ITEM_ADD: v = lhs + rhs; break;
                case DSV_EXPR_ITEM_SUB: v = lhs - rhs; break;
                case DSV_EXPR_ITEM_MUL: v = lhs * rhs; break;
                case DSV_EXPR_ITEM_DIV: v = lhs / rhs; break;
                default: goto done;
            }
            stack[sp++] = v;
        }
    }

    if (sp != 1) goto done;
    *out_value = stack[0];
    ok = 1;

done:
    if (stack != local_stack) free(stack);
    return ok;
}

static tstr_v dsv_value_at(const tstr_v *fields, size_t field_count, size_t col_idx) {
    if (!fields || col_idx >= field_count) return tstr_v_from_buf("", 0);
    return fields[col_idx];
}

static double dsv_value_double_at(const tstr_v *fields, size_t field_count, size_t col_idx) {
    tstr_v value = dsv_value_at(fields, field_count, col_idx);
    return dsv_fast_atof(value.data, value.len);
}

static int eval_lhs_numeric_expr_values(const dsv_filter_t *filter, const dsv_clause_t *clause,
                                        const tstr_v *fields, size_t field_count,
                                        double *out_value) {
    size_t i;
    size_t sp = 0;
    size_t cap = 64;
    double local_stack[64];
    double *stack = local_stack;
    int ok = 0;

    if (clause->lhs_is_simple_col) {
        if (clause->col_idx >= filter->col_count) return 0;
        *out_value = dsv_value_double_at(fields, field_count, clause->col_idx);
        return 1;
    }
    if (!clause->lhs_expr || clause->lhs_expr_count == 0) return 0;

    if (clause->lhs_expr_count > cap) {
        stack = (double *)malloc(sizeof(double) * clause->lhs_expr_count);
        if (!stack) return 0;
        cap = clause->lhs_expr_count;
    }

    for (i = 0; i < clause->lhs_expr_count; i++) {
        const dsv_expr_item_t *it = &clause->lhs_expr[i];
        if (it->kind == DSV_EXPR_ITEM_NUMBER) {
            if (sp >= cap) goto done;
            stack[sp++] = it->num;
        } else if (it->kind == DSV_EXPR_ITEM_IDENT) {
            if (it->col_idx >= filter->col_count) goto done;
            if (sp >= cap) goto done;
            stack[sp++] = dsv_value_double_at(fields, field_count, it->col_idx);
        } else if (it->kind == DSV_EXPR_ITEM_NEG) {
            if (sp < 1) goto done;
            stack[sp - 1] = -stack[sp - 1];
        } else {
            double rhs, lhs, v;
            if (sp < 2) goto done;
            rhs = stack[--sp];
            lhs = stack[--sp];
            switch (it->kind) {
                case DSV_EXPR_ITEM_ADD: v = lhs + rhs; break;
                case DSV_EXPR_ITEM_SUB: v = lhs - rhs; break;
                case DSV_EXPR_ITEM_MUL: v = lhs * rhs; break;
                case DSV_EXPR_ITEM_DIV: v = lhs / rhs; break;
                default: goto done;
            }
            stack[sp++] = v;
        }
    }

    if (sp != 1) goto done;
    *out_value = stack[0];
    ok = 1;

done:
    if (stack != local_stack) free(stack);
    return ok;
}

dsv_filter_t *dsv_filter_create(const csv_doc_t *doc, size_t header_row_index) {
    if (!doc) return NULL;
    if (header_row_index >= csv_row_count(doc)) return NULL;

    dsv_filter_t *f = (dsv_filter_t*)calloc(1, sizeof(dsv_filter_t));
    if (!f) return NULL;
    f->doc = doc;
    f->header_row = header_row_index;
    f->output_delim = '|';
    f->col_count = csv_column_count(doc);
    f->cols = (dsv_col_t*)calloc(f->col_count, sizeof(dsv_col_t));
    if (!f->cols) {
        free(f);
        return NULL;
    }

    for (size_t i = 0; i < f->col_count; ++i) {
        const char *raw_name = csv_get(doc, header_row_index, i);
        if (!raw_name) raw_name = "";

        size_t name_len = strlen(raw_name);
        f->cols[i].raw_name = dsv_strndup(raw_name, name_len);
        f->cols[i].index = i;
        if (dsv_ends_with(raw_name, "_n")) {
            f->cols[i].type = COL_NUMBER;
            f->cols[i].name = dsv_strndup(raw_name, name_len - 2);
        } else if (dsv_ends_with(raw_name, "_s")) {
            f->cols[i].type = COL_STRING;
            f->cols[i].name = dsv_strndup(raw_name, name_len - 2);
        } else {
            f->cols[i].type = COL_DYNAMIC;
            f->cols[i].name = dsv_strndup(raw_name, name_len);
        }
    }

    return f;
}

void dsv_filter_destroy(dsv_filter_t *filter) {
    if (!filter) return;
    free_plan(filter);
    for (size_t i = 0; i < filter->col_count; ++i) {
        if (filter->cols[i].name) free(filter->cols[i].name);
        if (filter->cols[i].raw_name) free(filter->cols[i].raw_name);
    }
    free(filter->cols);
    free(filter);
}

static int build_direct_scan_plan(dsv_filter_t *filter) {
    size_t i;
    csv_scan_predicate_t *predicates;

    filter->scan_plan_compatible = 0;
    if (filter->clause_count == 0) return 0;
    for (i = 0; i + 1 < filter->clause_count; ++i) {
        if (filter->joins[i] != DSV_JOIN_AND) return 0;
    }
    for (i = 0; i < filter->clause_count; ++i) {
        if (!filter->clauses[i].lhs_is_simple_col || filter->clauses[i].lhs_has_arith)
            return 0;
    }

    predicates = (csv_scan_predicate_t *)calloc(filter->clause_count, sizeof(*predicates));
    if (!predicates) return -1;
    for (i = 0; i < filter->clause_count; ++i) {
        const dsv_clause_t *clause = &filter->clauses[i];
        predicates[i].column = clause->col_idx;
        predicates[i].op = (csv_scan_op_t)clause->op;
        if (clause->rhs_is_string) {
            predicates[i].type = CSV_SCAN_VALUE_TEXT;
            predicates[i].text = tstr_v_from_buf(clause->rhs_str, clause->rhs_str_len);
        } else {
            predicates[i].type = CSV_SCAN_VALUE_DOUBLE;
            predicates[i].number = clause->rhs_num;
        }
    }
    filter->scan_predicates = predicates;
    filter->scan_plan_compatible = 1;
    return 1;
}

bool dsv_filter_compile(dsv_filter_t *filter, const char *expression) {
    const char *cur;
    const char *end;
    if (!filter || !expression) return false;

    free_plan(filter);
    set_error(filter, NULL);

    cur = expression;
    end = expression + strlen(expression);

    while (1) {
        dsv_clause_t clause;
        const char *lhs_start;
        const char *lhs_end;
        const char *op_start = NULL;
        const char *after_op = NULL;
        const char *id_start = NULL;
        size_t id_len = 0;
        dsv_op_t op;

        memset(&clause, 0, sizeof(clause));

        skip_ws(&cur, end);
        if (cur >= end) {
            if (filter->clause_count == 0) {
                set_error(filter, "invalid filter: empty expression");
            } else {
                set_error(filter, "invalid filter: expected column");
            }
            free_plan(filter);
            return false;
        }

        lhs_start = cur;
        if (!find_top_level_cmp(cur, end, &op_start, &after_op, &op)) {
            const char *paren_msg = NULL;
            if (!check_lhs_parentheses(cur, end, &paren_msg) && paren_msg) {
                set_error(filter, paren_msg);
            } else {
                set_error(filter, "invalid filter: expected operator");
            }
            free_clause_data(&clause);
            free_plan(filter);
            return false;
        }
        lhs_end = op_start;
        trim_ws_range(&lhs_start, &lhs_end);
        if (lhs_start >= lhs_end) {
            set_error(filter, "invalid filter: expected column");
            free_clause_data(&clause);
            free_plan(filter);
            return false;
        }
        clause.op = op;
        cur = after_op;

        if (parse_simple_identifier_range(lhs_start, lhs_end, &id_start, &id_len)) {
            int col_idx = find_col(filter, id_start, id_len);
            if (col_idx < 0) {
                set_error(filter, "invalid filter: unknown column");
                free_clause_data(&clause);
                free_plan(filter);
                return false;
            }
            clause.lhs_is_simple_col = 1;
            clause.col_idx = (size_t)col_idx;
            clause.col_type = filter->cols[col_idx].type;
            clause.lhs_has_arith = 0;
        } else {
            const char *probe = lhs_start;
            const char *first_id_start = NULL;
            size_t first_id_len = 0;
            if (parse_identifier(&probe, lhs_end, &first_id_start, &first_id_len)) {
                const char *after_id = probe;
                skip_ws(&after_id, lhs_end);
                if (after_id < lhs_end) {
                    int first_col_idx = find_col(filter, first_id_start, first_id_len);
                    if (first_col_idx >= 0 && filter->cols[first_col_idx].type == COL_STRING) {
                        set_error(filter, "invalid filter: arithmetic on string column");
                        free_clause_data(&clause);
                        free_plan(filter);
                        return false;
                    }
                }
            }
            if (!parse_lhs_numeric_expr(filter, lhs_start, lhs_end, &clause)) {
                free_clause_data(&clause);
                free_plan(filter);
                return false;
            }
        }

        skip_ws(&cur, end);
        if (cur < end && *cur == '"') {
            clause.rhs_is_string = 1;
            if (!parse_string_lit(&cur, end, &clause.rhs_str, &clause.rhs_str_len)) {
                set_error(filter, "invalid filter: bad string literal");
                free_clause_data(&clause);
                free_plan(filter);
                return false;
            }
            if (clause.lhs_has_arith) {
                set_error(filter, "invalid filter: arithmetic cannot compare to string");
                free_clause_data(&clause);
                free_plan(filter);
                return false;
            }
            if (!clause.lhs_is_simple_col ||
                (clause.col_type != COL_STRING && clause.col_type != COL_DYNAMIC)) {
                set_error(filter, "invalid filter: string on non-string column");
                free_clause_data(&clause);
                free_plan(filter);
                return false;
            }
        } else {
            const char *num_start = NULL;
            size_t num_len = 0;
            clause.rhs_is_string = 0;
            if (!parse_number_token(&cur, end, &num_start, &num_len)) {
                set_error(filter, "invalid filter: expected value");
                free_clause_data(&clause);
                free_plan(filter);
                return false;
            }
            clause.rhs_num = dsv_fast_atof(num_start, num_len);
            if (clause.lhs_is_simple_col && clause.col_type == COL_STRING) {
                set_error(filter, "invalid filter: number on string column");
                free_clause_data(&clause);
                free_plan(filter);
                return false;
            }
        }

        {
            dsv_clause_t *new_clauses =
                (dsv_clause_t *)realloc(filter->clauses, sizeof(dsv_clause_t) * (filter->clause_count + 1));
            if (!new_clauses) {
                set_error(filter, "oom compiling filter");
                free_clause_data(&clause);
                free_plan(filter);
                return false;
            }
            filter->clauses = new_clauses;
            filter->clauses[filter->clause_count] = clause;
            filter->clause_count++;
        }

        skip_ws(&cur, end);
        if (cur >= end) break;

        {
            dsv_join_t join;
            if (!parse_join(&cur, end, &join)) {
                set_error(filter, "invalid filter: expected and/or");
                free_plan(filter);
                return false;
            }
            dsv_join_t *new_joins =
                (dsv_join_t *)realloc(filter->joins, sizeof(dsv_join_t) * filter->clause_count);
            if (!new_joins) {
                set_error(filter, "oom compiling filter");
                free_plan(filter);
                return false;
            }
            filter->joins = new_joins;
            filter->joins[filter->clause_count - 1] = join;
        }
    }

    filter->compiled = 1;
    if (build_direct_scan_plan(filter) < 0) {
        set_error(filter, "oom compiling direct scan plan");
        free_plan(filter);
        return false;
    }
    return true;
}

void dsv_filter_set_output_delimiter(dsv_filter_t *filter, char delimiter) {
    if (filter) filter->output_delim = delimiter;
}

static int dsv_field_needs_quote(tstr_v value, char delimiter) {
    size_t i;
    if (!value.data) return 0;
    for (i = 0; i < value.len; i++) {
        char ch = value.data[i];
        if (ch == delimiter || ch == '"' || ch == '\r' || ch == '\n') return 1;
    }
    return 0;
}

static size_t dsv_rendered_field_len(tstr_v value, char delimiter) {
    size_t i;
    size_t len = value.len;
    if (!dsv_field_needs_quote(value, delimiter)) return len;
    len = 2;
    for (i = 0; i < value.len; i++) len += value.data[i] == '"' ? 2 : 1;
    return len;
}

static void dsv_render_field(char *dst, size_t *offset, tstr_v value, char delimiter) {
    size_t i;
    int quote = dsv_field_needs_quote(value, delimiter);
    if (!value.data) value = tstr_v_from_buf("", 0);
    if (quote) dst[(*offset)++] = '"';
    for (i = 0; i < value.len; i++) {
        if (quote && value.data[i] == '"') dst[(*offset)++] = '"';
        dst[(*offset)++] = value.data[i];
    }
    if (quote) dst[(*offset)++] = '"';
}

static int dsv_render_values(const dsv_filter_t *filter, const tstr_v *fields,
                             size_t field_count, tstr_t *buffer) {
    size_t total = 0;
    size_t col;
    tstr_t next;

    for (col = 0; col < filter->col_count; ++col) {
        tstr_v value = dsv_value_at(fields, field_count, col);
        size_t field_len = dsv_rendered_field_len(value, filter->output_delim);
        size_t separator_len = col > 0 ? 1 : 0;
        if (field_len > (size_t)-1 - separator_len ||
            total > (size_t)-1 - separator_len - field_len) {
            return 0;
        }
        total += separator_len + field_len;
    }

    next = tstr_reserve(*buffer, total);
    if (!next) return 0;
    *buffer = next;

    {
        size_t offset = 0;
        for (col = 0; col < filter->col_count; ++col) {
            tstr_v value = dsv_value_at(fields, field_count, col);
            if (col > 0) (*buffer)[offset++] = filter->output_delim;
            dsv_render_field(*buffer, &offset, value, filter->output_delim);
        }
        if (!tstr_set_len_checked(*buffer, offset)) return 0;
    }
    return 1;
}

int dsv_filter_check_row(dsv_filter_t *filter, size_t row_index) {
    size_t i;
    int result = 0;
    if (!filter || !filter->compiled) return -1;
    if (row_index >= csv_row_count(filter->doc)) return -1;

    for (i = 0; i < filter->clause_count; i++) {
        dsv_clause_t *c = &filter->clauses[i];
        int clause_ok;

        if (c->rhs_is_string) {
            tstr_v lhs = csv_get_v(filter->doc, row_index, c->col_idx);
            clause_ok = eval_str(c->op, lhs, c->rhs_str, c->rhs_str_len);
        } else {
            double lhs = 0.0;
            if (!eval_lhs_numeric_expr(filter, c, row_index, &lhs)) {
                return -1;
            }
            clause_ok = eval_num(c->op, lhs, c->rhs_num);
        }

        if (i == 0) {
            result = clause_ok;
        } else if (filter->joins[i - 1] == DSV_JOIN_AND) {
            result = result && clause_ok;
        } else {
            result = result || clause_ok;
        }
    }

    return result ? 1 : 0;
}

int dsv_filter_check_values(dsv_filter_t *filter, const tstr_v *fields, size_t field_count) {
    size_t i;
    int result = 0;
    if (!filter || !filter->compiled || !fields) return -1;

    for (i = 0; i < filter->clause_count; i++) {
        dsv_clause_t *c = &filter->clauses[i];
        int clause_ok;

        if (c->rhs_is_string) {
            tstr_v lhs = dsv_value_at(fields, field_count, c->col_idx);
            clause_ok = eval_str(c->op, lhs, c->rhs_str, c->rhs_str_len);
        } else {
            double lhs = 0.0;
            if (!eval_lhs_numeric_expr_values(filter, c, fields, field_count, &lhs)) {
                return -1;
            }
            clause_ok = eval_num(c->op, lhs, c->rhs_num);
        }

        if (i == 0) {
            result = clause_ok;
        } else if (filter->joins[i - 1] == DSV_JOIN_AND) {
            result = result && clause_ok;
        } else {
            result = result || clause_ok;
        }
    }

    return result ? 1 : 0;
}

void dsv_filter_run(dsv_filter_t *filter, dsv_row_callback_t callback, void *user_data) {
    csv_cursor_t *cursor;
    tstr_t rendered = NULL;
    int rc;
    if (!filter || !callback) return;

    cursor = csv_cursor_new(filter->doc, filter->header_row + 1);
    if (!cursor) {
        set_error(filter, "oom creating CSV cursor");
        return;
    }

    while ((rc = csv_cursor_next(cursor)) > 0) {
        size_t field_count = 0;
        const tstr_v *fields = csv_cursor_fields(cursor, &field_count);
        int match = dsv_filter_check_values(filter, fields, field_count);
        if (match != 1) continue;
        if (!dsv_render_values(filter, fields, field_count, &rendered)) {
            set_error(filter, "oom rendering row");
            break;
        }
        callback(user_data, csv_cursor_row_index(cursor), rendered);
    }

    if (rc < 0 || csv_cursor_error(cursor)) set_error(filter, "CSV cursor error");
    tstr_free(rendered);
    csv_cursor_free(cursor);
}

int dsv_filter_scan(dsv_filter_t *filter, const char *content, size_t len,
                    const csv_options_t *opts,
                    const csv_scan_projection_t *projections, size_t projection_count,
                    csv_scan_match_fn on_match, void *ctx, size_t *matched_count) {
    csv_scan_plan_t plan;
    int rc;

    if (matched_count) *matched_count = 0;
    if (!filter || !filter->compiled || !content) return -1;
    if (!filter->scan_plan_compatible) {
        set_error(filter, "direct scan requires simple predicates joined only by AND");
        return -1;
    }

    memset(&plan, 0, sizeof(plan));
    plan.predicates = filter->scan_predicates;
    plan.predicate_count = filter->clause_count;
    plan.projections = projections;
    plan.projection_count = projection_count;
    plan.on_match = on_match;
    plan.ctx = ctx;
    rc = csv_filter_scan_opts(content, len, opts, &plan, matched_count);
    if (rc != 0) {
        set_error(filter, csv_get_error());
        return -1;
    }
    set_error(filter, NULL);
    return 0;
}

static void dsv_index_apply_lower(dsv_index_query_t *query, double value, bool inclusive) {
    if (!query->has_lower_number || value > query->lower_number ||
        (value == query->lower_number && !inclusive && query->lower_inclusive)) {
        query->has_lower_number = true;
        query->lower_number = value;
        query->lower_inclusive = inclusive;
    }
}

static void dsv_index_apply_upper(dsv_index_query_t *query, double value, bool inclusive) {
    if (!query->has_upper_number || value < query->upper_number ||
        (value == query->upper_number && !inclusive && query->upper_inclusive)) {
        query->has_upper_number = true;
        query->upper_number = value;
        query->upper_inclusive = inclusive;
    }
}

typedef struct {
    dsv_index_query_t query;
    int has_text;
} dsv_index_plan_range_t;

static int dsv_index_query_is_empty(const dsv_index_query_t *query) {
    return query->has_lower_number && query->has_upper_number &&
           (query->lower_number > query->upper_number ||
            (query->lower_number == query->upper_number &&
             (!query->lower_inclusive || !query->upper_inclusive)));
}

static int dsv_index_plan_append(dsv_index_plan_range_t *ranges, size_t *count,
                                 const dsv_index_plan_range_t *range) {
    if (dsv_index_query_is_empty(&range->query)) return 1;
    if (*count >= DSV_INDEX_MAX_QUERY_RANGES) return 0;
    ranges[(*count)++] = *range;
    return 1;
}

static int dsv_index_plan_intersect_text(dsv_filter_t *filter,
                                         dsv_index_plan_range_t *ranges,
                                         size_t *range_count,
                                         const dsv_clause_t *clause) {
    tstr_v value = tstr_v_from_buf(clause->rhs_str, clause->rhs_str_len);
    size_t input;
    size_t output = 0;
    if (clause->op != DSV_OP_EQ && clause->op != DSV_OP_NE) {
        set_error(filter, "text index predicates support only == and !=");
        return 0;
    }
    for (input = 0; input < *range_count; ++input) {
        dsv_index_plan_range_t range = ranges[input];
        if (!range.has_text) {
            if (clause->op == DSV_OP_NE) {
                set_error(filter, "text != requires an existing equality-constrained index prefix");
                return 0;
            }
            range.query.text_equals = value;
            range.has_text = 1;
            ranges[output++] = range;
        } else {
            int equal = tstr_v_eq(range.query.text_equals, value);
            if ((clause->op == DSV_OP_EQ && equal) ||
                (clause->op == DSV_OP_NE && !equal))
                ranges[output++] = range;
        }
    }
    *range_count = output;
    return 1;
}

static int dsv_index_plan_intersect_number(dsv_filter_t *filter,
                                           dsv_index_plan_range_t *ranges,
                                           size_t *range_count,
                                           const dsv_clause_t *clause) {
    dsv_index_plan_range_t next[DSV_INDEX_MAX_QUERY_RANGES];
    size_t next_count = 0;
    size_t i;
    for (i = 0; i < *range_count; ++i) {
        dsv_index_plan_range_t range = ranges[i];
        switch (clause->op) {
            case DSV_OP_EQ:
                dsv_index_apply_lower(&range.query, clause->rhs_num, true);
                dsv_index_apply_upper(&range.query, clause->rhs_num, true);
                if (!dsv_index_plan_append(next, &next_count, &range)) goto capacity;
                break;
            case DSV_OP_NE: {
                dsv_index_plan_range_t lower = range;
                dsv_index_plan_range_t upper = range;
                dsv_index_apply_upper(&lower.query, clause->rhs_num, false);
                dsv_index_apply_lower(&upper.query, clause->rhs_num, false);
                if (!dsv_index_plan_append(next, &next_count, &lower) ||
                    !dsv_index_plan_append(next, &next_count, &upper))
                    goto capacity;
                break;
            }
            case DSV_OP_GT:
                dsv_index_apply_lower(&range.query, clause->rhs_num, false);
                if (!dsv_index_plan_append(next, &next_count, &range)) goto capacity;
                break;
            case DSV_OP_GE:
                dsv_index_apply_lower(&range.query, clause->rhs_num, true);
                if (!dsv_index_plan_append(next, &next_count, &range)) goto capacity;
                break;
            case DSV_OP_LT:
                dsv_index_apply_upper(&range.query, clause->rhs_num, false);
                if (!dsv_index_plan_append(next, &next_count, &range)) goto capacity;
                break;
            case DSV_OP_LE:
                dsv_index_apply_upper(&range.query, clause->rhs_num, true);
                if (!dsv_index_plan_append(next, &next_count, &range)) goto capacity;
                break;
            default:
                set_error(filter, "numeric index predicate is unsupported");
                return 0;
        }
    }
    memcpy(ranges, next, next_count * sizeof(next[0]));
    *range_count = next_count;
    return 1;

capacity:
    set_error(filter, "index predicate exceeds DSV_INDEX_MAX_QUERY_RANGES");
    return 0;
}

static int dsv_index_plan_intersect_clause(dsv_filter_t *filter,
                                           dsv_index_plan_range_t *ranges,
                                           size_t *range_count,
                                           const dsv_clause_t *clause,
                                           size_t text_column,
                                           size_t number_column) {
    if (clause->col_idx == text_column && clause->rhs_is_string)
        return dsv_index_plan_intersect_text(filter, ranges, range_count, clause);
    if (clause->col_idx == number_column && !clause->rhs_is_string)
        return dsv_index_plan_intersect_number(filter, ranges, range_count, clause);
    set_error(filter, "filter contains a predicate not covered by this index");
    return 0;
}

static int dsv_index_plan_union_clause(dsv_filter_t *filter,
                                       dsv_index_plan_range_t *ranges,
                                       size_t *range_count,
                                       const dsv_clause_t *clause,
                                       size_t text_column,
                                       size_t number_column) {
    dsv_index_plan_range_t range;
    memset(&range, 0, sizeof(range));
    if (clause->col_idx == text_column && clause->rhs_is_string) {
        if (clause->op != DSV_OP_EQ) {
            set_error(filter, "OR on the text index prefix requires equality");
            return 0;
        }
        range.has_text = 1;
        range.query.text_equals = tstr_v_from_buf(clause->rhs_str, clause->rhs_str_len);
        if (!dsv_index_plan_append(ranges, range_count, &range)) goto capacity;
        return 1;
    }
    if (clause->col_idx == number_column && !clause->rhs_is_string) {
        dsv_index_plan_range_t additions[DSV_INDEX_MAX_QUERY_RANGES];
        size_t addition_count = 1;
        additions[0] = range;
        if (!dsv_index_plan_intersect_number(filter, additions, &addition_count, clause))
            return 0;
        if (*range_count > DSV_INDEX_MAX_QUERY_RANGES - addition_count) goto capacity;
        memcpy(ranges + *range_count, additions, addition_count * sizeof(additions[0]));
        *range_count += addition_count;
        return 1;
    }
    set_error(filter, "filter contains an OR predicate not covered by this index");
    return 0;

capacity:
    set_error(filter, "index predicate exceeds DSV_INDEX_MAX_QUERY_RANGES");
    return 0;
}

int dsv_filter_index_seek(dsv_filter_t *filter, const dsv_index_t *index,
                          dsv_index_cursor_t *cursor) {
    dsv_index_plan_range_t ranges[DSV_INDEX_MAX_QUERY_RANGES];
    dsv_index_query_t queries[DSV_INDEX_MAX_QUERY_RANGES];
    size_t text_column;
    size_t number_column;
    size_t range_count = 1;
    size_t i;

    if (!filter || !filter->compiled || !index || !cursor) return -1;
    text_column = dsv_index_text_column(index);
    number_column = dsv_index_number_column(index);
    if (text_column == DSV_INDEX_NO_COLUMN || number_column == DSV_INDEX_NO_COLUMN) {
        set_error(filter, "index is not open");
        return -1;
    }

    memset(ranges, 0, sizeof(ranges));
    for (i = 0; i < filter->clause_count; ++i) {
        const dsv_clause_t *clause = &filter->clauses[i];
        int ok;
        if (!clause->lhs_is_simple_col || clause->lhs_has_arith) {
            set_error(filter, "index seek requires simple column predicates");
            return -1;
        }
        if (i == 0 || filter->joins[i - 1] == DSV_JOIN_AND) {
            ok = dsv_index_plan_intersect_clause(filter, ranges, &range_count,
                                                 clause, text_column, number_column);
        } else {
            ok = dsv_index_plan_union_clause(filter, ranges, &range_count,
                                             clause, text_column, number_column);
        }
        if (!ok) return -1;
    }
    for (i = 0; i < range_count; ++i) {
        if (!ranges[i].has_text) {
            set_error(filter, "every OR range must constrain the leading text index column");
            return -1;
        }
        queries[i] = ranges[i].query;
    }
    if (range_count == 0) {
        memset(cursor, 0, sizeof(*cursor));
        set_error(filter, NULL);
        return 0;
    }
    if (dsv_index_seek_many(index, queries, range_count, cursor) != 0) {
        set_error(filter, "failed to seek DSV index");
        return -1;
    }
    set_error(filter, NULL);
    return 0;
}
