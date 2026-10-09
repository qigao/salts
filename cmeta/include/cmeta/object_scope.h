#ifndef CMETA_OBJECT_SCOPE_H
#define CMETA_OBJECT_SCOPE_H

#include <cmeta/object.h>
#include <cmeta/cleanup.h>

CMETA_INLINE void cmeta_cleanup_object_release_(void *authority, void *resource) {
    (void)authority;
#ifdef __cplusplus
    cmeta_object_release(static_cast<cmeta_object_ref *>(resource));
#else
    cmeta_object_release((cmeta_object_ref *)resource);
#endif
}
/** BORROWED only clears its view; SHARED releases; OWNED destroys. The original
 * handle remains the sole owner and must not be copied or externally replaced
 * while armed. Providers and any outer plugin lease stay borrowed. */
CMETA_INLINE cmeta_status cmeta_cleanup_object(
    cmeta_cleanup *obligation, cmeta_object_ref *object) {
    if (!cmeta_object_ref_valid(object)) return CMETA_INVALID_ARGUMENT;
    return cmeta_cleanup_arm(obligation, cmeta_cleanup_object_release_, NULL, object);
}

/** Generator result adapter: canonical flags authorize exactly the existing
 * ObjectRef lifetime. Rejection changes neither handle nor obligation.
 * Borrowed results create no obligation; an escaping borrow still needs its
 * authoritative external owner. VALUE storage uses the Data lifecycle adapter. */
CMETA_INLINE cmeta_status cmeta_cleanup_object_result(cmeta_cleanup *obligation,
    cmeta_result_flags flags, cmeta_object_ref *object) {
    cmeta_result_cleanup kind;
    cmeta_status status = cmeta_result_cleanup_classify(flags,&kind);
    if (status != CMETA_OK) return status;
    if (obligation == NULL || !cmeta_object_ref_valid(object)) return CMETA_INVALID_ARGUMENT;
    if (obligation->release != NULL) return CMETA_BUSY;
    switch (kind) {
    case CMETA_RESULT_CLEANUP_BORROW:
        return object->lifetime == CMETA_OBJECT_LIFETIME_BORROWED ? CMETA_OK : CMETA_TYPE_MISMATCH;
    case CMETA_RESULT_CLEANUP_RELEASE:
        if (object->lifetime != CMETA_OBJECT_LIFETIME_SHARED) return CMETA_TYPE_MISMATCH;
        break;
    case CMETA_RESULT_CLEANUP_DESTROY:
        if (object->lifetime != CMETA_OBJECT_LIFETIME_OWNED) return CMETA_TYPE_MISMATCH;
        break;
    default: return CMETA_TRAIT_MISSING;
    }
    return cmeta_cleanup_arm(obligation,cmeta_cleanup_object_release_,NULL,object);
}

#ifdef __cplusplus
namespace cmeta {
class object_scope {
    cmeta_object_ref object_ = CMETA_OBJECT_REF_INIT;
public:
    object_scope() noexcept = default;
    object_scope(const object_scope &) = delete;
    object_scope &operator=(const object_scope &) = delete;
    object_scope &operator=(object_scope &&) = delete;
    object_scope(object_scope &&other) noexcept : object_(other.object_) {
        other.object_ = CMETA_OBJECT_REF_INIT;
    }
    ~object_scope() noexcept { cmeta_object_release(&object_); }
    /** Transfer a valid handle without retain; rejection preserves both owners. */
    [[nodiscard]] cmeta_status take(cmeta_object_ref &source) noexcept {
        if (object_.lifetime != CMETA_OBJECT_LIFETIME_NONE) return CMETA_BUSY;
        if (!cmeta_object_ref_valid(&source)) return CMETA_INVALID_ARGUMENT;
        object_ = source;
        source = CMETA_OBJECT_REF_INIT;
        return CMETA_OK;
    }
    const cmeta_object_ref *get() const noexcept { return &object_; }
    void close() noexcept { cmeta_object_release(&object_); }
    cmeta_object_ref detach() noexcept {
        cmeta_object_ref result = object_;
        object_ = CMETA_OBJECT_REF_INIT;
        return result;
    }
};
}
#endif
#endif
