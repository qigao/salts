#include "turbo_simd_scan.h"

#include <simde/x86/sse2.h>
#include <string.h>

static size_t turbo_scan_mask_first_u16(unsigned int mask) {
  size_t bit = 0;
  while (bit < 16u) {
    if (mask & (1u << bit)) return bit;
    ++bit;
  }
  return 16u;
}

const char *turbo_scan_char(const char *p, const char *end, char needle) {
  const simde__m128i needle_v = simde_mm_set1_epi8(needle);
  while ((size_t)(end - p) >= 16u) {
    const simde__m128i block = simde_mm_loadu_si128((const simde__m128i *)(const void *)p);
    const unsigned int mask =
        (unsigned int)simde_mm_movemask_epi8(simde_mm_cmpeq_epi8(block, needle_v));
    if (mask != 0u) return p + turbo_scan_mask_first_u16(mask);
    p += 16u;
  }

  while (p < end) {
    if (*p == needle) return p;
    ++p;
  }
  return NULL;
}

const char *turbo_scan_to_char(const char *p, const char *end, char needle) {
  const char *hit = turbo_scan_char(p, end, needle);
  return hit ? hit : end;
}

const char *turbo_scan_to_any2(const char *p, const char *end, char a, char b) {
  const simde__m128i a_v = simde_mm_set1_epi8(a);
  const simde__m128i b_v = simde_mm_set1_epi8(b);
  while ((size_t)(end - p) >= 16u) {
    const simde__m128i block = simde_mm_loadu_si128((const simde__m128i *)(const void *)p);
    const simde__m128i match =
        simde_mm_or_si128(simde_mm_cmpeq_epi8(block, a_v), simde_mm_cmpeq_epi8(block, b_v));
    const unsigned int mask = (unsigned int)simde_mm_movemask_epi8(match);
    if (mask != 0u) return p + turbo_scan_mask_first_u16(mask);
    p += 16u;
  }

  while (p < end) {
    if (*p == a || *p == b) return p;
    ++p;
  }
  return end;
}

const char *turbo_scan_to_any3(const char *p,
                               const char *end,
                               char a,
                               char b,
                               char c) {
  const simde__m128i a_v = simde_mm_set1_epi8(a);
  const simde__m128i b_v = simde_mm_set1_epi8(b);
  const simde__m128i c_v = simde_mm_set1_epi8(c);
  while ((size_t)(end - p) >= 16u) {
    const simde__m128i block = simde_mm_loadu_si128((const simde__m128i *)(const void *)p);
    const simde__m128i ab =
        simde_mm_or_si128(simde_mm_cmpeq_epi8(block, a_v), simde_mm_cmpeq_epi8(block, b_v));
    const simde__m128i match = simde_mm_or_si128(ab, simde_mm_cmpeq_epi8(block, c_v));
    const unsigned int mask = (unsigned int)simde_mm_movemask_epi8(match);
    if (mask != 0u) return p + turbo_scan_mask_first_u16(mask);
    p += 16u;
  }

  while (p < end) {
    if (*p == a || *p == b || *p == c) return p;
    ++p;
  }
  return end;
}

const char *turbo_scan_skip_sp_tab(const char *p, const char *end) {
  const simde__m128i sp_v = simde_mm_set1_epi8(' ');
  const simde__m128i tab_v = simde_mm_set1_epi8('\t');
  while ((size_t)(end - p) >= 16u) {
    const simde__m128i block = simde_mm_loadu_si128((const simde__m128i *)(const void *)p);
    const simde__m128i match =
        simde_mm_or_si128(simde_mm_cmpeq_epi8(block, sp_v), simde_mm_cmpeq_epi8(block, tab_v));
    const unsigned int mask = (unsigned int)simde_mm_movemask_epi8(match) & 0xffffu;
    if (mask == 0xffffu) {
      p += 16u;
      continue;
    }
    return p + turbo_scan_mask_first_u16((~mask) & 0xffffu);
  }

  while (p < end && (*p == ' ' || *p == '\t')) ++p;
  return p;
}

const char *turbo_scan_skip_sp_tab_cr_lf(const char *p, const char *end) {
  const simde__m128i sp_v = simde_mm_set1_epi8(' ');
  const simde__m128i tab_v = simde_mm_set1_epi8('\t');
  const simde__m128i cr_v = simde_mm_set1_epi8('\r');
  const simde__m128i lf_v = simde_mm_set1_epi8('\n');
  while ((size_t)(end - p) >= 16u) {
    const simde__m128i block = simde_mm_loadu_si128((const simde__m128i *)(const void *)p);
    const simde__m128i st =
        simde_mm_or_si128(simde_mm_cmpeq_epi8(block, sp_v), simde_mm_cmpeq_epi8(block, tab_v));
    const simde__m128i rn =
        simde_mm_or_si128(simde_mm_cmpeq_epi8(block, cr_v), simde_mm_cmpeq_epi8(block, lf_v));
    const unsigned int mask =
        (unsigned int)simde_mm_movemask_epi8(simde_mm_or_si128(st, rn)) & 0xffffu;
    if (mask == 0xffffu) {
      p += 16u;
      continue;
    }
    return p + turbo_scan_mask_first_u16((~mask) & 0xffffu);
  }

  while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
  return p;
}

const char *turbo_scan_mem(const char *haystack,
                           size_t haystack_len,
                           const char *needle,
                           size_t needle_len) {
  const char first = needle ? needle[0] : '\0';
  const char *cursor = haystack;
  const char *last;

  if (!haystack || !needle || needle_len == 0u) return haystack;
  if (haystack_len < needle_len) return NULL;

  last = haystack + haystack_len - needle_len + 1u;
  while (cursor < last) {
    const char *hit = turbo_scan_char(cursor, last, first);
    if (!hit) return NULL;
    if (memcmp(hit, needle, needle_len) == 0) return hit;
    cursor = hit + 1;
  }
  return NULL;
}
