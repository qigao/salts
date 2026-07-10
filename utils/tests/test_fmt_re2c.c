#include "fmt_lexer.h"
#include "tinytest.h"
#include <stdio.h>
#include <string.h>

spec("fmt_str Re2c Lexer Tests") {
  it("should handle basic text") {
    const char *fmt_str = "Hello World";
    const char *cursor = fmt_str;
    const char *token_start;
    size_t token_len;
    fmt_token_t token;

    token = fmt_scan(&cursor, &token_start, &token_len);
    check_int_eq(token, FMT_TOKEN_TEXT);
    check_size_eq(token_len, 11);
    check_ptr_eq(memcmp(token_start, "Hello World", 11), 0);

    token = fmt_scan(&cursor, &token_start, &token_len);
    check_int_eq(token, FMT_TOKEN_END);
  }

  it("should scan long literal text through the bounded entry point") {
    enum { LITERAL_BYTES = 4096 };
    char fmt_str[LITERAL_BYTES + 3];
    const char *cursor = fmt_str;
    tstr_v token = tstr_v_from_buf(NULL, 0);

    memset(fmt_str, 'x', LITERAL_BYTES);
    fmt_str[LITERAL_BYTES] = '{';
    fmt_str[LITERAL_BYTES + 1] = '}';
    fmt_str[LITERAL_BYTES + 2] = '\0';

    check_int_eq(fmt_scan_v_n(&cursor, fmt_str + LITERAL_BYTES + 2, &token), FMT_TOKEN_TEXT);
    check_size_eq(token.len, LITERAL_BYTES);
    check_int_eq(fmt_scan_v_n(&cursor, fmt_str + LITERAL_BYTES + 2, &token),
                 FMT_TOKEN_PLACEHOLDER);
    check_int_eq(fmt_scan_v_n(&cursor, fmt_str + LITERAL_BYTES + 2, &token), FMT_TOKEN_END);
  }

  it("should not read past a non nul terminated bounded literal") {
    enum { LITERAL_BYTES = 4096 };
    char fmt_str[LITERAL_BYTES];
    const char *cursor = fmt_str;
    tstr_v token = tstr_v_from_buf(NULL, 0);

    memset(fmt_str, 'x', sizeof(fmt_str));

    check_int_eq(fmt_scan_v_n(&cursor, fmt_str + sizeof(fmt_str), &token), FMT_TOKEN_TEXT);
    check_size_eq(token.len, sizeof(fmt_str));
    check_ptr_eq(cursor, fmt_str + sizeof(fmt_str));
    check_int_eq(fmt_scan_v_n(&cursor, fmt_str + sizeof(fmt_str), &token), FMT_TOKEN_END);
  }

  it("should not read past a bounded trailing brace") {
    char fmt_str[] = {'{'};
    const char *cursor = fmt_str;
    tstr_v token = tstr_v_from_buf(NULL, 0);

    check_int_eq(fmt_scan_v_n(&cursor, fmt_str + sizeof(fmt_str), &token), FMT_TOKEN_INVALID);
    check_size_eq(token.len, 1);
    check_ptr_eq(cursor, fmt_str + sizeof(fmt_str));
    check_int_eq(fmt_scan_v_n(&cursor, fmt_str + sizeof(fmt_str), &token), FMT_TOKEN_END);
  }

  it("should handle simple placeholders") {
    const char *fmt_str = "{}";
    const char *cursor = fmt_str;
    const char *token_start;
    size_t token_len;
    fmt_token_t token;

    token = fmt_scan(&cursor, &token_start, &token_len);
    check_int_eq(token, FMT_TOKEN_PLACEHOLDER);

    token = fmt_scan(&cursor, &token_start, &token_len);
    check_int_eq(token, FMT_TOKEN_END);
  }

  it("should handle format specifiers") {
    const char *fmt_str = "{:d}";
    const char *cursor = fmt_str;
    const char *token_start;
    size_t token_len;
    fmt_token_t token;

    token = fmt_scan(&cursor, &token_start, &token_len);
    check_int_eq(token, FMT_TOKEN_SPECIFIER);
    check_size_eq(token_len, 1);
    check_int_eq(token_start[0], 'd');

    token = fmt_scan(&cursor, &token_start, &token_len);
    check_int_eq(token, FMT_TOKEN_END);
  }

  it("should handle escaped braces") {
    const char *fmt_str = "{{}}";
    const char *cursor = fmt_str;
    const char *token_start;
    size_t token_len;
    fmt_token_t token;

    token = fmt_scan(&cursor, &token_start, &token_len);
    check_int_eq(token, FMT_TOKEN_LBRACE_ESC);

    token = fmt_scan(&cursor, &token_start, &token_len);
    check_int_eq(token, FMT_TOKEN_RBRACE_ESC);

    token = fmt_scan(&cursor, &token_start, &token_len);
    check_int_eq(token, FMT_TOKEN_END);
  }

  it("should handle mixed content") {
    const char *fmt_str = "Val: {:04x}";
    const char *cursor = fmt_str;
    const char *token_start;
    size_t token_len;
    fmt_token_t token;

    token = fmt_scan(&cursor, &token_start, &token_len);
    check_int_eq(token, FMT_TOKEN_TEXT);
    check_size_eq(token_len, 5);

    token = fmt_scan(&cursor, &token_start, &token_len);
    check_int_eq(token, FMT_TOKEN_SPECIFIER);
    check_size_eq(token_len, 3);

    token = fmt_scan(&cursor, &token_start, &token_len);
    check_int_eq(token, FMT_TOKEN_END);
  }
}
