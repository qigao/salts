#ifndef CMETA_ACE_INTERCEPTOR_H
#define CMETA_ACE_INTERCEPTOR_H

#include <cmeta/compiler.h>
#include <cmeta/status.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * POSA2 Interceptor: a finite, caller-owned typed chain over an exact native
 * function signature. The declaration generates native type checking; it does
 * not synthesize a FunctionAbi, erase signatures, allocate, or keep a provider
 * or Plugin module alive.
 *
 * before hooks run forward; successful after hooks run in reverse order.
 * Errors (including a rejected before) unwind entered hooks in reverse order
 * through on_error. A short-circuit does not call the target or after hooks.
 *
 * Each hook and its context are borrowed for the whole invocation. The caller
 * keeps the provider/Plugin Scope live; callbacks must not throw/longjmp,
 * recurse into the same mutable chain, or modify the hooks while executing.
 * An error returned by the target leaves its partial output cleanup with the
 * target's documented native contract: this façade cannot invent ownership.
 */
#define CMETA_INTERCEPTOR_TYPE(name_, request_type_, response_type_) \
    typedef cmeta_status (*name_##_target_fn)( \
        void *, const request_type_ *, response_type_ *); \
    typedef cmeta_status (*name_##_before_fn)( \
        void *, const request_type_ *, bool *); \
    typedef void (*name_##_after_fn)( \
        void *, const request_type_ *, const response_type_ *); \
    typedef void (*name_##_error_fn)( \
        void *, const request_type_ *, cmeta_status); \
    typedef struct name_##_hook { \
        void *context; \
        name_##_before_fn before; \
        name_##_after_fn after; \
        name_##_error_fn on_error; \
    } name_##_hook; \
    typedef struct name_ { \
        void *target_context; \
        name_##_target_fn target; \
        const name_##_hook *hooks; \
        size_t hook_count; \
    } name_; \
    CMETA_INLINE cmeta_status name_##_invoke( \
        const name_ *chain, const request_type_ *request, response_type_ *response) { \
        size_t entered = 0u; \
        cmeta_status status = CMETA_OK; \
        if (chain == NULL || chain->target == NULL || request == NULL || \
            response == NULL || (chain->hook_count != 0u && chain->hooks == NULL)) \
            return CMETA_INVALID_ARGUMENT; \
        if (chain->hook_count > 16u) return CMETA_CAPACITY_EXCEEDED; \
        /* Prevalidate the entire borrowed chain before any side effect. */ \
        for (size_t index = 0u; index < chain->hook_count; ++index) { \
            const name_##_hook *hook = &chain->hooks[index]; \
            if (hook->before == NULL && hook->after == NULL && \
                hook->on_error == NULL) return CMETA_INVALID_ARGUMENT; \
        } \
        while (entered < chain->hook_count) { \
            const name_##_hook *hook = &chain->hooks[entered]; \
            bool proceed = true; \
            if (hook->before != NULL) \
                status = hook->before(hook->context, request, &proceed); \
            ++entered; \
            if (status != CMETA_OK || !proceed) { \
                if (status == CMETA_OK) status = CMETA_CALLBACK_ERROR; \
                break; \
            } \
        } \
        if (status == CMETA_OK) \
            status = chain->target(chain->target_context, request, response); \
        while (entered != 0u) { \
            const name_##_hook *hook = &chain->hooks[--entered]; \
            if (status == CMETA_OK) { \
                if (hook->after != NULL) \
                    hook->after(hook->context, request, response); \
            } else if (hook->on_error != NULL) { \
                hook->on_error(hook->context, request, status); \
            } \
        } \
        return status; \
    } \
    typedef int name_##_declaration_complete

#endif /* CMETA_ACE_INTERCEPTOR_H */
