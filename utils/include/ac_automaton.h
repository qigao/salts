#ifndef SALTS_AC_AUTOMATON_H
#define SALTS_AC_AUTOMATON_H

#include "platform.h"
#include "salts_vstr.h"
#include "salts_error.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Aho-Corasick multi-pattern matcher for 8-bit bytes.
 *
 * Position semantics:
 * - start/end are byte offsets inside input text, using half-open [start, end).
 */
typedef struct ac_automaton_s ac_automaton_t;

/**
 * Aho-Corasick multi-pattern matcher for UTF-8 code points.
 *
 * Position semantics:
 * - start/end are code-point offsets inside input text, using half-open [start, end).
 */
typedef struct ac_utf8_automaton_s ac_utf8_automaton_t;

typedef bool (*ac_match_cb)(uint32_t pattern_id, size_t start, size_t end, void *user_data);

/* Byte automaton API */

/** Allocates an initialized byte automaton on heap. Caller owns the object. */
SALTS_C_API ac_automaton_t *ac_automaton_create(void);

/** Destroys and frees a heap-allocated byte automaton. */
SALTS_C_API void ac_automaton_free(ac_automaton_t *ac);

/** Initializes an empty automaton for raw-byte matching. */
SALTS_C_API int ac_automaton_init(ac_automaton_t *ac);

/** Releases automaton memory. Safe on NULL/zero-initialized objects. */
SALTS_C_API void ac_automaton_destroy(ac_automaton_t *ac);

/**
 * Adds a pattern and returns its stable pattern_id.
 *
 *  - On success, pattern_id is written when non-NULL.
 *  - The automaton is marked dirty; call ac_automaton_build() before matching again.
 */
SALTS_C_API int ac_automaton_add_pattern(ac_automaton_t *ac, vstr pattern,
                                       uint32_t *pattern_id);

/**
 * Builds fail links. Must be called after adding all patterns.
 *
 * Returns SALTS_OK on success.
 */
SALTS_C_API int ac_automaton_build(ac_automaton_t *ac);

/**
 * Runs matching over byte text.
 *
 * Callback is invoked for every matched pattern instance with pattern_id/start/end.
 * Return false in callback to stop scanning early.
 */
SALTS_C_API int ac_automaton_match(const ac_automaton_t *ac, vstr text, ac_match_cb cb,
                                 void *user_data);

/**
 * Number of added patterns.
 */
SALTS_C_API uint32_t ac_automaton_pattern_count(const ac_automaton_t *ac);

/* UTF-8 automaton API */

/** Allocates an initialized UTF-8 automaton on heap. Caller owns the object. */
SALTS_C_API ac_utf8_automaton_t *ac_utf8_automaton_create(void);

/** Destroys and frees a heap-allocated UTF-8 automaton. */
SALTS_C_API void ac_utf8_automaton_free(ac_utf8_automaton_t *ac);

/** Initializes an empty automaton for UTF-8 code-point matching. */
SALTS_C_API int ac_utf8_automaton_init(ac_utf8_automaton_t *ac);

/** Releases UTF-8 automaton memory. Safe on NULL/zero-initialized objects. */
SALTS_C_API void ac_utf8_automaton_destroy(ac_utf8_automaton_t *ac);

/**
 * Adds a UTF-8 pattern and returns its stable pattern_id.
 *
 * Input must be valid UTF-8.
 */
SALTS_C_API int ac_utf8_automaton_add_pattern(ac_utf8_automaton_t *ac, vstr pattern,
                                           uint32_t *pattern_id);

/** Builds fail links. Must be called after adding all patterns. */
SALTS_C_API int ac_utf8_automaton_build(ac_utf8_automaton_t *ac);

/**
 * Runs matching over UTF-8 text. Input text must be valid UTF-8.
 *
 * Callback is invoked for every matched pattern instance with pattern_id/start/end.
 * start/end are code-point offsets.
 */
SALTS_C_API int ac_utf8_automaton_match(const ac_utf8_automaton_t *ac, vstr text, ac_match_cb cb,
                                     void *user_data);

/** Number of added UTF-8 patterns. */
SALTS_C_API uint32_t ac_utf8_automaton_pattern_count(const ac_utf8_automaton_t *ac);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_AC_AUTOMATON_H */
