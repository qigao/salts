#include "turbo_str_view.h"
#include <simde/x86/sse2.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { TSTR_SIMD_CSET_LIMIT = 8 };

#define TSTR_V_COPY_TO(v, alloc_call)                                                              \
  do {                                                                                             \
    if ((v).len == SIZE_MAX || (!(v).data && (v).len > 0)) return NULL;                            \
    char *out = (char *)(alloc_call);                                                              \
    if (!out) return NULL;                                                                         \
    if ((v).len > 0) memcpy(out, (v).data, (v).len);                                               \
    out[(v).len] = '\0';                                                                           \
    return out;                                                                                    \
  } while (0)

static const unsigned char lower_table[256] = {
    0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,
    19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,
    38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,
    57,  58,  59,  60,  61,  62,  63,  64,  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k',
    'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 91,  92,  93,  94,
    95,  96,  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q',
    'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 123, 124, 125, 126, 127, 128, 129, 130, 131, 132,
    133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151,
    152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170,
    171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189,
    190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208,
    209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227,
    228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246,
    247, 248, 249, 250, 251, 252, 253, 254, 255,
};

static inline int tstr_v_valid(tstr_v v) { return v.data != NULL || v.len == 0; }

static simde__m128i tstr_simd_cset_mask(simde__m128i bytes, const char *cset, size_t cset_len) {
  simde__m128i matches = simde_mm_setzero_si128();

  for (size_t i = 0; i < cset_len; ++i) {
    matches = simde_mm_or_si128(matches, simde_mm_cmpeq_epi8(bytes, simde_mm_set1_epi8(cset[i])));
  }
  return matches;
}

static inline int tstr_utf8_is_cont(unsigned char c) { return (c & 0xC0u) == 0x80u; }

static size_t tstr_utf8_ascii_prefix_scalar(const char *data, size_t len) {
  size_t offset = 0;

  while (offset < len && (unsigned char)data[offset] < 0x80u)
    ++offset;
  return offset;
}

/* Returns the leading ASCII byte count without reading past end. */
static size_t tstr_utf8_ascii_prefix_simde(const char *data, size_t len) {
  size_t offset = 0;
  size_t first_block = len < sizeof(simde__m128i) ? len : sizeof(simde__m128i);

  /* Avoid vector setup for text that becomes non-ASCII within the first block. */
  while (offset < first_block && (unsigned char)data[offset] < 0x80u)
    ++offset;
  if (offset < first_block) return offset;

  while (len - offset >= sizeof(simde__m128i)) {
    simde__m128i bytes = simde_mm_loadu_si128((const simde__m128i *)(data + offset));
    unsigned int non_ascii = (unsigned int)simde_mm_movemask_epi8(bytes);

    if (non_ascii == 0U) {
      offset += sizeof(simde__m128i);
      continue;
    }
    while ((non_ascii & 1U) == 0U) {
      ++offset;
      non_ascii >>= 1;
    }
    return offset;
  }

  return offset + tstr_utf8_ascii_prefix_scalar(data + offset, len - offset);
}

