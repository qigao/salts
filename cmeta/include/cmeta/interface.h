#ifndef CMETA_META_INTERFACE_H
#define CMETA_META_INTERFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmeta/pp.h>
#include <cmeta/function.h>

/*
 * CMeta Interface/Object Protocol
 * -------------------------------
 *
 * Natural declaration vocabulary is intentional:
 *
 *   interface(name, METHODS)
 *   implements(name, implementation, capabilities, ...)
 *
 * Generated public C symbols remain namespaced by the interface name itself
 * (normally cmeta_* or cflow_*).  There is no class/inheritance model here:
 * an interface value is only { self, vtable } plus capability metadata.
 *
 * A method schema is a single X-list replayed by interface() for the vtable,
 * inline wrappers, and translation-unit-local reflection metadata.  Row kind
 * encodes return-kind + arity:
 *
 *   R0..R4  non-void return, 0..4 arguments after self
 *   V0..V4  void return,     0..4 arguments after self
 *   D0       owning destructor; V0 ABI plus handle invalidation after return
 *
 * Example:
 *
 *   #define WAITABLE_METHODS(X,I) \
 *       X(I,R1,bool,arm,sample_waker,waker) \
 *       X(I,V0,void,cancel,_)
 *
 *   interface(sample_waitable, WAITABLE_METHODS);
 */

typedef uint32_t cmeta_interface_method_flags;

enum {
    CMETA_INTERFACE_METHOD_NONE = 0u,
    /* Dispatch invalidates the owning interface handle after the call. */
    CMETA_INTERFACE_METHOD_OWNS_SELF = 1u << 0,
    CMETA_INTERFACE_METHOD_FLAG_MASK = CMETA_INTERFACE_METHOD_OWNS_SELF
};

typedef struct cmeta_interface_method_desc {
    size_t size;
    const char *name;
    /* Exact dispatch shape retained even for legacy ABI-only rows. */
    unsigned dispatch_arity;
    cmeta_interface_method_flags flags;
    /* Canonical CMeta function semantics; NULL for legacy ABI-only rows. */
    const cmeta_function_desc *function;
    const cmeta_function_abi_desc *abi;
} cmeta_interface_method_desc;

typedef struct cmeta_interface_desc {
    size_t size;
    const char *name;
    const cmeta_interface_method_desc *methods;
    size_t method_count;
} cmeta_interface_desc;

CMETA_INLINE const cmeta_function_desc *
cmeta_interface_method_function(const cmeta_interface_method_desc *method) {
    return method != NULL ? method->function : NULL;
}

CMETA_INLINE const cmeta_function_abi_desc *
cmeta_interface_method_abi(const cmeta_interface_method_desc *method) {
    return method != NULL ? method->abi : NULL;
}

CMETA_INLINE size_t
cmeta_interface_method_arity(const cmeta_interface_method_desc *method) {
    if (method == NULL) return 0u;
    return method->function != NULL ? method->function->param_count
                                    : (size_t)method->dispatch_arity;
}

CMETA_INLINE bool
cmeta_interface_method_reflection_valid(const cmeta_interface_method_desc *method) {
    return method != NULL && method->size >= sizeof(*method) &&
           method->name != NULL && method->name[0] != '\0' &&
           method->function != NULL && method->abi != NULL &&
           cmeta_function_desc_valid(method->function) &&
           cmeta_function_abi_desc_valid(method->abi) &&
           method->abi->function == method->function &&
           method->function->param_count == (size_t)method->dispatch_arity &&
           (method->flags & ~CMETA_INTERFACE_METHOD_FLAG_MASK) == 0u;
}

CMETA_INLINE bool
cmeta_interface_desc_valid(const cmeta_interface_desc *desc) {
    size_t i;
    if (desc == NULL || desc->size < sizeof(*desc) ||
        desc->name == NULL || desc->name[0] == '\0' ||
        (desc->method_count != 0u && desc->methods == NULL))
        return false;
    for (i = 0u; i < desc->method_count; ++i) {
        const cmeta_interface_method_desc *method = &desc->methods[i];
        if (method->size < sizeof(*method) ||
            method->name == NULL || method->name[0] == '\0' ||
            method->dispatch_arity > 4u ||
            (method->flags & ~CMETA_INTERFACE_METHOD_FLAG_MASK) != 0u)
            return false;
        if ((method->function == NULL) != (method->abi == NULL))
            return false;
        if (method->function != NULL &&
            !cmeta_interface_method_reflection_valid(method))
            return false;
    }
    return true;
}

#define CMETA_IFACE_ARITY_R0 0u
#define CMETA_IFACE_ARITY_R1 1u
#define CMETA_IFACE_ARITY_R2 2u
#define CMETA_IFACE_ARITY_R3 3u
#define CMETA_IFACE_ARITY_R4 4u
#define CMETA_IFACE_ARITY_V0 0u
#define CMETA_IFACE_ARITY_V1 1u
#define CMETA_IFACE_ARITY_V2 2u
#define CMETA_IFACE_ARITY_V3 3u
#define CMETA_IFACE_ARITY_V4 4u
#define CMETA_IFACE_ARITY_D0 0u

