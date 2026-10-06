#ifndef CMETA_ATOMIC_H
#define CMETA_ATOMIC_H
#if defined(__cplusplus) || defined(__STDC_NO_ATOMICS__)
#error "CMeta Atomic requires C11 atomics; use native C++ atomics in C++"
#endif
#include <cmeta/generic.h>
#include <cmeta/status.h>
#include <stdatomic.h>
#include <stdbool.h>

/* The caller supplies every memory order. Invalid orders fail before touching
 * the atomic or output. Initialization is exclusive and precedes publication.
 * No lock-free guarantee: use the generated is_lock_free query to inspect the
 * native implementation. Atomic storage must never be copied while in use. */
CMETA_INLINE bool cmeta_atomic_order_valid(memory_order order) {
    return order == memory_order_relaxed || order == memory_order_consume ||
           order == memory_order_acquire || order == memory_order_release ||
           order == memory_order_acq_rel || order == memory_order_seq_cst;
}
CMETA_INLINE bool cmeta_atomic_load_order(memory_order order) {
    return order == memory_order_relaxed || order == memory_order_consume ||
           order == memory_order_acquire || order == memory_order_seq_cst;
}
CMETA_INLINE bool cmeta_atomic_store_order(memory_order order) {
    return order == memory_order_relaxed || order == memory_order_release || order == memory_order_seq_cst;
}
CMETA_INLINE bool cmeta_atomic_cas_orders(memory_order success, memory_order failure) {
    if (!cmeta_atomic_order_valid(success) || !cmeta_atomic_load_order(failure)) return false;
    if (failure == memory_order_relaxed) return true;
    if (failure == memory_order_consume)
        return success == memory_order_consume || success == memory_order_acquire ||
               success == memory_order_acq_rel || success == memory_order_seq_cst;
    if (failure == memory_order_acquire)
        return success == memory_order_acquire || success == memory_order_acq_rel || success == memory_order_seq_cst;
    return success == memory_order_seq_cst;
}

#define CMETA_GENERIC_KIND_Atomic CMETA_GENERIC_PROBE()
#define CMETA_TYPED_Atomic(name_, type_) \
    typedef struct name_ { _Atomic(type_) value; } name_; \
    CMETA_INLINE cmeta_status name_##_init(name_ *p, type_ value) { \
        if (p == NULL) return CMETA_INVALID_ARGUMENT; \
        atomic_init(&p->value, value); return CMETA_OK; \
    } \
    CMETA_INLINE cmeta_status name_##_load(const name_ *p, memory_order order, type_ *out) { \
        if (p == NULL || out == NULL || !cmeta_atomic_load_order(order)) return CMETA_INVALID_ARGUMENT; \
        *out = atomic_load_explicit(&p->value, order); return CMETA_OK; \
    } \
    CMETA_INLINE cmeta_status name_##_store(name_ *p, type_ value, memory_order order) { \
        if (p == NULL || !cmeta_atomic_store_order(order)) return CMETA_INVALID_ARGUMENT; \
        atomic_store_explicit(&p->value, value, order); return CMETA_OK; \
    } \
    CMETA_INLINE cmeta_status name_##_exchange(name_ *p, type_ value, memory_order order, type_ *out) { \
        if (p == NULL || out == NULL || !cmeta_atomic_order_valid(order)) return CMETA_INVALID_ARGUMENT; \
        *out = atomic_exchange_explicit(&p->value, value, order); return CMETA_OK; \
    } \
    CMETA_INLINE cmeta_status name_##_compare_exchange(name_ *p, type_ *expected, type_ desired, \
        memory_order success, memory_order failure, bool *exchanged) { \
        if (p == NULL || expected == NULL || exchanged == NULL || \
            !cmeta_atomic_cas_orders(success, failure)) return CMETA_INVALID_ARGUMENT; \
        *exchanged = atomic_compare_exchange_strong_explicit(&p->value, expected, desired, success, failure); \
        return CMETA_OK; \
    } \
    CMETA_INLINE cmeta_status name_##_is_lock_free(const name_ *p, bool *out) { \
        if (p == NULL || out == NULL) return CMETA_INVALID_ARGUMENT; \
        *out = atomic_is_lock_free(&p->value); return CMETA_OK; \
    } \
    typedef type_ name_##_value_type
#endif
