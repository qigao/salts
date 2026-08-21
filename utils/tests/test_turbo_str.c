/**
 * @file test_turbo_str.c
 * @brief Unit tests for turbo_str.h (tstr dynamic string)
 */

#include "turbo_str.h"
#include "tinytest.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

spec("TurboStr Tests") {

  describe("Creation and Destruction") {
    it("should create empty string") {
      tstr s = tstr_new();
      check_not_null(s);
      check_equal(tstr_len(s), 0);
      check_equal(tstr_empty(s), 1);
      check_equal(s, "");
      tstr_free(s);
    }

    it("should create from C string") {
      tstr s = tstr_dup("hello");
      check_not_null(s);
      check_equal(tstr_len(s), 5);
      check_equal(s, "hello");
      tstr_free(s);
    }

    it("should clone as a unique-owned deep copy") {
      tstr s = tstr_dup("hello");
      tstr copy = tstr_clone(s);

      check_not_null(copy);
      check_not_equal((const void *)(copy), (const void *)(s));
      check_equal(copy, "hello");
      copy = tstr_cat(copy, " world");
      check_equal(s, "hello");
      check_equal(copy, "hello world");

      tstr_free(copy);
      tstr_free(s);
      check_null(tstr_clone(NULL));
    }

    it("should move ownership and clear the source handle") {
      tstr s = tstr_dup("owned");
      tstr moved = tstr_move(&s);

      check_null(s);
      check_equal(moved, "owned");
      tstr_free(moved);
      check_null(tstr_move(NULL));
    }

    it("should free through pointer and clear the handle") {
      tstr s = tstr_dup("owned");

      tstr_freep(&s);
      check_null(s);
      tstr_freep(NULL);
    }

    it("should create from buffer with length") {
      const char data[] = "hello\0world";
      tstr s = tstr_dup_len(data, 11);
      check_not_null(s);
      check_equal(tstr_len(s), 11);
      check_equal(memcmp(s, data, 11), 0);
      tstr_free(s);
    }

    it("should create zero-filled string from null buffer and length") {
      tstr s = tstr_new_len(NULL, 4);
      check_not_null(s);
      check_equal(tstr_len(s), 4);
      check_equal(memcmp(s, "\0\0\0\0", 4), 0);
      tstr_free(s);
    }

    it("should handle NULL input") {
      tstr s = tstr_dup(NULL);
      check_not_null(s);
      check_equal(tstr_len(s), 0);
      tstr_free(s);
    }

    it("should free NULL safely") {
      tstr_free(NULL);
      check(1); /* No crash */
    }
  }

  describe("Properties") {
    it("should return O(1) length") {
      tstr s = tstr_dup("hello world");
      check_equal(tstr_len(s), 11);
      tstr_free(s);
    }

    it("should report available space") {
      tstr s = tstr_new();
      s = tstr_reserve(s, 100);
      check(tstr_avail(s) >= 100);
      tstr_free(s);
    }

    it("should check empty correctly") {
      tstr s = tstr_new();
      check_equal(tstr_empty(s), 1);
      s = tstr_cat(s, "x");
      check_equal(tstr_empty(s), 0);
      tstr_free(s);
    }

    it("should handle NULL in properties") {
      check_equal(tstr_len(NULL), 0);
      check_equal(tstr_avail(NULL), 0);
      check_equal(tstr_empty(NULL), 1);
    }

    it("should reject invalid manual length changes") {
      tstr s = tstr_dup("hello");
      size_t cap = tstr_len(s) + tstr_avail(s);

      check_equal(tstr_set_len_checked(s, cap + 1), 0);
      check_equal(s, "hello");
      check_equal(tstr_set_len_checked(s, 2), 1);
      check_equal(s, "he");

      tstr_free(s);
    }
  }

  describe("Concatenation") {
    it("should append C string") {
      tstr s = tstr_dup("hello");
      s = tstr_cat(s, " world");
      check_equal(s, "hello world");
      check_equal(tstr_len(s), 11);
      tstr_free(s);
    }

    it("should append with length") {
      tstr s = tstr_dup("hello");
      s = tstr_cat_len(s, " world!!!", 6);
      check_equal(s, "hello world");
      tstr_free(s);
    }

    it("should append another tstr") {
      tstr s1 = tstr_dup("hello");
      tstr s2 = tstr_dup(" world");
      s1 = tstr_cat_str(s1, s2);
      check_equal(s1, "hello world");
      tstr_free(s1);
      tstr_free(s2);
    }

    it("should append formatted string") {
      tstr s = tstr_dup("id=");
      s = tstr_cat_fmt(s, "%d, name=%s", 42, "test");
      check_equal(s, "id=42, name=test");
      tstr_free(s);
    }

    it("should handle NULL in cat") {
      tstr s = tstr_cat(NULL, "hello");
      check_equal(s, "hello");
      tstr_free(s);
    }

    it("should handle multiple appends") {
      tstr s = tstr_new();
      for (int i = 0; i < 100; i++) {
        s = tstr_cat(s, "x");
      }
      check_equal(tstr_len(s), 100);
      tstr_free(s);
    }
  }

  describe("Copy") {
    it("should copy C string") {
      tstr s = tstr_dup("hello");
      s = tstr_cpy(s, "world");
      check_equal(s, "world");
      check_equal(tstr_len(s), 5);
      tstr_free(s);
    }

    it("should copy with length") {
      tstr s = tstr_dup("hello");
      s = tstr_cpy_len(s, "world!!!", 5);
      check_equal(s, "world");
      tstr_free(s);
    }

    it("should clear string") {
      tstr s = tstr_dup("hello");
      tstr_clear(s);
      check_equal(tstr_len(s), 0);
      check_equal(s, "");
      tstr_free(s);
    }
  }

  describe("Comparison") {
    it("should compare equal strings") {
      tstr s1 = tstr_dup("hello");
      tstr s2 = tstr_dup("hello");
      check_equal(tstr_cmp(s1, s2), 0);
      tstr_free(s1);
      tstr_free(s2);
    }

    it("should compare different strings") {
      tstr s1 = tstr_dup("abc");
      tstr s2 = tstr_dup("abd");
      check(tstr_cmp(s1, s2) < 0);
      check(tstr_cmp(s2, s1) > 0);
      tstr_free(s1);
      tstr_free(s2);
    }

    it("should handle NULL in comparison") {
      tstr s = tstr_dup("hello");
      check(tstr_cmp(NULL, s) < 0);
      check(tstr_cmp(s, NULL) > 0);
      check_equal(tstr_cmp(NULL, NULL), 0);
      tstr_free(s);
    }
  }

  describe("Transformation") {
    it("should trim whitespace") {
      tstr s = tstr_dup("  hello world  ");
      s = tstr_trim(s, " ");
      check_equal(s, "hello world");
      tstr_free(s);
    }

    it("should trim multiple characters") {
      tstr s = tstr_dup("\t\n hello \r\n");
      s = tstr_trim(s, " \t\r\n");
      check_equal(s, "hello");
      tstr_free(s);
    }

    it("should convert to lowercase") {
      tstr s = tstr_dup("Hello World");
      tstr_lower(s);
      check_equal(s, "hello world");
      tstr_free(s);
    }

    it("should convert to uppercase") {
      tstr s = tstr_dup("Hello World");
      tstr_upper(s);
      check_equal(s, "HELLO WORLD");
      tstr_free(s);
    }

    it("should preserve non-ASCII bytes during ASCII case conversion") {
      const char input[] = "AbC" "\xE4\xB8\xAD";
      tstr s = tstr_dup_len(input, sizeof(input) - 1);

      tstr_lower(s);
      check_equal(s, "abc" "\xE4\xB8\xAD", sizeof(input) - 1);
      tstr_upper(s);
      check_equal(s, "ABC" "\xE4\xB8\xAD", sizeof(input) - 1);
      tstr_free(s);
    }

    it("should trim left and right independently") {
      tstr s = tstr_dup("..hello..");
      s = tstr_ltrim(s, ".");
      check_equal(s, "hello..");
      s = tstr_rtrim(s, ".");
      check_equal(s, "hello");
      tstr_free(s);
    }

    it("should pad strings without shrinking them") {
      tstr s = tstr_dup("go");

      s = tstr_pad_left(s, 5, '.');
      check_equal(s, "...go");
      s = tstr_pad_right(s, 7, '_');
      check_equal(s, "...go__");
      s = tstr_pad_left(s, 3, '*');
      check_equal(s, "...go__");
      tstr_free(s);
    }

    it("should slice into an owned string") {
      tstr s = tstr_dup("hello world");
      tstr part = tstr_slice(s, 6, 20);
      check_equal(part, "world");
      tstr_free(part);

      part = tstr_slice(s, 100, 4);
      check_equal(part, "");
      tstr_free(part);
      tstr_free(s);
    }

    it("should repeat strings") {
      tstr s = tstr_repeat("ha", 3);
      check_equal(s, "hahaha");
      tstr_free(s);

      s = tstr_repeat(NULL, 3);
      check_equal(s, "");
      tstr_free(s);
    }

    it("should replace substrings with limits") {
      tstr s = tstr_dup("one fish two fish");
      s = tstr_replace(s, "fish", "cat", 1);
      check_equal(s, "one cat two fish");
      s = tstr_replace_all(s, "fish", "cat");
      check_equal(s, "one cat two cat");
      s = tstr_replace_all(s, " cat", "");
      check_equal(s, "one two");
      tstr_free(s);
    }
  }

  describe("Memory Management") {
    it("should reserve space") {
      tstr s = tstr_new();
      s = tstr_reserve(s, 1000);
      check(tstr_avail(s) >= 1000);
      tstr_free(s);
    }

    it("should shrink to fit") {
      tstr s = tstr_new();
      s = tstr_reserve(s, 1000);
      s = tstr_cat(s, "hello");
      s = tstr_shrink(s);
      check_equal(tstr_len(s), 5);
      tstr_free(s);
    }
  }

  describe("Conversion") {
    it("should convert to malloc'd C string") {
      tstr s = tstr_dup("hello");
      char *cstr = tstr_to_cstr(s);
      check_not_null(cstr);
      check_equal(cstr, "hello");
      free(cstr);
      tstr_free(s);
    }

    it("should create from long long") {
      tstr s = tstr_from_ll(12345);
      check_equal(s, "12345");
      tstr_free(s);

      s = tstr_from_ll(-9876);
      check_equal(s, "-9876");
      tstr_free(s);
    }

    it("should handle NULL in to_cstr") {
      char *cstr = tstr_to_cstr(NULL);
      check_null(cstr);
    }
  }

  describe("Split and Join") {
    it("should split by separator") {
      tstr s = tstr_dup("a,b,c");
      int count = 0;
      tstr *tokens = tstr_split(s, ",", &count);
      check_equal(count, 3);
      check_equal(tokens[0], "a");
      check_equal(tokens[1], "b");
      check_equal(tokens[2], "c");
      tstr_free_split(tokens, count);
      tstr_free(s);
    }

    it("should split with multi-char separator") {
      tstr s = tstr_dup("a||b||c");
      int count = 0;
      tstr *tokens = tstr_split(s, "||", &count);
      check_equal(count, 3);
      check_equal(tokens[0], "a");
      check_equal(tokens[1], "b");
      check_equal(tokens[2], "c");
      tstr_free_split(tokens, count);
      tstr_free(s);
    }

    it("should join strings") {
      char *parts[] = {"a", "b", "c"};
      tstr s = tstr_join(parts, 3, "-");
      check_equal(s, "a-b-c");
      tstr_free(s);
    }

    it("should handle empty join") {
      tstr s = tstr_join(NULL, 0, ",");
      check_equal(tstr_len(s), 0);
      tstr_free(s);
    }

    it("should split with empty separator as whole string") {
      tstr s = tstr_dup("abc");
      int count = 0;
      tstr *tokens = tstr_split(s, "", &count);
      check_equal(count, 1);
      check_equal(tokens[0], "abc");
      tstr_free_split(tokens, count);
      tstr_free(s);
    }

    it("should join with null separator and null parts") {
      char *parts[] = {"a", NULL, "b"};
      tstr s = tstr_join(parts, 3, NULL);
      check_equal(s, "ab");
      tstr_free(s);
    }

    it("should partition views without allocating") {
      vstr before;
      vstr match;
      vstr after;
      vstr source = vstr_from_cstr("left::right::tail");
      vstr delim = vstr_from_cstr("::");

      check_equal(vstr_partition(source, delim, &before, &match, &after), 1);
      check_equal(vstr_eq(before, vstr_from_cstr("left")), 1);
      check_equal(vstr_eq(match, delim), 1);
      check_equal(vstr_eq(after, vstr_from_cstr("right::tail")), 1);

      check_equal(vstr_rpartition(source, delim, &before, &match, &after), 1);
      check_equal(vstr_eq(before, vstr_from_cstr("left::right")), 1);
      check_equal(vstr_eq(match, delim), 1);
      check_equal(vstr_eq(after, vstr_from_cstr("tail")), 1);

      check_equal(vstr_partition(source, vstr_from_cstr("/"), &before, &match, &after), 0);
      check_equal(vstr_eq(before, source), 1);
      check_equal(vstr_empty(match), 1);
      check_equal(vstr_empty(after), 1);
    }
  }

  describe("String View") {
    it("should find count and trim views") {
      vstr v = vstr_from_cstr("  alpha beta alpha  ");
      vstr stripped = vstr_trim(v, " ");
      check_equal(vstr_find(stripped, vstr_from_cstr("beta")), 6);
      check_equal(vstr_rfind(stripped, vstr_from_cstr("alpha")), 11);
      check_equal(vstr_count(stripped, vstr_from_cstr("alpha")), 2);
    }

    it("should compare trim and scan long ASCII views") {
      enum { PADDING = 32, TEXT_BYTES = 32 };
      char lower[TEXT_BYTES];
      char upper[TEXT_BYTES];
      char padded[PADDING + TEXT_BYTES + PADDING];
      char delimited[TEXT_BYTES + 1];

      memset(lower, 'a', sizeof(lower));
      memset(upper, 'A', sizeof(upper));
      check_equal(vstr_ieq(vstr_from_buf(lower, sizeof(lower)),
                              vstr_from_buf(upper, sizeof(upper))), 1);
      upper[TEXT_BYTES - 1] = 'B';
      check_equal(vstr_ieq(vstr_from_buf(lower, sizeof(lower)),
                              vstr_from_buf(upper, sizeof(upper))), 0);

      memset(padded, ' ', PADDING);
      memset(padded + PADDING, 'x', TEXT_BYTES);
      memset(padded + PADDING + TEXT_BYTES, '\t', PADDING);
      vstr trimmed = vstr_trim(vstr_from_buf(padded, sizeof(padded)), " \t");
      check_equal(trimmed.len, TEXT_BYTES);
      check_equal(trimmed.data, padded + PADDING, TEXT_BYTES);

      memset(delimited, 'x', TEXT_BYTES);
      delimited[TEXT_BYTES] = ';';
      check_equal(vstr_find_any(vstr_from_buf(delimited, sizeof(delimited)),
                                    vstr_from_cstr(",;")), TEXT_BYTES);
    }

    it("should split views and consume empty delimiter once") {
      vstr rest = vstr_from_cstr("a,b,c");
      vstr delim = vstr_from_cstr(",");
      vstr part = vstr_split_next(&rest, delim);
      check_equal(vstr_eq(part, vstr_from_cstr("a")), 1);
      part = vstr_split_next(&rest, delim);
      check_equal(vstr_eq(part, vstr_from_cstr("b")), 1);
      part = vstr_split_next(&rest, delim);
      check_equal(vstr_eq(part, vstr_from_cstr("c")), 1);
      check_equal(vstr_empty(rest), 1);

      rest = vstr_from_cstr("abc");
      part = vstr_split_next(&rest, vstr_from_buf("", 0));
      check_equal(vstr_eq(part, vstr_from_cstr("abc")), 1);
      check_equal(vstr_empty(rest), 1);
    }

    it("should split views from the right") {
      vstr rest = vstr_from_cstr("a,b,");
      vstr delim = vstr_from_cstr(",");
      vstr part = vstr_rsplit_next(&rest, delim);

      check_equal(vstr_empty(part), 1);
      part = vstr_rsplit_next(&rest, delim);
      check_equal(vstr_eq(part, vstr_from_cstr("b")), 1);
      part = vstr_rsplit_next(&rest, delim);
      check_equal(vstr_eq(part, vstr_from_cstr("a")), 1);
      check_equal(vstr_empty(rest), 1);
    }

    it("should reject invalid views safely") {
      vstr invalid = {NULL, 3};
      vstr needle = vstr_from_cstr("x");
      check_equal(vstr_eq(invalid, invalid), 0);
      check_equal(vstr_find(invalid, needle), VSTR_NPOS);
      check_null(vstr_to_cstr(invalid));

      invalid.data = "x";
      invalid.len = SIZE_MAX;
      check_null(vstr_to_cstr(invalid));
    }

    it("should copy views into c strings") {
      vstr v = vstr_from_buf("hello world", 5);
      char *copy = vstr_to_cstr(v);
      check_not_null(copy);
      check_equal(copy, "hello");
      free(copy);
    }

    it("should validate count slice and iterate UTF-8 views") {
      const char text[] = "a" "\xE4\xB8\xAD" "\xF0\x9F\x98\x80" "b";
      vstr v = vstr_from_buf(text, sizeof(text) - 1);
      vstr rest = v;
      vstr part;
      uint32_t cp = 0;

      check_equal(vstr_utf8_valid(v), 1);
      check_equal(vstr_utf8_invalid_offset(v), VSTR_NPOS);
      check_equal(vstr_utf8_len(v), 4);
      check_equal(vstr_utf8_nlen(v, sizeof(text) - 1), 4);
      check_equal(vstr_utf8_nlen(v, 4), 2);
      check_equal(vstr_utf8_nlen(v, 7), VSTR_NPOS);
      check_equal(vstr_utf8_size_lazy(v), 9);
      check_equal(vstr_utf8_byte_offset(v, 0), 0);
      check_equal(vstr_utf8_byte_offset(v, 1), 1);
      check_equal(vstr_utf8_byte_offset(v, 2), 4);
      check_equal(vstr_utf8_byte_offset(v, 3), 8);
      check_equal(vstr_utf8_byte_offset(v, 4), 9);
      check_equal(vstr_utf8_find_cp(v, 0x4E2Du), 1);
      check_equal(vstr_utf8_find_cp(v, 0x1F600u), 4);
      check_equal(vstr_utf8_rfind_cp(v, 0x62u), 8);
      check_equal(vstr_utf8_find(v, vstr_from_cstr("\xE4\xB8\xAD")), 1);
      check_equal(vstr_utf8_find(v, vstr_from_cstr("\xF0\x9F\x98\x80" "b")), 4);

      part = vstr_utf8_sub(v, 1, 2);
      check_equal(part.len, 7);
      check_equal(memcmp(part.data, "\xE4\xB8\xAD" "\xF0\x9F\x98\x80", 7), 0);

      check_equal(vstr_utf8_next(&rest, &cp), 1);
      check_equal(cp, 0x61u);
      check_equal(vstr_utf8_next(&rest, &cp), 1);
      check_equal(cp, 0x4E2Du);
      check_equal(vstr_utf8_next(&rest, &cp), 1);
      check_equal(cp, 0x1F600u);
      check_equal(vstr_utf8_next(&rest, &cp), 1);
      check_equal(cp, 0x62u);
      check_equal(vstr_utf8_next(&rest, &cp), 0);
    }

    it("should reject invalid UTF-8 views") {
      const char overlong[] = "\xC0\x80";
      const char invalid_after_ascii[] = "a" "\xC0\x80";
      const char surrogate[] = "\xED\xA0\x80";
      const char truncated[] = "\xF0\x9F\x98";
      const char continuation[] = "\x80";
      vstr invalid = vstr_from_buf(overlong, sizeof(overlong) - 1);

      check_equal(vstr_utf8_valid(invalid), 0);
      check_equal(vstr_utf8_invalid_offset(invalid), 0);
      check_equal(vstr_utf8_len(invalid), VSTR_NPOS);
      check_equal(vstr_utf8_byte_offset(invalid, 1), VSTR_NPOS);
      check_equal(vstr_empty(vstr_utf8_sub(invalid, 0, 1)), 1);
      check_equal(vstr_utf8_invalid_offset(vstr_from_buf(invalid_after_ascii, sizeof(invalid_after_ascii) - 1)), 1);

      check_equal(vstr_utf8_valid(vstr_from_buf(surrogate, sizeof(surrogate) - 1)), 0);
      check_equal(vstr_utf8_valid(vstr_from_buf(truncated, sizeof(truncated) - 1)), 0);
      check_equal(vstr_utf8_valid(vstr_from_buf(continuation, sizeof(continuation) - 1)), 0);
      check_equal(tstr_utf8_codepoint_size(0x41u), 1);
      check_equal(tstr_utf8_codepoint_size(0x4E2Du), 3);
      check_equal(tstr_utf8_codepoint_size(0x1F600u), 4);
      check_equal(tstr_utf8_codepoint_size(0xD800u), 0);
      check_equal(tstr_utf8_codepoint_size(0x110000u), 0);
    }

    it("should report invalid UTF-8 after multiple SIMD ASCII blocks") {
      enum { ASCII_BYTES = 32 };
      static const unsigned char invalid_tail[] = {0xE0u, 0x80u, 'x'};
      char input[ASCII_BYTES + sizeof(invalid_tail)];

      memset(input, 'a', ASCII_BYTES);
      memcpy(input + ASCII_BYTES, invalid_tail, sizeof(invalid_tail));

      vstr value = vstr_from_buf(input, sizeof(input));
      check_equal(vstr_utf8_valid(value), 0);
      check_equal(vstr_utf8_invalid_offset(value), ASCII_BYTES);
      check_equal(vstr_utf8_len(value), VSTR_NPOS);
      check_equal(vstr_utf8_byte_offset(value, ASCII_BYTES), ASCII_BYTES);
    }
  }

  describe("UTF-8 String") {
    it("should expose byte length and code-point length separately") {
      const char text[] = "a" "\xE4\xB8\xAD" "\xF0\x9F\x98\x80" "b";
      tstr s = tstr_dup_len(text, sizeof(text) - 1);
      tstr part;

      check_equal(tstr_len(s), 9);
      check_equal(tstr_utf8_valid(s), 1);
      check_equal(tstr_utf8_invalid_offset(s), VSTR_NPOS);
      check_equal(tstr_utf8_len(s), 4);
      check_equal(tstr_utf8_nlen(s, 4), 2);
      check_equal(tstr_utf8_nlen(s, 7), VSTR_NPOS);
      check_equal(tstr_utf8_size_lazy(s), 9);
      check_equal(tstr_utf8_size(s), 10);
      check_equal(tstr_utf8_find_cp(s, 0x4E2Du), 1);
      check_equal(tstr_utf8_find_cp(s, 0x1F600u), 4);
      check_equal(tstr_utf8_rfind_cp(s, 0x62u), 8);
      check_equal(tstr_utf8_find(s, vstr_from_cstr("\xE4\xB8\xAD")), 1);

      part = tstr_utf8_slice(s, 1, 2);
      check_equal(part, "\xE4\xB8\xAD" "\xF0\x9F\x98\x80");
      tstr_free(part);
      tstr_free(s);
    }

    it("should append and create UTF-8 code points") {
      tstr s = tstr_new();
      tstr cp;

      s = tstr_utf8_append_cp(s, 'A');
      s = tstr_utf8_append_cp(s, 0x4E2Du);
      s = tstr_utf8_append_cp(s, 0x1F600u);
      s = tstr_utf8_append_cp(s, 0xD800u);
      check_equal(s, "A" "\xE4\xB8\xAD" "\xF0\x9F\x98\x80");
      check_equal(tstr_utf8_len(s), 3);

      cp = tstr_utf8_from_cp(0x1F600u);
      check_equal(cp, "\xF0\x9F\x98\x80");
      tstr_free(cp);

      check_null(tstr_utf8_from_cp(0x110000u));
      tstr_free(s);
    }
  }

  describe("Binary Safety") {
    it("should handle embedded nulls") {
      const char data[] = "hello\0world";
      tstr s = tstr_dup_len(data, 11);
      check_equal(tstr_len(s), 11);

      s = tstr_cat_len(s, "\0!", 2);
      check_equal(tstr_len(s), 13);
      tstr_free(s);
    }

    it("should repeat embedded null bytes") {
      const char data[] = {'a', '\0', 'b'};
      const char expected[] = {'a', '\0', 'b', 'a', '\0', 'b', 'a', '\0', 'b'};
      tstr repeated = tstr_repeat_v(vstr_from_buf(data, sizeof(data)), 3);

      check_not_null(repeated);
      check_equal(tstr_len(repeated), sizeof(expected));
      check_equal(repeated, expected, sizeof(expected));
      tstr_free(repeated);
    }
  }

  describe("Performance") {
    it("should handle large strings efficiently") {
      tstr s = tstr_new();
      s = tstr_reserve(s, 100000);

      for (int i = 0; i < 10000; i++) {
        s = tstr_cat(s, "0123456789");
      }

      check_equal(tstr_len(s), 100000);
      tstr_free(s);
    }
  }
}