/* vtable fields */
#define CMETA_IFACE_VT_R0(I,R,N,_) R (*N)(void *self);
#define CMETA_IFACE_VT_R1(I,R,N,T1,A1) R (*N)(void *self, T1 A1);
#define CMETA_IFACE_VT_R2(I,R,N,T1,A1,T2,A2) R (*N)(void *self, T1 A1, T2 A2);
#define CMETA_IFACE_VT_R3(I,R,N,T1,A1,T2,A2,T3,A3) R (*N)(void *self, T1 A1, T2 A2, T3 A3);
#define CMETA_IFACE_VT_R4(I,R,N,T1,A1,T2,A2,T3,A3,T4,A4) R (*N)(void *self, T1 A1, T2 A2, T3 A3, T4 A4);
#define CMETA_IFACE_VT_V0(I,R,N,_) void (*N)(void *self);
#define CMETA_IFACE_VT_V1(I,R,N,T1,A1) void (*N)(void *self, T1 A1);
#define CMETA_IFACE_VT_V2(I,R,N,T1,A1,T2,A2) void (*N)(void *self, T1 A1, T2 A2);
#define CMETA_IFACE_VT_V3(I,R,N,T1,A1,T2,A2,T3,A3) void (*N)(void *self, T1 A1, T2 A2, T3 A3);
#define CMETA_IFACE_VT_V4(I,R,N,T1,A1,T2,A2,T3,A3,T4,A4) void (*N)(void *self, T1 A1, T2 A2, T3 A3, T4 A4);
#define CMETA_IFACE_VT_D0(I,R,N,_) void (*N)(void *self);
#define CMETA_IFACE_VT_ROW(I,K,R,N,...) CMETA_PP_CAT(CMETA_IFACE_VT_,K)(I,R,N,__VA_ARGS__)

/* wrapper definitions. D0 is an owning zero-argument destructor: it preserves
 * the V0 vtable ABI and clears the interface handle after dispatch returns. */
#define CMETA_IFACE_IMPL_R0(I,R,N,_) CMETA_INLINE R I##_##N(I *self) { return self->vtable->N(self->self); }
#define CMETA_IFACE_IMPL_R1(I,R,N,T1,A1) CMETA_INLINE R I##_##N(I *self, T1 A1) { return self->vtable->N(self->self, A1); }
#define CMETA_IFACE_IMPL_R2(I,R,N,T1,A1,T2,A2) CMETA_INLINE R I##_##N(I *self, T1 A1, T2 A2) { return self->vtable->N(self->self, A1, A2); }
#define CMETA_IFACE_IMPL_R3(I,R,N,T1,A1,T2,A2,T3,A3) CMETA_INLINE R I##_##N(I *self, T1 A1, T2 A2, T3 A3) { return self->vtable->N(self->self, A1, A2, A3); }
#define CMETA_IFACE_IMPL_R4(I,R,N,T1,A1,T2,A2,T3,A3,T4,A4) CMETA_INLINE R I##_##N(I *self, T1 A1, T2 A2, T3 A3, T4 A4) { return self->vtable->N(self->self, A1, A2, A3, A4); }
#define CMETA_IFACE_IMPL_V0(I,R,N,_) CMETA_INLINE void I##_##N(I *self) { self->vtable->N(self->self); }
#define CMETA_IFACE_IMPL_V1(I,R,N,T1,A1) CMETA_INLINE void I##_##N(I *self, T1 A1) { self->vtable->N(self->self, A1); }
#define CMETA_IFACE_IMPL_V2(I,R,N,T1,A1,T2,A2) CMETA_INLINE void I##_##N(I *self, T1 A1, T2 A2) { self->vtable->N(self->self, A1, A2); }
#define CMETA_IFACE_IMPL_V3(I,R,N,T1,A1,T2,A2,T3,A3) CMETA_INLINE void I##_##N(I *self, T1 A1, T2 A2, T3 A3) { self->vtable->N(self->self, A1, A2, A3); }
#define CMETA_IFACE_IMPL_V4(I,R,N,T1,A1,T2,A2,T3,A3,T4,A4) CMETA_INLINE void I##_##N(I *self, T1 A1, T2 A2, T3 A3, T4 A4) { self->vtable->N(self->self, A1, A2, A3, A4); }
#define CMETA_IFACE_IMPL_D0(I,R,N,_) \
    CMETA_INLINE void I##_##N(I *self) { \
        if (!self || !self->self || !self->vtable || !self->vtable->N) return; \
        self->vtable->N(self->self); \
        self->self = NULL; \
        self->vtable = NULL; \
    }
