#include "cyaml_ypath_internal.h"
#include "tinytest.h"

#include <string.h>

suite("cyaml YPATH AST") {
    it("parses absolute names and an index") {
        const char* source = "/users[1]/name";
        ypath_ast_t ast;

        check_true(cyaml_ypath_parse(source, &ast));
        check_true(ast.path.absolute);
        check_uint_eq(ast.path.count, 3);
        check_int_eq(ast.path.steps[0].type, YPATH_STEP_NAME);
        check_mem_eq(ast.path.steps[0].v.name.s, "users", 5);
        check_uint_eq(ast.path.steps[0].v.name.len, 5);
        check_int_eq(ast.path.steps[1].type, YPATH_STEP_INDEX);
        check_long_eq(ast.path.steps[1].v.idx, 1);
        check_int_eq(ast.path.steps[2].type, YPATH_STEP_NAME);
        check_mem_eq(ast.path.steps[2].v.name.s, "name", 4);
    }

    it("preserves slice bounds and direction") {
        ypath_ast_t ast;

        check_true(cyaml_ypath_parse("/items[5:1:-2]", &ast));
        check_uint_eq(ast.path.count, 2);
        check_int_eq(ast.path.steps[1].type, YPATH_STEP_SLICE);
        check_long_eq(ast.path.steps[1].v.slice.start, 5);
        check_long_eq(ast.path.steps[1].v.slice.end, 1);
        check_long_eq(ast.path.steps[1].v.slice.step, -2);
        check_bits(ast.path.steps[1].v.slice.flags,
            YPATH_SLICE_HAS_START | YPATH_SLICE_HAS_END | YPATH_SLICE_HAS_STEP);
    }

    it("builds a filter expression tree") {
        const char* source = "/users[?@.age >= 18]/name";
        ypath_ast_t ast;

        check_true(cyaml_ypath_parse(source, &ast));
        check_uint_eq(ast.path.count, 3);
        check_int_eq(ast.path.steps[1].type, YPATH_STEP_FILTER);

        const ypath_expr_t* filter = ast.path.steps[1].v.filter;
        check_not_null(filter);
        check_int_eq(filter->type, YPATH_EXPR_BINARY);
        check_int_eq(filter->v.binary.op, YPATH_OP_GE);
        check_int_eq(filter->v.binary.left->type, YPATH_EXPR_PATH);
        check_uint_eq(filter->v.binary.left->v.path.count, 1);
        check_int_eq(filter->v.binary.left->v.path.steps[0].type, YPATH_STEP_NAME);
        check_mem_eq(filter->v.binary.left->v.path.steps[0].v.name.s, "age", 3);
        check_int_eq(filter->v.binary.right->type, YPATH_EXPR_INT);
        check_long_eq(filter->v.binary.right->v.i, 18);
    }

    it("applies Lemon operator precedence and left associativity") {
        ypath_ast_t ast;

        check_true(cyaml_ypath_parse("/items[?10 - 4 - 1 == 5 && 2 + 3 * 4 == 14]", &ast));
        const ypath_expr_t* root = ast.path.steps[1].v.filter;
        check_int_eq(root->type, YPATH_EXPR_BINARY);
        check_int_eq(root->v.binary.op, YPATH_OP_AND);

        const ypath_expr_t* left_eq = root->v.binary.left;
        check_int_eq(left_eq->v.binary.op, YPATH_OP_EQ);
        const ypath_expr_t* subtraction = left_eq->v.binary.left;
        check_int_eq(subtraction->v.binary.op, YPATH_OP_SUB);
        check_int_eq(subtraction->v.binary.left->v.binary.op, YPATH_OP_SUB);

        const ypath_expr_t* right_eq = root->v.binary.right;
        check_int_eq(right_eq->v.binary.op, YPATH_OP_EQ);
        const ypath_expr_t* addition = right_eq->v.binary.left;
        check_int_eq(addition->v.binary.op, YPATH_OP_ADD);
        check_int_eq(addition->v.binary.right->v.binary.op, YPATH_OP_MUL);
    }

    it("builds nested unary expressions") {
        ypath_ast_t ast;

        check_true(cyaml_ypath_parse("/items[?!!-@.value]", &ast));
        const ypath_expr_t* root = ast.path.steps[1].v.filter;
        check_int_eq(root->type, YPATH_EXPR_UNARY);
        check_int_eq(root->v.unary.op, YPATH_OP_NOT);
        check_int_eq(root->v.unary.arg->v.unary.op, YPATH_OP_NOT);
        check_int_eq(root->v.unary.arg->v.unary.arg->v.unary.op, YPATH_OP_NEG);
        check_int_eq(root->v.unary.arg->v.unary.arg->v.unary.arg->type, YPATH_EXPR_PATH);
    }

    it("borrows name slices from the source expression") {
        char source[] = "/config/host";
        ypath_ast_t ast;

        check_true(cyaml_ypath_parse(source, &ast));
        check_ptr_eq(ast.source, source);
        check_ptr_eq(ast.path.steps[0].v.name.s, source + 1);
        check_ptr_eq(ast.path.steps[1].v.name.s, source + 8);
    }

    it("reports the first invalid token position") {
        ypath_ast_t ast;

        check_false(cyaml_ypath_parse("/users[?@.age >= ]", &ast));
        check_not_null(ast.error);
        check_uint_eq(ast.error_pos, 17);
    }

    it("rejects a null source") {
        ypath_ast_t ast;

        check_false(cyaml_ypath_parse(NULL, &ast));
        check_str_eq(ast.error, "null argument");
        check_uint_eq(ast.error_pos, 0);
    }
}