static int tstr_utf8_decode_one(const char *data, size_t len, uint32_t *codepoint, size_t *width) {
  const unsigned char *s = (const unsigned char *)data;
  uint32_t cp;

  if (!data || len == 0) return 0;

  if (s[0] < 0x80u) {
    cp = s[0];
    *width = 1;
  } else if (s[0] >= 0xC2u && s[0] <= 0xDFu) {
    if (len < 2 || !tstr_utf8_is_cont(s[1])) return 0;
    cp = ((uint32_t)(s[0] & 0x1Fu) << 6) | (uint32_t)(s[1] & 0x3Fu);
    *width = 2;
  } else if (s[0] == 0xE0u) {
    if (len < 3 || s[1] < 0xA0u || s[1] > 0xBFu || !tstr_utf8_is_cont(s[2])) return 0;
    cp = ((uint32_t)(s[0] & 0x0Fu) << 12) | ((uint32_t)(s[1] & 0x3Fu) << 6) |
         (uint32_t)(s[2] & 0x3Fu);
    *width = 3;
  } else if ((s[0] >= 0xE1u && s[0] <= 0xECu) || (s[0] >= 0xEEu && s[0] <= 0xEFu)) {
    if (len < 3 || !tstr_utf8_is_cont(s[1]) || !tstr_utf8_is_cont(s[2])) return 0;
    cp = ((uint32_t)(s[0] & 0x0Fu) << 12) | ((uint32_t)(s[1] & 0x3Fu) << 6) |
         (uint32_t)(s[2] & 0x3Fu);
    *width = 3;
  } else if (s[0] == 0xEDu) {
    if (len < 3 || s[1] < 0x80u || s[1] > 0x9Fu || !tstr_utf8_is_cont(s[2])) return 0;
    cp = ((uint32_t)(s[0] & 0x0Fu) << 12) | ((uint32_t)(s[1] & 0x3Fu) << 6) |
         (uint32_t)(s[2] & 0x3Fu);
    *width = 3;
  } else if (s[0] == 0xF0u) {
    if (len < 4 || s[1] < 0x90u || s[1] > 0xBFu || !tstr_utf8_is_cont(s[2]) ||
        !tstr_utf8_is_cont(s[3]))
      return 0;
    cp = ((uint32_t)(s[0] & 0x07u) << 18) | ((uint32_t)(s[1] & 0x3Fu) << 12) |
         ((uint32_t)(s[2] & 0x3Fu) << 6) | (uint32_t)(s[3] & 0x3Fu);
    *width = 4;
  } else if (s[0] >= 0xF1u && s[0] <= 0xF3u) {
    if (len < 4 || !tstr_utf8_is_cont(s[1]) || !tstr_utf8_is_cont(s[2]) ||
        !tstr_utf8_is_cont(s[3]))
      return 0;
    cp = ((uint32_t)(s[0] & 0x07u) << 18) | ((uint32_t)(s[1] & 0x3Fu) << 12) |
         ((uint32_t)(s[2] & 0x3Fu) << 6) | (uint32_t)(s[3] & 0x3Fu);
    *width = 4;
  } else if (s[0] == 0xF4u) {
    if (len < 4 || s[1] < 0x80u || s[1] > 0x8Fu || !tstr_utf8_is_cont(s[2]) ||
        !tstr_utf8_is_cont(s[3]))
      return 0;
    cp = ((uint32_t)(s[0] & 0x07u) << 18) | ((uint32_t)(s[1] & 0x3Fu) << 12) |
         ((uint32_t)(s[2] & 0x3Fu) << 6) | (uint32_t)(s[3] & 0x3Fu);
    *width = 4;
  } else {
    return 0;
  }

  if (codepoint) *codepoint = cp;
  return 1;
}

int tstr_v_eq(tstr_v a, tstr_v b) {
  if (a.len != b.len) return 0;
  if (a.len == 0) return 1;
  if (!tstr_v_valid(a) || !tstr_v_valid(b)) return 0;
  return memcmp(a.data, b.data, a.len) == 0;
}

