#ifndef CMETA_META_INTERFACE_H
#define CMETA_META_INTERFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmeta/pp.h>

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
 *
 * Example:
 *
 *   #define WAITABLE_METHODS(X,I) \
 *       X(I,R1,bool,arm,sample_waker,waker) \
 *       X(I,V0,void,cancel,_)
 *
 *   interface(sample_waitable, WAITABLE_METHODS);
 */

typedef struct cmeta_interface_method_desc {
    const char *name;
    const char *return_type;
    unsigned arity;
} cmeta_interface_method_desc;

typedef struct cmeta_interface_desc {
    const char *name;
    const cmeta_interface_method_desc *methods;
    size_t method_count;
} cmeta_interface_desc;

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
#define CMETA_IFACE_VT_ROW(I,K,R,N,...) CMETA_PP_CAT(CMETA_IFACE_VT_,K)(I,R,N,__VA_ARGS__)

/* wrapper definitions */
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
#define CMETA_IFACE_IMPL_ROW(I,K,R,N,...) CMETA_PP_CAT(CMETA_IFACE_IMPL_,K)(I,R,N,__VA_ARGS__)

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
    CMETA_INLINE bool I##_valid(const I *self) { return self && self->self && self->vtable; } \
    CMETA_INLINE const char *I##_implementation(const I *self) { return I##_valid(self) && self->vtable->implementation ? self->vtable->implementation : "none"; } \
    CMETA_INLINE uint64_t I##_capabilities(const I *self) { return I##_valid(self) ? self->vtable->capabilities : 0u; } \
    CMETA_INLINE bool I##_has(const I *self, uint64_t capability) { return (I##_capabilities(self) & capability) == capability; } \
    CMETA_INLINE const cmeta_interface_desc *I##_interface(void) { return &I##_interface_meta; }

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
