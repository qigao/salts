#include "cyaml_ypath_lexer.h"
#include "tinytest.h"

#include <stdint.h>

suite("cyaml YPATH re2c lexer") {
    it("distinguishes path slash from filter division") {
        ypath_lexer_t lexer;

        ypath_lex_init(&lexer, "/");
        ypath_lex_next(&lexer);
        check_int_eq(lexer.tok.type, YPATH_TOK_SLASH);

        ypath_lex_init(&lexer, "/");
        lexer.in_filter = true;
        ypath_lex_next(&lexer);
        check_int_eq(lexer.tok.type, YPATH_TOK_DIV);
    }

    it("keeps keyword prefixes as identifiers") {
        ypath_lexer_t lexer;

        ypath_lex_init(&lexer, "true_value falsehood nullable true false null");
        ypath_lex_next(&lexer);
        check_int_eq(lexer.tok.type, YPATH_TOK_IDENT);
        ypath_lex_next(&lexer);
        check_int_eq(lexer.tok.type, YPATH_TOK_IDENT);
        ypath_lex_next(&lexer);
        check_int_eq(lexer.tok.type, YPATH_TOK_IDENT);
        ypath_lex_next(&lexer);
        check_int_eq(lexer.tok.type, YPATH_TOK_TRUE);
        ypath_lex_next(&lexer);
        check_int_eq(lexer.tok.type, YPATH_TOK_FALSE);
        ypath_lex_next(&lexer);
        check_int_eq(lexer.tok.type, YPATH_TOK_NULL);
    }

    it("saturates out of range integers") {
        ypath_lexer_t lexer;

        ypath_lex_init(&lexer, "999999999999999999999999 -999999999999999999999999");
        ypath_lex_next(&lexer);
        check_int_eq(lexer.tok.type, YPATH_TOK_INT);
        check_true(lexer.tok.val.i == INT64_MAX);
        ypath_lex_next(&lexer);
        check_int_eq(lexer.tok.type, YPATH_TOK_INT);
        check_true(lexer.tok.val.i == INT64_MIN);
    }

    it("reports an exponent beyond the supported range") {
        ypath_lexer_t lexer;

        ypath_lex_init(&lexer, "1e309");
        ypath_lex_next(&lexer);
        check_int_eq(lexer.tok.type, YPATH_TOK_ERROR);
        check_str_eq(lexer.error, "exponent overflow");
    }

    it("returns borrowed string contents without quotes") {
        const char* source = "\"hello\\\" world\" 'it\\'s'";
        ypath_lexer_t lexer;

        ypath_lex_init(&lexer, source);
        ypath_lex_next(&lexer);
        check_int_eq(lexer.tok.type, YPATH_TOK_STRING);
        check_ptr_eq(lexer.tok.start, source + 1);
        check_uint_eq(lexer.tok.len, 13);
        ypath_lex_next(&lexer);
        check_int_eq(lexer.tok.type, YPATH_TOK_STRING);
        check_uint_eq(lexer.tok.len, 5);
    }

    it("reports incomplete boolean operators") {
        ypath_lexer_t lexer;

        ypath_lex_init(&lexer, "|");
        ypath_lex_next(&lexer);
        check_int_eq(lexer.tok.type, YPATH_TOK_ERROR);
        check_str_eq(lexer.error, "expected ||");
    }
}