int tstr_v_ieq(tstr_v a, tstr_v b) {
  if (a.len != b.len) return 0;
  if (a.len == 0) return 1;
  if (!tstr_v_valid(a) || !tstr_v_valid(b)) return 0;
  size_t i = 0;
  const simde__m128i uppercase_start = simde_mm_set1_epi8('A' - 1);
  const simde__m128i uppercase_end = simde_mm_set1_epi8('Z' + 1);
  const simde__m128i ascii_lower_bit = simde_mm_set1_epi8(0x20);

  while (a.len - i >= sizeof(simde__m128i)) {
    simde__m128i left = simde_mm_loadu_si128((const simde__m128i *)(a.data + i));
    simde__m128i right = simde_mm_loadu_si128((const simde__m128i *)(b.data + i));
    simde__m128i left_upper = simde_mm_and_si128(simde_mm_cmpgt_epi8(left, uppercase_start),
                                                  simde_mm_cmplt_epi8(left, uppercase_end));
    simde__m128i right_upper = simde_mm_and_si128(simde_mm_cmpgt_epi8(right, uppercase_start),
                                                   simde_mm_cmplt_epi8(right, uppercase_end));
    left = simde_mm_or_si128(left, simde_mm_and_si128(left_upper, ascii_lower_bit));
    right = simde_mm_or_si128(right, simde_mm_and_si128(right_upper, ascii_lower_bit));
    if (simde_mm_movemask_epi8(simde_mm_cmpeq_epi8(left, right)) != 0xFFFF)
      return 0;
    i += sizeof(simde__m128i);
  }
  for (; i < a.len; i++) {
    if (lower_table[(unsigned char)a.data[i]] != lower_table[(unsigned char)b.data[i]]) return 0;
  }
  return 1;
}

int tstr_v_starts_with(tstr_v s, tstr_v prefix) {
  if (prefix.len > s.len) return 0;
  if (prefix.len == 0) return 1;
  if (!tstr_v_valid(s) || !tstr_v_valid(prefix)) return 0;
  return memcmp(s.data, prefix.data, prefix.len) == 0;
}

int tstr_v_ends_with(tstr_v s, tstr_v suffix) {
  if (suffix.len > s.len) return 0;
  if (suffix.len == 0) return 1;
  if (!tstr_v_valid(s) || !tstr_v_valid(suffix)) return 0;
  return memcmp(s.data + (s.len - suffix.len), suffix.data, suffix.len) == 0;
}

int tstr_v_contains(tstr_v s, tstr_v needle) { return tstr_v_find(s, needle) != TSTR_V_NPOS; }

size_t tstr_v_find(tstr_v s, tstr_v needle) {
  if (needle.len == 0) return 0;
  if (needle.len > s.len) return TSTR_V_NPOS;
  if (!tstr_v_valid(s) || !tstr_v_valid(needle)) return TSTR_V_NPOS;
  char first = needle.data[0];
  size_t limit = s.len - needle.len;
  for (size_t i = 0; i <= limit; i++) {
    const char *p = (const char *)memchr(s.data + i, first, limit - i + 1);
    if (!p) return TSTR_V_NPOS;
    i = (size_t)(p - s.data);
    if (memcmp(p, needle.data, needle.len) == 0) return i;
  }
  return TSTR_V_NPOS;
}

size_t tstr_v_rfind(tstr_v s, tstr_v needle) {
  if (needle.len == 0) return s.len;
  if (needle.len > s.len) return TSTR_V_NPOS;
  if (!tstr_v_valid(s) || !tstr_v_valid(needle)) return TSTR_V_NPOS;
  for (size_t i = s.len - needle.len + 1; i > 0; i--) {
    if (memcmp(s.data + i - 1, needle.data, needle.len) == 0) return i - 1;
  }
  return TSTR_V_NPOS;
}

size_t tstr_v_find_char(tstr_v s, char c) {
  if (s.len == 0) return TSTR_V_NPOS;
  if (!tstr_v_valid(s)) return TSTR_V_NPOS;
  const char *p = (const char *)memchr(s.data, c, s.len);
  return p ? (size_t)(p - s.data) : TSTR_V_NPOS;
}

size_t tstr_v_rfind_char(tstr_v s, char c) {
  if (!tstr_v_valid(s)) return TSTR_V_NPOS;
  for (size_t i = s.len; i > 0; i--) {
    if (s.data[i - 1] == c) return i - 1;
  }
  return TSTR_V_NPOS;
}

size_t tstr_v_count(tstr_v s, tstr_v needle) {
  size_t count = 0;
  size_t offset = 0;

  if (needle.len == 0 || needle.len > s.len) return 0;
  if (!tstr_v_valid(s) || !tstr_v_valid(needle)) return 0;

  while (offset <= s.len - needle.len) {
    tstr_v rest = tstr_v_from_buf(s.data + offset, s.len - offset);
    size_t pos = tstr_v_find(rest, needle);
    if (pos == TSTR_V_NPOS) break;
    ++count;
    offset += pos + needle.len;
  }
  return count;
}

