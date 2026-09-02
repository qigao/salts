#ifndef ROCIDA_LEVENSHTEIN_AUTOMATON_H
#define ROCIDA_LEVENSHTEIN_AUTOMATON_H

#include "platform.h"
#include "turbo_vstr.h"
#include "turbo_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Levenshtein automaton for byte patterns.
 */
typedef struct lev_automaton_s lev_automaton_t;

/**
 * Levenshtein automaton for UTF-8 code-point patterns.
 */
typedef struct lev_utf8_automaton_s lev_utf8_automaton_t;

/**
 * Returns:
 *   - pattern [start, end) in the searched string
 *   - distance is the computed edit distance
 *   - Return false from callback to stop searching early
 */
typedef bool (*levenshtein_match_cb)(size_t start, size_t end, size_t distance, void *user_data);

/** Allocates a byte Levenshtein automaton on heap. Caller owns the object. */
TURBO_C_API lev_automaton_t *lev_automaton_create(void);

/** Destroys and frees a heap-allocated byte Levenshtein automaton. */
TURBO_C_API void lev_automaton_free(lev_automaton_t *lev);

/** Initializes a byte Levenshtein automaton. */
TURBO_C_API int lev_automaton_init(lev_automaton_t *lev, vstr pattern, size_t max_distance);

/** Releases automaton memory. */
TURBO_C_API void lev_automaton_destroy(lev_automaton_t *lev);

/**
 * Runs byte-pattern search over text.
 *
 * pattern is fixed at initialization.
 */
TURBO_C_API int lev_automaton_match(const lev_automaton_t *lev, vstr text,
                                  levenshtein_match_cb cb, void *user_data);

/** Initializes a UTF-8 Levenshtein automaton. Pattern must be valid UTF-8. */
TURBO_C_API int lev_utf8_automaton_init(lev_utf8_automaton_t *lev, vstr pattern,
                                      size_t max_distance);

/** Allocates a UTF-8 Levenshtein automaton on heap. Caller owns the object. */
TURBO_C_API lev_utf8_automaton_t *lev_utf8_automaton_create(void);

/** Destroys and frees a heap-allocated UTF-8 Levenshtein automaton. */
TURBO_C_API void lev_utf8_automaton_free(lev_utf8_automaton_t *lev);

/** Releases UTF-8 automaton memory. */
TURBO_C_API void lev_utf8_automaton_destroy(lev_utf8_automaton_t *lev);

/** Runs UTF-8 code-point search. Text must be valid UTF-8. */
TURBO_C_API int lev_utf8_automaton_match(const lev_utf8_automaton_t *lev, vstr text,
                                       levenshtein_match_cb cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* ROCIDA_LEVENSHTEIN_AUTOMATON_H */
