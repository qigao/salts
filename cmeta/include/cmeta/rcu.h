#ifndef CMETA_RCU_H
#define CMETA_RCU_H
#include <cmeta/generic.h>
#include <salts/rcu.h>

/* Optional facade: link Salts::Concurrency and Salts::CMeta. Reclamation
 * returns ownership explicitly; the provider's owner then destroys the value. */
#define CMETA_GENERIC_KIND_Rcu CMETA_GENERIC_PROBE()
#define CMETA_TYPED_Rcu(name_, type_) \
    typedef struct name_ { salts_rcu domain; } name_; \
    typedef struct name_##_guard { salts_rcu_guard guard; } name_##_guard; \
    CMETA_INLINE int name_##_init(name_ *self, type_ *initial, size_t readers) { \
        return salts_rcu_init(self != NULL ? &self->domain : NULL, initial, readers); \
    } \
    CMETA_INLINE int name_##_read_lock(name_ *self, name_##_guard *guard) { \
        return salts_rcu_read_lock(self != NULL ? &self->domain : NULL, \
                                   guard != NULL ? &guard->guard : NULL); \
    } \
    CMETA_INLINE const type_ *name_##_load(const name_##_guard *guard) { \
        return (const type_ *)salts_rcu_load(guard != NULL ? &guard->guard : NULL); \
    } \
    CMETA_INLINE int name_##_read_unlock(name_##_guard *guard) { \
        return salts_rcu_read_unlock(guard != NULL ? &guard->guard : NULL); \
    } \
    CMETA_INLINE int name_##_replace(name_ *self, type_ *replacement) { \
        return salts_rcu_replace(self != NULL ? &self->domain : NULL, replacement); \
    } \
    CMETA_INLINE int name_##_try_reclaim(name_ *self, type_ **out) { \
        void *value = NULL; int status; \
        if (out == NULL) return SALTS_EINVAL; \
        *out = NULL; \
        status = salts_rcu_try_reclaim(self != NULL ? &self->domain : NULL, &value); \
        *out = (type_ *)value; return status; \
    } \
    CMETA_INLINE int name_##_close(name_ *self) { \
        return salts_rcu_close(self != NULL ? &self->domain : NULL); \
    } \
    CMETA_INLINE int name_##_destroy(name_ *self, type_ **out) { \
        void *value = NULL; int status; \
        if (out == NULL) return SALTS_EINVAL; \
        *out = NULL; \
        status = salts_rcu_destroy(self != NULL ? &self->domain : NULL, &value); \
        *out = (type_ *)value; return status; \
    } \
    typedef type_ name_##_value_type
#endif