static void tstr_v_partition_empty(tstr_v *before, tstr_v *match, tstr_v *after) {
  if (before) *before = tstr_v_from_buf(NULL, 0);
  if (match) *match = tstr_v_from_buf(NULL, 0);
  if (after) *after = tstr_v_from_buf(NULL, 0);
}

int tstr_v_partition(tstr_v s, tstr_v delim, tstr_v *before, tstr_v *match, tstr_v *after) {
  size_t pos;

  if (!before || !match || !after || !tstr_v_valid(s) || !tstr_v_valid(delim) || delim.len == 0) {
    tstr_v_partition_empty(before, match, after);
    return 0;
  }
  pos = tstr_v_find(s, delim);
  if (pos == TSTR_V_NPOS) {
    *before = s;
    *match = tstr_v_from_buf(s.data ? s.data + s.len : NULL, 0);
    *after = tstr_v_from_buf(s.data ? s.data + s.len : NULL, 0);
    return 0;
  }
  *before = tstr_v_from_buf(s.data, pos);
  *match = tstr_v_from_buf(s.data + pos, delim.len);
  *after = tstr_v_from_buf(s.data + pos + delim.len, s.len - pos - delim.len);
  return 1;
}

int tstr_v_rpartition(tstr_v s, tstr_v delim, tstr_v *before, tstr_v *match, tstr_v *after) {
  size_t pos;

  if (!before || !match || !after || !tstr_v_valid(s) || !tstr_v_valid(delim) || delim.len == 0) {
    tstr_v_partition_empty(before, match, after);
    return 0;
  }
  pos = tstr_v_rfind(s, delim);
  if (pos == TSTR_V_NPOS) {
    *before = tstr_v_from_buf(s.data, 0);
    *match = tstr_v_from_buf(s.data, 0);
    *after = s;
    return 0;
  }
  *before = tstr_v_from_buf(s.data, pos);
  *match = tstr_v_from_buf(s.data + pos, delim.len);
  *after = tstr_v_from_buf(s.data + pos + delim.len, s.len - pos - delim.len);
  return 1;
}

tstr_v tstr_v_sub(tstr_v s, size_t pos, size_t n) {
  if (!tstr_v_valid(s)) return tstr_v_from_buf(NULL, 0);
  if (pos >= s.len) return tstr_v_from_buf(NULL, 0);
  size_t available = s.len - pos;
  if (n > available) n = available;
  return tstr_v_from_buf(s.data + pos, n);
}

int tstr_v_utf8_valid(tstr_v s) {
  return tstr_v_utf8_invalid_offset(s) == TSTR_V_NPOS;
}

size_t tstr_v_utf8_invalid_offset(tstr_v s) {
  size_t offset = 0;
  int use_simde = 1;

  if (!tstr_v_valid(s)) return 0;
  while (offset < s.len) {
    size_t width = 0;
    if (!use_simde) {
      while (offset < s.len) {
        if (!tstr_utf8_decode_one(s.data + offset, s.len - offset, NULL, &width)) return offset;
        offset += width;
      }
      return TSTR_V_NPOS;
    }
    size_t remaining = s.len - offset;
    size_t ascii = use_simde ? tstr_utf8_ascii_prefix_simde(s.data + offset, remaining)
                             : tstr_utf8_ascii_prefix_scalar(s.data + offset, remaining);

    if (use_simde && ascii < (remaining < sizeof(simde__m128i) ? remaining : sizeof(simde__m128i)))
      use_simde = 0;
    offset += ascii;
    if (offset == s.len) break;
    if (!tstr_utf8_decode_one(s.data + offset, s.len - offset, NULL, &width)) return offset;
    offset += width;
  }
  return TSTR_V_NPOS;
}

