#ifndef CMETA_NATIVE_THUNK_H
#define CMETA_NATIVE_THUNK_H
#include <cmeta/function.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*cmeta_native_i32_fn)(int);
typedef int (*cmeta_native_context_i32_fn)(void *, int);
typedef enum cmeta_native_kind {
    CMETA_NATIVE_INVALID, CMETA_NATIVE_I32, CMETA_NATIVE_CONTEXT_I32
} cmeta_native_kind;

/* Borrowed admission token; initialize only with admit, then keep immutable.
 * Metadata, target, receiver and any outer module lease must outlive use. */
typedef struct cmeta_native_binding {
    const cmeta_function_abi_desc *abi;
    cmeta_native_kind kind;
    cmeta_native_i32_fn direct;
    cmeta_native_context_i32_fn contextual;
    void *context;
} cmeta_native_binding;
#define CMETA_NATIVE_BINDING_INIT { NULL, CMETA_NATIVE_INVALID, NULL, NULL, NULL }

typedef enum cmeta_native_state {
    CMETA_NATIVE_EMPTY, CMETA_NATIVE_WRITABLE, CMETA_NATIVE_READY, CMETA_NATIVE_UNPUBLISHED
} cmeta_native_state;

/* Unique caller-owned handle. Zero-initialize; never copy a live handle.
 * Create/rebind/destroy require exclusive access and no outstanding calls.
 * READY permits concurrent calls if the exact target permits them. OS errors
 * are retained in platform_error (GetLastError/errno), without changing ABI. */
typedef struct cmeta_native_thunk {
    void *allocation;
    size_t allocation_size;
    const cmeta_function_abi_desc *abi;
    cmeta_native_state state;
    unsigned long platform_error;
} cmeta_native_thunk;
#define CMETA_NATIVE_THUNK_INIT { NULL, 0, NULL, CMETA_NATIVE_EMPTY, 0 }

/* Admit exactly int(int), or its explicit borrowed receiver projection.
 * Wrong/unspecified ABI is TYPE_MISMATCH; invalid arguments are rejected and
 * clear out. These functions do not allocate or invoke/retain the target. */
cmeta_status cmeta_native_i32_admit(const cmeta_function_abi_desc *abi,
    cmeta_native_i32_fn target, cmeta_native_binding *out);
cmeta_status cmeta_native_context_i32_admit(const cmeta_function_abi_desc *source,
    const cmeta_function_abi_desc *projected, cmeta_native_context_i32_fn target,
    void *context, cmeta_native_binding *out);

/* One OS allocation unit per thunk, bounded by max_code_bytes (including
 * Windows reservation granularity or POSIX page rounding).
 * Creation requires EMPTY; live output returns BUSY. Permission failures
 * return CALLBACK_ERROR and retain an unpublished allocation for destroy.
 * No RWX, hidden allocation fallback, module retention or ABI reconstruction. */
cmeta_status cmeta_native_thunk_create(const cmeta_native_binding *binding,
    size_t max_code_bytes, cmeta_native_thunk *out);
/* Rebind preserves entry address and requires an ABI-compatible admitted token.
 * Revoke all borrowed entries and quiesce callers first. A publication failure
 * makes entry() NULL until successful rebind; destroy is always permitted. */
cmeta_status cmeta_native_thunk_rebind(cmeta_native_thunk *thunk,
    const cmeta_native_binding *binding);
/* Borrow until rebind/destroy begins. NULL unless successfully published. */
cmeta_native_i32_fn cmeta_native_thunk_entry(const cmeta_native_thunk *thunk);
/* Idempotent on EMPTY; failed OS release preserves the handle for retry. */
cmeta_status cmeta_native_thunk_destroy(cmeta_native_thunk *thunk);

#ifdef __cplusplus
}
#endif
#endif
