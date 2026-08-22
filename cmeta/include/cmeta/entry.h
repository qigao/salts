#ifndef CMETA_ENTRY_H
#define CMETA_ENTRY_H

#include <cmeta/cmeta.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A runtime associative entry. key/value borrow container storage when the
 * corresponding *_storage pointer is NULL. A copied entry owns independently
 * allocated key/value objects and keeps their allocation bases in *_storage. */
typedef struct cmeta_entry {
    const cmeta_type_desc *key_type;
    const cmeta_type_desc *value_type;
    const void *key;
    const void *value;
    void *key_storage;
    void *value_storage;
} cmeta_entry;

/* Hash-family entries compare/hash by key. Ordered-family entries compare by
 * key. Both share the same borrowed/owned copy, move and destroy semantics. */
extern const cmeta_type_desc cmeta_type_hash_entry;
extern const cmeta_type_desc cmeta_type_ordered_entry;

#ifdef __cplusplus
}
#endif

#endif /* CMETA_ENTRY_H */