size_t tstr_v_find_any(tstr_v s, tstr_v delimiters) {
  size_t i = 0;

  if (s.len == 0 || delimiters.len == 0 || !tstr_v_valid(s) || !tstr_v_valid(delimiters))
    return TSTR_V_NPOS;
  if (delimiters.len > TSTR_SIMD_CSET_LIMIT) {
    for (; i < s.len; ++i) {
      if (memchr(delimiters.data, s.data[i], delimiters.len) != NULL) return i;
    }
    return TSTR_V_NPOS;
  }
  while (s.len - i >= sizeof(simde__m128i)) {
    simde__m128i bytes = simde_mm_loadu_si128((const simde__m128i *)(s.data + i));
    unsigned int matches = (unsigned int)simde_mm_movemask_epi8(
        tstr_simd_cset_mask(bytes, delimiters.data, delimiters.len));

    if (matches == 0U) {
      i += sizeof(simde__m128i);
      continue;
    }
    while ((matches & 1U) == 0U) {
      ++i;
      matches >>= 1;
    }
    return i;
  }
  for (; i < s.len; ++i) {
    if (memchr(delimiters.data, s.data[i], delimiters.len) != NULL) return i;
  }
  return TSTR_V_NPOS;
}

size_t tstr_v_utf8_len(tstr_v s) {
  return tstr_v_utf8_nlen(s, s.len);
}

size_t tstr_v_utf8_nlen(tstr_v s, size_t n) {
  size_t offset = 0;
  size_t count = 0;
  size_t limit;
  int use_simde = 1;

  if (!tstr_v_valid(s)) return TSTR_V_NPOS;
  limit = n < s.len ? n : s.len;
  while (offset < limit) {
    size_t width = 0;
    if (!use_simde) {
      while (offset < limit) {
        if (!tstr_utf8_decode_one(s.data + offset, limit - offset, NULL, &width))
          return TSTR_V_NPOS;
        offset += width;
        ++count;
      }
      return count;
    }
    size_t remaining = limit - offset;
    size_t ascii = use_simde ? tstr_utf8_ascii_prefix_simde(s.data + offset, remaining)
                             : tstr_utf8_ascii_prefix_scalar(s.data + offset, remaining);

    if (use_simde && ascii < (remaining < sizeof(simde__m128i) ? remaining : sizeof(simde__m128i)))
      use_simde = 0;
    offset += ascii;
    count += ascii;
    if (offset == limit) break;
    if (!tstr_utf8_decode_one(s.data + offset, limit - offset, NULL, &width)) return TSTR_V_NPOS;
    offset += width;
    ++count;
  }
  return count;
}

size_t tstr_v_utf8_size_lazy(tstr_v s) {
  if (!tstr_v_valid(s)) return TSTR_V_NPOS;
  return s.len;
}

size_t tstr_v_utf8_byte_offset(tstr_v s, size_t char_index) {
  size_t offset = 0;
  size_t count = 0;

  if (!tstr_v_valid(s)) return TSTR_V_NPOS;
  if (char_index == 0) return 0;
  while (offset < s.len) {
    size_t width = 0;
    size_t ascii = tstr_utf8_ascii_prefix_simde(s.data + offset, s.len - offset);
    size_t remaining = char_index - count;
    if (ascii >= remaining) return offset + remaining;
    offset += ascii;
    count += ascii;
    if (!tstr_utf8_decode_one(s.data + offset, s.len - offset, NULL, &width)) return TSTR_V_NPOS;
    offset += width;
    ++count;
    if (count == char_index) return offset;
  }
  return TSTR_V_NPOS;
}

