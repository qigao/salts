#ifndef CMETA_ACE_SYNCHRONIZATION_H
#define CMETA_ACE_SYNCHRONIZATION_H

#include <cmeta/interface.h>
#include <cmeta/status.h>

/*
 * POSA2 Strategized Locking + Scoped Locking + Thread-Safe Interface.
 *
 * CMeta gives a canonical, compile-checked typed Interface {self,vtable}.
 * Platform/Concurrency remain the ONLY mutex, RWLock and condition owners.
 * A selected Lockable policy has exactly one owner and a non-failing unlock.
 * This is a borrowed view: the lock, vtable, and any provider/module must
 * outlive the guard and guarded call. Configure/switch policies only while
 * the protected object is quiescent.
 *
 * A guard protects ONE synchronous call, not arbitrary goto/longjmp or
 * async suspension. A C++ exception from the body releases before rethrow.
 * An inner "private" method must not reacquire the public nonrecursive gate.
 * Do not treat owner-affinity or FunctionDesc effects as a mutex guarantee.
 */
#define CMETA_ACE_LOCKABLE_METHODS(X, I) \
    X(I, FV0, void, acquire, stateful, &cmeta_type_void, CMETA_ABI_VOID) \
    X(I, FV0, void, release, stateful, &cmeta_type_void, CMETA_ABI_VOID)

CMETA_INTERFACE(cmeta_ace_lockable, CMETA_ACE_LOCKABLE_METHODS);

typedef struct cmeta_ace_guard {
    cmeta_ace_lockable lock;
    bool held;
} cmeta_ace_guard;

CMETA_INLINE cmeta_status cmeta_ace_guard_enter(
    cmeta_ace_guard *guard, const cmeta_ace_lockable *lock) {
    if (guard == NULL || lock == NULL || !cmeta_ace_lockable_valid(lock))
        return CMETA_INVALID_ARGUMENT;
    if (guard->held) return CMETA_BUSY;
    guard->lock = *lock;
    cmeta_ace_lockable_acquire(&guard->lock);
    guard->held = true;
    return CMETA_OK;
}

CMETA_INLINE cmeta_status cmeta_ace_guard_leave(cmeta_ace_guard *guard) {
    if (guard == NULL || !guard->held) return CMETA_INVALID_ARGUMENT;
    cmeta_ace_lockable_release(&guard->lock);
    guard->held = false;
    guard->lock = cmeta_ace_lockable_bind(NULL, NULL);
    return CMETA_OK;
}

#ifdef __cplusplus
#define CMETA_ACE_SYNCHRONIZED_DISPATCH_(guard_, status_, body_, arg_) \
    try { (status_) = (body_)(arg_); } \
    catch (...) { (void)cmeta_ace_guard_leave(&(guard_)); throw; }
#else
#define CMETA_ACE_SYNCHRONIZED_DISPATCH_(guard_, status_, body_, arg_) \
    (status_) = (body_)(arg_);
#endif

/* Typed public gate. The native body is the already-locked PRIVATE method:
 * static C type checking forbids passing callbacks for an unrelated state.
 * Each call selects an initialized Lockable policy without a new scheduler.
 */
#define CMETA_ACE_SYNCHRONIZED(name_, state_type_) \
    typedef cmeta_status (*name_##_body)(state_type_ *); \
    CMETA_INLINE cmeta_status name_##_run( \
        const cmeta_ace_lockable *policy, state_type_ *state, name_##_body body) { \
        cmeta_ace_guard guard = {0}; \
        cmeta_status status; \
        if (state == NULL || body == NULL) return CMETA_INVALID_ARGUMENT; \
        status = cmeta_ace_guard_enter(&guard, policy); \
        if (status != CMETA_OK) return status; \
        CMETA_ACE_SYNCHRONIZED_DISPATCH_(guard, status, body, state) \
        (void)cmeta_ace_guard_leave(&guard); \
        return status; \
    } \
    typedef int name_##_declaration_complete

#endif /* CMETA_ACE_SYNCHRONIZATION_H */