#define CMETA_IFACE_IMPL_ROW(I,K,R,N,...) CMETA_PP_CAT(CMETA_IFACE_IMPL_,K)(I,R,N,__VA_ARGS__)

/* required-method validation */
#define CMETA_IFACE_VALID_R0(I,R,N,_) && self->vtable->N != NULL
#define CMETA_IFACE_VALID_R1(I,R,N,T1,A1) && self->vtable->N != NULL
#define CMETA_IFACE_VALID_R2(I,R,N,T1,A1,T2,A2) && self->vtable->N != NULL
#define CMETA_IFACE_VALID_R3(I,R,N,T1,A1,T2,A2,T3,A3) && self->vtable->N != NULL
#define CMETA_IFACE_VALID_R4(I,R,N,T1,A1,T2,A2,T3,A3,T4,A4) && self->vtable->N != NULL
#define CMETA_IFACE_VALID_V0(I,R,N,_) && self->vtable->N != NULL
#define CMETA_IFACE_VALID_V1(I,R,N,T1,A1) && self->vtable->N != NULL
#define CMETA_IFACE_VALID_V2(I,R,N,T1,A1,T2,A2) && self->vtable->N != NULL
#define CMETA_IFACE_VALID_V3(I,R,N,T1,A1,T2,A2,T3,A3) && self->vtable->N != NULL
#define CMETA_IFACE_VALID_V4(I,R,N,T1,A1,T2,A2,T3,A3,T4,A4) && self->vtable->N != NULL
#define CMETA_IFACE_VALID_D0(I,R,N,_) && self->vtable->N != NULL
#define CMETA_IFACE_VALID_ROW(I,K,R,N,...) CMETA_PP_CAT(CMETA_IFACE_VALID_,K)(I,R,N,__VA_ARGS__)

#define CMETA_IFACE_META_ROW(I,K,R,N,...) { #N, #R, CMETA_PP_CAT(CMETA_IFACE_ARITY_,K) },

#define CMETA_INTERFACE(I, METHODS) \
    typedef struct I I; \
    typedef struct I##_vtable I##_vtable; \
    struct I##_vtable { \
        const char *implementation; \
        uint64_t capabilities; \
        METHODS(CMETA_IFACE_VT_ROW, I) \
    }; \
    struct I { void *self; const I##_vtable *vtable; }; \
    CMETA_LOCAL const cmeta_interface_method_desc I##_method_meta[] = { METHODS(CMETA_IFACE_META_ROW, I) }; \
    CMETA_LOCAL const cmeta_interface_desc I##_interface_meta = { #I, I##_method_meta, sizeof(I##_method_meta)/sizeof(I##_method_meta[0]) }; \
    METHODS(CMETA_IFACE_IMPL_ROW, I) \
    CMETA_INLINE I I##_bind(void *self, const I##_vtable *vtable) { I out = { self, vtable }; return out; } \
    CMETA_INLINE bool I##_valid(const I *self) { \
        return self && self->self && self->vtable METHODS(CMETA_IFACE_VALID_ROW, I); \
    } \
    CMETA_INLINE const char *I##_implementation(const I *self) { return I##_valid(self) && self->vtable->implementation ? self->vtable->implementation : "none"; } \
    CMETA_INLINE uint64_t I##_capabilities(const I *self) { return I##_valid(self) ? self->vtable->capabilities : 0u; } \
    CMETA_INLINE bool I##_has(const I *self, uint64_t capability) { return (I##_capabilities(self) & capability) == capability; } \
    CMETA_INLINE const cmeta_interface_desc *I##_interface(void) { return &I##_interface_meta; } \
    typedef int I##_interface_anchor_t

/* Bind a conventional C implementation to an interface.  Method functions use
 * the interface ABI directly: first parameter is void *self.  Implementations
 * cast self to their concrete state type internally. */
#define CMETA_IMPLEMENTS(I, NAME, CAPS, ...) \
    CMETA_LOCAL const I##_vtable NAME##_vtable = { \
        .implementation = #NAME, \
        .capabilities = (uint64_t)(CAPS), \
        __VA_ARGS__ \
    }; \
    static I NAME##_as_##I(void *self) { return I##_bind(self, &NAME##_vtable); } \
    typedef int NAME##_implements_anchor_t


/* Natural DSL spellings are the default when the host headers have not already
 * claimed them (notably some Windows/COM environments define `interface`).
 * Framework/internal code should use the collision-safe CMETA_* spellings. */
#ifndef CMETA_NO_NATURAL_INTERFACE_NAMES
#  ifndef interface
#    define interface(...) CMETA_INTERFACE(__VA_ARGS__)
#  endif
#  ifndef implements
#    define implements(...) CMETA_IMPLEMENTS(__VA_ARGS__)
#  endif
#endif

#endif
