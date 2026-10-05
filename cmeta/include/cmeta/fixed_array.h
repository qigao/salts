#ifndef CMETA_FIXED_ARRAY_H
#define CMETA_FIXED_ARRAY_H

#include <cmeta/data.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Immutable metadata for a complete inline native array. There is no native
 * count field: count is a type invariant and every slot remains live. */
typedef struct cmeta_data_fixed_array_spec {
    const cmeta_type_desc *storage_type;
    const cmeta_data_desc *element;
    size_t count;
} cmeta_data_fixed_array_spec;

bool cmeta_data_fixed_array_spec_valid(const cmeta_data_fixed_array_spec *spec);
bool cmeta_data_fixed_array_is_zero(
    const cmeta_data_fixed_array_spec *spec, const void *object);
cmeta_status cmeta_data_fixed_array_init_zero(
    const cmeta_data_fixed_array_spec *spec, void *object);
void cmeta_data_fixed_array_restore_zero(
    const cmeta_data_fixed_array_spec *spec, void *object);
void cmeta_data_fixed_array_move(
    const cmeta_data_fixed_array_spec *spec, void *destination, void *source);
cmeta_status cmeta_data_fixed_array_copy_construct(
    const cmeta_data_fixed_array_spec *spec, void *destination, const void *source);
cmeta_status cmeta_data_fixed_array_read(
    const cmeta_data_fixed_array_spec *spec, const void *object,
    cmeta_data_collection_view *out);
cmeta_status cmeta_data_fixed_array_foreach(
    const cmeta_data_fixed_array_spec *spec, const void *object,
    cmeta_data_collection_visit_fn visit, void *context, size_t max_items);
cmeta_gen_status cmeta_data_fixed_array_next(
    const cmeta_data_fixed_array_spec *spec, const void *object,
    cmeta_range_cursor *cursor, const void **out);
cmeta_status cmeta_data_fixed_array_collect_begin(
    const cmeta_data_fixed_array_spec *spec, cmeta_collector *collector,
    const cmeta_type_desc *input, size_t limit);
cmeta_status cmeta_data_fixed_array_collect_accept(
    const cmeta_data_fixed_array_spec *spec, cmeta_collector *collector,
    const void *value);
cmeta_status cmeta_data_fixed_array_collect_finish(
    const cmeta_data_fixed_array_spec *spec, cmeta_collector *collector);
void cmeta_data_fixed_array_collect_abort(
    const cmeta_data_fixed_array_spec *spec, cmeta_collector *collector);

/**
 * Define canonical SEQUENCE metadata for one inline array typedef.
 *
 * The owner holds exactly count live elements. Element zero initialization
 * must be allocation-free and no-fail; a failure is a provider contract
 * violation. Copy may fail and releases every copied element on failure. Move
 * and restore follow the element's no-fail canonical lifecycle. Neither this
 * provider nor its collector allocates metadata or changes native storage.
 * Element copy may allocate only under its own bounded ownership contract.
 *
 * Collection construction accepts exactly count elements in order. A short
 * finish fails TYPE_MISMATCH; excess input fails CAPACITY_EXCEEDED. Failure or
 * abort restores the complete array. The collector borrows its final caller
 * slot and cannot be copied or moved before finish/abort. Borrowed element
 * pointers expire at owner mutation, move or destruction. Access is externally
 * serialized (single-threaded by default). Work is O(count * element work),
 * with O(1) metadata and no auxiliary payload storage.
 */