tstr_v tstr_v_utf8_sub(tstr_v s, size_t char_pos, size_t char_count) {
  size_t start;
  size_t end;
  tstr_v tail;

  if (!tstr_v_utf8_valid(s)) return tstr_v_from_buf(NULL, 0);

  start = tstr_v_utf8_byte_offset(s, char_pos);
  if (start == TSTR_V_NPOS || start >= s.len || char_count == 0) return tstr_v_from_buf(NULL, 0);
  if (char_count == SIZE_MAX) return tstr_v_from_buf(s.data + start, s.len - start);

  tail = tstr_v_from_buf(s.data + start, s.len - start);
  end = tstr_v_utf8_byte_offset(tail, char_count);
  if (end == TSTR_V_NPOS) end = tail.len;
  return tstr_v_from_buf(tail.data, end);
}

int tstr_v_utf8_next(tstr_v *rest, uint32_t *codepoint) {
  size_t width = 0;
  uint32_t cp = 0;

  if (!rest || rest->len == 0 || !tstr_v_valid(*rest)) return 0;
  if (!tstr_utf8_decode_one(rest->data, rest->len, &cp, &width)) return 0;

  if (codepoint) *codepoint = cp;
  *rest = tstr_v_from_buf(rest->data + width, rest->len - width);
  return 1;
}

size_t tstr_v_utf8_find_cp(tstr_v s, uint32_t codepoint) {
  size_t offset = 0;

  if (tstr_utf8_codepoint_size(codepoint) == 0 || !tstr_v_valid(s)) return TSTR_V_NPOS;
  while (offset < s.len) {
    size_t width = 0;
    uint32_t cp = 0;
    if (!tstr_utf8_decode_one(s.data + offset, s.len - offset, &cp, &width)) return TSTR_V_NPOS;
    if (cp == codepoint) return offset;
    offset += width;
  }
  return TSTR_V_NPOS;
}

size_t tstr_v_utf8_rfind_cp(tstr_v s, uint32_t codepoint) {
  size_t offset = 0;
  size_t found = TSTR_V_NPOS;

  if (tstr_utf8_codepoint_size(codepoint) == 0 || !tstr_v_valid(s)) return TSTR_V_NPOS;
  while (offset < s.len) {
    size_t width = 0;
    uint32_t cp = 0;
    if (!tstr_utf8_decode_one(s.data + offset, s.len - offset, &cp, &width)) return TSTR_V_NPOS;
    if (cp == codepoint) found = offset;
    offset += width;
  }
  return found;
}

size_t tstr_v_utf8_find(tstr_v haystack, tstr_v needle) {
  size_t offset = 0;

  if (!tstr_v_utf8_valid(haystack) || !tstr_v_utf8_valid(needle)) return TSTR_V_NPOS;
  if (needle.len == 0) return 0;
  if (needle.len > haystack.len) return TSTR_V_NPOS;

  while (offset <= haystack.len - needle.len) {
    size_t width = 0;
    if (memcmp(haystack.data + offset, needle.data, needle.len) == 0) return offset;
    if (!tstr_utf8_decode_one(haystack.data + offset, haystack.len - offset, NULL, &width))
      return TSTR_V_NPOS;
    offset += width;
  }
  return TSTR_V_NPOS;
}

size_t tstr_utf8_codepoint_size(uint32_t codepoint) {
  if (codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) return 0;
  if (codepoint <= 0x7Fu) return 1;
  if (codepoint <= 0x7FFu) return 2;
  if (codepoint <= 0xFFFFu) return 3;
  return 4;
}

tstr_v tstr_v_trim(tstr_v s, const char *cset) {
  return tstr_v_trim_left(tstr_v_trim_right(s, cset), cset);
}

tstr_v tstr_v_trim_left(tstr_v s, const char *cset) {
  if (!s.data || s.len == 0 || !cset) return s;
  size_t cset_len = strlen(cset);
  size_t start = 0;
  if (cset_len > 0 && cset_len <= TSTR_SIMD_CSET_LIMIT) {
    while (s.len - start >= sizeof(simde__m128i)) {
      simde__m128i bytes = simde_mm_loadu_si128((const simde__m128i *)(s.data + start));
      unsigned int matches = (unsigned int)simde_mm_movemask_epi8(
          tstr_simd_cset_mask(bytes, cset, cset_len));

      if (matches == 0xFFFFU) {
        start += sizeof(simde__m128i);
        continue;
      }
      while ((matches & 1U) != 0U) {
        ++start;
        matches >>= 1;
      }
      return tstr_v_from_buf(s.data + start, s.len - start);
    }
  }
  while (start < s.len && strchr(cset, s.data[start]) != NULL)
    start++;
  return tstr_v_from_buf(s.data + start, s.len - start);
}

