#ifndef CMETA_CONTAINER_OF_H
#define CMETA_CONTAINER_OF_H

#include <cmeta/compiler.h>
#include <stddef.h>

/* Borrow a fixed-layout enclosing object from its actual member address.
 * ptr must be nonnull and designate that member of a live object of owner.
 * The caller chooses owner qualifiers explicitly (for example const Record).
 * Type checks are unevaluated; the pointer expression runs exactly once.
 * No ownership, lifetime extension, synchronization or dynamic-layout recovery. */
#ifdef __cplusplus
#define CMETA_CONTAINER_OFFSET(owner,member) offsetof(std::remove_cv_t<owner>,member)
#define CMETA_CONTAINER_LAYOUT_REQUIRE(owner) \
    CMETA_CONST_REQUIRE(std::is_standard_layout<owner>::value)
#define CMETA_CONTAINER_ADDRESS(ptr,owner,offset) \
    reinterpret_cast<owner *>(const_cast<unsigned char *>( \
        reinterpret_cast<const volatile unsigned char *>(ptr)) - (offset))
#else
#define CMETA_CONTAINER_OFFSET(owner,member) offsetof(owner,member)
#define CMETA_CONTAINER_LAYOUT_REQUIRE(owner) 0
#define CMETA_CONTAINER_ADDRESS(ptr,owner,offset) \
    ((owner *)((unsigned char *)(ptr) - (offset)))
#endif

/* member_type includes the qualifiers imposed by owner. Array members use an
 * array typedef, and ptr is the address of the whole array, not an element. */
#define cmeta_container_of_as(ptr,owner,member_type,member) \
    CMETA_CONTAINER_ADDRESS(ptr,owner,CMETA_CONTAINER_OFFSET(owner,member) + \
        CMETA_CONTAINER_LAYOUT_REQUIRE(owner) + \
        CMETA_CONST_REQUIRE( \
            sizeof(((owner *)0)->member) > 0 && \
            CMETA_TYPE_MATCHES(&((owner *)0)->member,member_type *) && \
            CMETA_TYPE_MATCHES((ptr) + 0,member_type *)))

#define CMETA_HAS_CONTAINER_OF CMETA_HAS_NATIVE_TYPEOF
#if CMETA_HAS_CONTAINER_OF
#define cmeta_container_of(ptr,owner,member) \
    cmeta_container_of_as(ptr,owner,CMETA_NATIVE_TYPEOF(((owner *)0)->member),member)
#endif

#endif