#define CMETA_DEFINE_FIXED_ARRAY(name_, storage_type_, element_type_, count_, \
                                element_data_, stable_id_, display_name_)    \
    typedef char name_##_extent_must_match_storage[                           \
        ((count_) > 0u && sizeof(storage_type_) % sizeof(element_type_) == 0u && \
         sizeof(storage_type_) / sizeof(element_type_) == (count_)) ? 1 : -1]; \
    static inline bool name_##_copy_construct(void *, const void *);          \
    static inline void name_##_move(void *, void *);                          \
    static inline void name_##_restore_zero(void *);                          \
    static const cmeta_type_traits name_##_cmeta_traits = {                   \
        CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,            \
        NULL, NULL, NULL, name_##_copy_construct, name_##_move,                \
        name_##_restore_zero};                                               \
    static const cmeta_type_identity name_##_cmeta_identity =                 \
        CMETA_TYPE_ID_ATOM_INIT(stable_id_);                                  \
    static const cmeta_type_desc name_##_cmeta_type = {                       \
        #storage_type_, sizeof(storage_type_), CMETA_ALIGNOF(storage_type_), \
        CMETA_T_OBJECT, NULL, &name_##_cmeta_traits, &name_##_cmeta_identity}; \
    static const cmeta_data_fixed_array_spec name_##_cmeta_spec = {           \
        &name_##_cmeta_type, (element_data_), (count_)};                       \
    static inline bool name_##_is_zero(const void *object_) {                \
        return cmeta_data_fixed_array_is_zero(&name_##_cmeta_spec, object_);   \
    }                                                                       \
    static inline cmeta_status name_##_init_zero(void *object_) {             \
        return cmeta_data_fixed_array_init_zero(&name_##_cmeta_spec, object_); \
    }                                                                       \
    static inline void name_##_restore_zero(void *object_) {                 \
        cmeta_data_fixed_array_restore_zero(&name_##_cmeta_spec, object_);     \
    }                                                                       \
    static inline void name_##_move(void *destination_, void *source_) {      \
        cmeta_data_fixed_array_move(&name_##_cmeta_spec, destination_, source_); \
    }                                                                       \
    static inline bool name_##_copy_construct(                               \
        void *destination_, const void *source_) {                           \
        return cmeta_data_fixed_array_copy_construct(                        \
            &name_##_cmeta_spec, destination_, source_) == CMETA_OK;          \
    }                                                                       \
    static inline const cmeta_data_desc *name_##_element(const void *object_) { \
        (void)object_;                                                      \
        return (element_data_);                                             \
    }                                                                       \
    static inline cmeta_status name_##_read(                                 \
        const void *object_, cmeta_data_collection_view *out_) {              \
        return cmeta_data_fixed_array_read(&name_##_cmeta_spec, object_, out_); \
    }                                                                       \
    static inline cmeta_status name_##_foreach(                              \
        const void *object_, cmeta_data_collection_visit_fn visit_,          \
        void *context_, size_t max_items_) {                                 \
        return cmeta_data_fixed_array_foreach(                               \
            &name_##_cmeta_spec, object_, visit_, context_, max_items_);      \
    }                                                                       \
    static inline size_t name_##_size(const void *object_) {                 \
        (void)object_;                                                      \
        return (count_);                                                    \
    }                                                                       \
    static inline cmeta_gen_status name_##_next(                             \
        const void *object_, cmeta_range_cursor *cursor_, const void **out_) { \
        return cmeta_data_fixed_array_next(                                 \
            &name_##_cmeta_spec, object_, cursor_, out_);                     \
    }                                                                       \
    static inline cmeta_status name_##_collect_begin(                        \
        void *context_, const cmeta_type_desc *input_, size_t limit_) {       \
        return cmeta_data_fixed_array_collect_begin(&name_##_cmeta_spec,     \
            (cmeta_collector *)context_, input_, limit_);                    \
    }                                                                       \
    static inline cmeta_status name_##_collect_accept(                       \
        void *context_, const void *value_) {                                \
        return cmeta_data_fixed_array_collect_accept(&name_##_cmeta_spec,     \
            (cmeta_collector *)context_, value_);                            \
    }                                                                       \
    static inline cmeta_status name_##_collect_finish(void *context_) {      \
        return cmeta_data_fixed_array_collect_finish(                        \
            &name_##_cmeta_spec, (cmeta_collector *)context_);                \
    }                                                                       \
    static inline void name_##_collect_abort(void *context_) {               \
        cmeta_data_fixed_array_collect_abort(                                \
            &name_##_cmeta_spec, (cmeta_collector *)context_);                \
    }                                                                       \
    static const cmeta_collector_ops name_##_collector_ops = {                \
        name_##_collect_begin, name_##_collect_accept,                        \
        name_##_collect_finish, name_##_collect_abort};                       \
    static inline cmeta_status name_##_collector_init(                       \
        void *object_, size_t limit_, cmeta_collector *out_) {                \
        if (out_ == NULL || object_ == NULL ||                               \
            !cmeta_data_fixed_array_spec_valid(&name_##_cmeta_spec))          \
            return CMETA_INVALID_ARGUMENT;                                  \
        out_->ops = &name_##_collector_ops;                                  \
        out_->context = out_;                                               \
        out_->zero_output = object_;                                        \
        out_->input_type = (element_data_)->storage_type;                    \
        out_->limit = limit_;                                               \
        out_->count = 0u;                                                   \
        out_->state = CMETA_COLLECTOR_ZERO;                                  \
        out_->status = CMETA_OK;                                             \
        return CMETA_OK;                                                    \
    }                                                                       \
    static const cmeta_data_collection_borrow_ops name_##_borrow_ops = {      \
        sizeof(cmeta_data_collection_borrow_ops),                            \
        CMETA_DATA_COLLECTION_BORROW_OPS_ABI_VERSION,                        \
        name_##_size, name_##_next, NULL};                                   \
    static const cmeta_data_collection_ops name_##_collection_ops = {         \
        sizeof(cmeta_data_collection_ops), CMETA_DATA_COLLECTION_OPS_ABI_VERSION, \
        &name_##_cmeta_type, CMETA_DATA_COLLECTION_CONTIGUOUS |               \
            CMETA_DATA_COLLECTION_ORDERED | CMETA_DATA_COLLECTION_RANDOM_ACCESS, \
        name_##_element, name_##_read, name_##_foreach, NULL,                 \
        &name_##_borrow_ops, (element_data_), name_##_collector_init, name_##_is_zero}; \
    static const cmeta_data_construct_ops name_##_construct_ops = {          \
        sizeof(cmeta_data_construct_ops), CMETA_DATA_CONSTRUCT_OPS_ABI_VERSION, \
        &name_##_cmeta_type, name_##_init_zero, name_##_restore_zero, name_##_move}; \
    static const cmeta_data_desc name_##_cmeta_data = {                       \
        sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,                 \
        stable_id_ ".data", display_name_, CMETA_DATA_SEQUENCE,              \
        &name_##_cmeta_type, NULL, NULL, NULL, NULL, NULL, NULL,               \
        &name_##_collection_ops, NULL, &name_##_construct_ops}

#ifdef __cplusplus
}
#endif
#endif
