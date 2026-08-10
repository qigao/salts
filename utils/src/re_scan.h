#ifndef TURBO_RE_SCAN_H
#define TURBO_RE_SCAN_H

#include <simde/x86/sse2.h>

#include <stddef.h>

/* Internal helper for re.c (not installed as a public header).
 * Returns a pointer to the first byte equal to needle in [p, end), or NULL.
 * The SIMDe variant scans 16 bytes per step and keeps a scalar fallback for
 * the unaligned tail and for non-x86 builds where SIMDe lowers to portable C.
 * The scalar variant is kept for equivalence tests and benchmarks. */
static inline const char *re_scan_first_byte_scalar(const char *p, const char *end,
                                                    unsigned char needle) {
  while (p < end) {
    if ((unsigned char)*p == needle) return p;
    ++p;
  }
  return NULL;
}

static inline const char *re_scan_first_byte_simde(const char *p, const char *end,
                                                   unsigned char needle) {
  const simde__m128i target = simde_mm_set1_epi8((char)needle);
  while ((size_t)(end - p) >= sizeof(simde__m128i)) {
    const simde__m128i bytes = simde_mm_loadu_si128((const simde__m128i *)p);
    const simde__m128i equal = simde_mm_cmpeq_epi8(bytes, target);
    unsigned int mask = (unsigned int)simde_mm_movemask_epi8(equal);
    if (mask != 0U) {
      unsigned int offset = 0;
      while ((mask & 1U) == 0U) {
        ++offset;
        mask >>= 1;
      }
      return p + offset;
    }
    p += sizeof(simde__m128i);
  }
  return re_scan_first_byte_scalar(p, end, needle);
}

#endif /* TURBO_RE_SCAN_H */
