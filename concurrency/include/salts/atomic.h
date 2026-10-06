#ifndef SALTS_ATOMIC_H
#define SALTS_ATOMIC_H
#if defined(__cplusplus) || defined(__STDC_NO_ATOMICS__)
#error "Salts Atomic requires C11 atomics; use native C++ atomics in C++"
#endif
#include <salts/error_codes.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdbool.h>

/* The caller supplies every memory order. Invalid orders fail before touching
 * the atomic or output. Initialization is exclusive and precedes publication.
 * No lock-free guarantee: use the generated is_lock_free query to inspect the
 * native implementation. Atomic storage must never be copied while in use. */
static inline bool salts_atomic_order_valid(memory_order order) {
    return order == memory_order_relaxed || order == memory_order_consume ||
           order == memory_order_acquire || order == memory_order_release ||
           order == memory_order_acq_rel || order == memory_order_seq_cst;
}
static inline bool salts_atomic_load_order(memory_order order) {
    return order == memory_order_relaxed || order == memory_order_consume ||
           order == memory_order_acquire || order == memory_order_seq_cst;
}
static inline bool salts_atomic_store_order(memory_order order) {
    return order == memory_order_relaxed || order == memory_order_release || order == memory_order_seq_cst;
}
static inline bool salts_atomic_cas_orders(memory_order success, memory_order failure) {
    if (!salts_atomic_order_valid(success) || !salts_atomic_load_order(failure)) return false;
    if (failure == memory_order_relaxed) return true;
    if (failure == memory_order_consume)
        return success == memory_order_consume || success == memory_order_acquire ||
               success == memory_order_acq_rel || success == memory_order_seq_cst;
    if (failure == memory_order_acquire)
        return success == memory_order_acquire || success == memory_order_acq_rel || success == memory_order_seq_cst;
    return success == memory_order_seq_cst;
}

/* Declare one native C11 atomic Type and checked operations. All return
 * SALTS_OK/SALTS_EINVAL; valid CAS mismatch updates expected and returns OK.
 * Type/layout/lock-free support follow the compiler's native atomics. */
#define SALTS_TYPED_ATOMIC(name_, type_) \
    typedef struct name_ { _Atomic(type_) value; } name_; \
    static inline int name_##_init(name_ *p, type_ value) { \
        if (p == NULL) return SALTS_EINVAL; \
        atomic_init(&p->value, value); return SALTS_OK; \
    } \
    static inline int name_##_load(const name_ *p, memory_order order, type_ *out) { \
        if (p == NULL || out == NULL || !salts_atomic_load_order(order)) return SALTS_EINVAL; \
        *out = atomic_load_explicit(&p->value, order); return SALTS_OK; \
    } \
    static inline int name_##_store(name_ *p, type_ value, memory_order order) { \
        if (p == NULL || !salts_atomic_store_order(order)) return SALTS_EINVAL; \
        atomic_store_explicit(&p->value, value, order); return SALTS_OK; \
    } \
    static inline int name_##_exchange(name_ *p, type_ value, memory_order order, type_ *out) { \
        if (p == NULL || out == NULL || !salts_atomic_order_valid(order)) return SALTS_EINVAL; \
        *out = atomic_exchange_explicit(&p->value, value, order); return SALTS_OK; \
    } \
    static inline int name_##_compare_exchange(name_ *p, type_ *expected, type_ desired, \
        memory_order success, memory_order failure, bool *exchanged) { \
        if (p == NULL || expected == NULL || exchanged == NULL || \
            !salts_atomic_cas_orders(success, failure)) return SALTS_EINVAL; \
        *exchanged = atomic_compare_exchange_strong_explicit(&p->value, expected, desired, success, failure); \
        return SALTS_OK; \
    } \
    static inline int name_##_is_lock_free(const name_ *p, bool *out) { \
        if (p == NULL || out == NULL) return SALTS_EINVAL; \
        *out = atomic_is_lock_free(&p->value); return SALTS_OK; \
    } \
    typedef type_ name_##_value_type
#endif
