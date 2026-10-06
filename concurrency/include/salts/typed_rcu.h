#ifndef SALTS_TYPED_RCU_H
#define SALTS_TYPED_RCU_H
#include <salts/rcu.h>

/* Typed projection over salts_rcu; link Salts::Concurrency only. No metadata,
 * allocation, reclamation policy or lifecycle callback is added. Reclaim and
 * destroy return ownership explicitly; the caller then destroys the payload.
 * Domain/guard lifetime, capacity and concurrency follow salts/rcu.h. */
#ifdef __cplusplus
#define SALTS_TYPED_RCU_CAST_(type_, value_) static_cast<type_ *>(value_)
#else
#define SALTS_TYPED_RCU_CAST_(type_, value_) ((type_ *)(value_))
#endif
#define SALTS_TYPED_RCU(name_, type_) \
    typedef struct name_ { salts_rcu domain; } name_; \
    typedef struct name_##_guard { salts_rcu_guard guard; } name_##_guard; \
    static inline int name_##_init(name_ *self, type_ *initial, size_t readers) { \
        return salts_rcu_init(self != NULL ? &self->domain : NULL, initial, readers); \
    } \
    static inline int name_##_read_lock(name_ *self, name_##_guard *guard) { \
        return salts_rcu_read_lock(self != NULL ? &self->domain : NULL, \
                                   guard != NULL ? &guard->guard : NULL); \
    } \
    static inline const type_ *name_##_load(const name_##_guard *guard) { \
        return SALTS_TYPED_RCU_CAST_(const type_, salts_rcu_load(guard != NULL ? &guard->guard : NULL)); \
    } \
    static inline int name_##_read_unlock(name_##_guard *guard) { \
        return salts_rcu_read_unlock(guard != NULL ? &guard->guard : NULL); \
    } \
    static inline int name_##_replace(name_ *self, type_ *replacement) { \
        return salts_rcu_replace(self != NULL ? &self->domain : NULL, replacement); \
    } \
    static inline int name_##_try_reclaim(name_ *self, type_ **out) { \
        void *value = NULL; int status; \
        if (out == NULL) return SALTS_EINVAL; \
        *out = NULL; \
        status = salts_rcu_try_reclaim(self != NULL ? &self->domain : NULL, &value); \
        *out = SALTS_TYPED_RCU_CAST_(type_, value); return status; \
    } \
    static inline int name_##_close(name_ *self) { \
        return salts_rcu_close(self != NULL ? &self->domain : NULL); \
    } \
    static inline int name_##_destroy(name_ *self, type_ **out) { \
        void *value = NULL; int status; \
        if (out == NULL) return SALTS_EINVAL; \
        *out = NULL; \
        status = salts_rcu_destroy(self != NULL ? &self->domain : NULL, &value); \
        *out = SALTS_TYPED_RCU_CAST_(type_, value); return status; \
    } \
    typedef type_ name_##_value_type
#endif