tstr_v tstr_v_trim_right(tstr_v s, const char *cset) {
  if (!s.data || s.len == 0 || !cset) return s;
  size_t cset_len = strlen(cset);
  size_t end = s.len;
  if (cset_len > 0 && cset_len <= TSTR_SIMD_CSET_LIMIT) {
    while (end >= sizeof(simde__m128i)) {
      simde__m128i bytes = simde_mm_loadu_si128((const simde__m128i *)(s.data + end - sizeof(simde__m128i)));
      unsigned int matches = (unsigned int)simde_mm_movemask_epi8(
          tstr_simd_cset_mask(bytes, cset, cset_len));

      if (matches == 0xFFFFU) {
        end -= sizeof(simde__m128i);
        continue;
      }
      while ((matches & 0x8000U) != 0U) {
        --end;
        matches <<= 1;
      }
      return tstr_v_from_buf(s.data, end);
    }
  }
  while (end > 0 && strchr(cset, s.data[end - 1]) != NULL)
    end--;
  return tstr_v_from_buf(s.data, end);
}

tstr_v tstr_v_split_next(tstr_v *rest, tstr_v delim) {
  if (!rest || rest->len == 0) return tstr_v_from_buf(NULL, 0);
  if (!tstr_v_valid(*rest)) {
    *rest = tstr_v_from_buf(NULL, 0);
    return tstr_v_from_buf(NULL, 0);
  }
  if (!tstr_v_valid(delim) || delim.len == 0) {
    tstr_v result = *rest;
    *rest = tstr_v_from_buf(rest->data + rest->len, 0);
    return result;
  }
  size_t pos = tstr_v_find(*rest, delim);
  if (pos == TSTR_V_NPOS) {
    tstr_v result = *rest;
    *rest = tstr_v_from_buf(rest->data + rest->len, 0);
    return result;
  }
  tstr_v result = tstr_v_from_buf(rest->data, pos);
  *rest = tstr_v_from_buf(rest->data + pos + delim.len, rest->len - pos - delim.len);
  return result;
}

tstr_v tstr_v_rsplit_next(tstr_v *rest, tstr_v delim) {
  size_t pos;

  if (!rest || rest->len == 0) return tstr_v_from_buf(NULL, 0);
  if (!tstr_v_valid(*rest)) {
    *rest = tstr_v_from_buf(NULL, 0);
    return tstr_v_from_buf(NULL, 0);
  }
  if (!tstr_v_valid(delim) || delim.len == 0) {
    tstr_v result = *rest;
    *rest = tstr_v_from_buf(rest->data, 0);
    return result;
  }
  pos = tstr_v_rfind(*rest, delim);
  if (pos == TSTR_V_NPOS) {
    tstr_v result = *rest;
    *rest = tstr_v_from_buf(rest->data, 0);
    return result;
  }
  tstr_v result = tstr_v_from_buf(rest->data + pos + delim.len,
                                   rest->len - pos - delim.len);
  *rest = tstr_v_from_buf(rest->data, pos);
  return result;
}

char *tstr_v_to_cstr(tstr_v v) { TSTR_V_COPY_TO(v, malloc(v.len + 1)); }

char *tstr_v_to_pool(tstr_v v, MemoryPool *pool) {
  if (!pool) return NULL;
  TSTR_V_COPY_TO(v, pool_alloc(pool, v.len + 1));
}

char *tstr_v_to_arena(tstr_v v, mem_pool_t *arena) {
  if (!arena) return NULL;
  TSTR_V_COPY_TO(v, mem_alloc(arena, v.len + 1));
}
