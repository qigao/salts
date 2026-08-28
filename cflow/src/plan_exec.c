#include <cflow/plan_internal.h>

#include "result_storage.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const cflow_plan_impl *plan_impl(const cflow_plan *p) {
    return p ? (const cflow_plan_impl *)p->impl : NULL;
}

static void vec_destroy(cflow_plan_value_vec *v) {
    if (!v) return;
    if (v->data && v->type) {
        for (size_t index = 0u; index < v->count; ++index) {
            cflow_value_destroy(
                v->type, v->data + index * v->type->size);
        }
    }
    free(v->allocation);
    memset(v, 0, sizeof(*v));
}

void cflow_plan_value_vec_destroy(cflow_plan_value_vec *values) {
    vec_destroy(values);
}

static bool checked_bytes(size_t n, size_t size, size_t *bytes) {
    if (!bytes) return false;
    if (size && n > SIZE_MAX / size) return false;
    *bytes = n * size;
    return true;
}

static bool checked_add(size_t left, size_t right, size_t *sum) {
    if (!sum || left > SIZE_MAX - right) return false;
    *sum = left + right;
    return true;
}

static bool vec_init(cflow_plan_value_vec *v,
                     const cmeta_type_desc *type,
                     size_t capacity) {
    if (!v || v->allocation || v->data || v->count || v->capacity || v->type ||
        !cflow_result_storage_allocate(
            type, capacity, &v->allocation, &v->data))
        return false;
    v->capacity = capacity;
    v->type = type;
    return true;
}

static bool vec_copy_append(cflow_plan_value_vec *v, const void *value) {
    void *destination;

    if (!v || !v->type || !value || v->count >= v->capacity) return false;
    destination = v->data + v->count * v->type->size;
    if (!cflow_value_construct(v->type, destination, value)) return false;
    ++v->count;
    return true;
}

static bool vec_reserve(cflow_plan_value_vec *v, size_t capacity) {
    cflow_plan_value_vec replacement = {0};
    size_t bytes;
    unsigned char *data;

    if (!v || !v->type || capacity < v->count) return false;
    if (capacity <= v->capacity) return true;
    if (cflow_value_storage_type_supported(v->type)) {
        if (!checked_bytes(capacity, v->type->size, &bytes)) return false;
        data = (unsigned char *)realloc(v->allocation, bytes);
        if (!data) return false;
        v->allocation = data;
        v->data = data;
        v->capacity = capacity;
        return true;
    }
    if (!vec_init(&replacement, v->type, capacity)) return false;
    for (size_t index = 0u; index < v->count; ++index) {
        if (!vec_copy_append(
                &replacement, v->data + index * v->type->size)) {
            vec_destroy(&replacement);
            return false;
        }
    }
    vec_destroy(v);
    *v = replacement;
    return true;
}

static bool vec_move_append(cflow_plan_value_vec *v,
                            cflow_value_slot *source) {
    void *destination;

    if (!v || !v->type || !source || !source->live ||
        !cmeta_type_equal(v->type, source->type) || v->count >= v->capacity)
        return false;
    destination = v->data + v->count * v->type->size;
    if (!cflow_value_move_construct(v->type, destination, source->storage))
        return false;
    source->live = false;
    ++v->count;
    return true;
}

static size_t selection_byte_count(size_t count) {
    return count / 8u + (count % 8u != 0u);
}

static bool selection_contains(const unsigned char *selection, size_t index) {
    return (selection[index / 8u] & (unsigned char)(1u << (index % 8u))) != 0u;
}

static void selection_remove(unsigned char *selection, size_t index) {
    selection[index / 8u] &= (unsigned char)~(1u << (index % 8u));
}

#define CFLOW_PLAN_RAW_BATCH_NAME_I(id) cflow_plan_raw_batch_##id
#define CFLOW_PLAN_RAW_BATCH_NAME_E(id) CFLOW_PLAN_RAW_BATCH_NAME_I(id)
#define CFLOW_PLAN_RAW_BATCH_NAME(id) CFLOW_PLAN_RAW_BATCH_NAME_E(id)

/* Each generated stage is O(input_count) time and O(1) auxiliary space. The
 * caller owns the bounded selection and exact output buffers. */
#define CFLOW_DEFINE_RAW_BATCH(in, ret) \
    static bool CFLOW_PLAN_RAW_BATCH_NAME(CMETA_U_ID(in, ret))( \
        const cflow_plan_call *call, cflow_plan_unary_batch_mode mode, \
        const unsigned char *input, size_t input_count, \
        unsigned char *selection, unsigned char *output, size_t *value_count) { \
        CMETA_FN_TYPE(CMETA_U_ID(in, ret)) raw; \
        size_t produced = 0u; \
        if (!call || !value_count || (!input && input_count) || \
            call->fn.meta.sig != CMETA_SIG_NAME(CMETA_U_ID(in, ret))) \
            return false; \
        raw = call->fn.meta.call.CMETA_CALL_MEMBER(CMETA_U_ID(in, ret)); \
        if (!raw) return false; \
        if (mode == CFLOW_PLAN_BATCH_FILTER) { \
            size_t selected = *value_count; \
            if (!selection || output || selected > input_count || \
                !cmeta_type_equal(call->output_type, &cmeta_type_bool)) \
                return false; \
            for (size_t index = 0u; index < input_count; ++index) { \
                CMETA_TYPE_CTYPE(in) value; \
                CMETA_TYPE_CTYPE(ret) result; \
                _Bool keep; \
                if (!selection_contains(selection, index)) continue; \
                memcpy(&value, input + index * sizeof(value), sizeof(value)); \
                result = raw(value); \
                memcpy(&keep, &result, sizeof(keep)); \
                if (!keep) { \
                    if (!selected) return false; \
                    selection_remove(selection, index); \
                    --selected; \
                } \
            } \
            *value_count = selected; \
            return true; \
        } \
        if (mode != CFLOW_PLAN_BATCH_MAP) return false; \
        for (size_t index = 0u; index < input_count; ++index) { \
            CMETA_TYPE_CTYPE(in) value; \
            CMETA_TYPE_CTYPE(ret) result; \
            if (selection && !selection_contains(selection, index)) continue; \
            if (produced >= *value_count || !output) return false; \
            memcpy(&value, input + index * sizeof(value), sizeof(value)); \
            result = raw(value); \
            memcpy(output + produced * sizeof(result), &result, sizeof(result)); \
            ++produced; \
        } \
        *value_count = produced; \
        return true; \
    }
CMETA_UNARY_SIGNATURES(CFLOW_DEFINE_RAW_BATCH)
#undef CFLOW_DEFINE_RAW_BATCH

cflow_plan_unary_batch_fn cflow_plan_unary_batch_for_signature(cmeta_sig sig) {
    switch (sig) {
#define CFLOW_RAW_BATCH_CASE(in, ret) \
        case CMETA_SIG_NAME(CMETA_U_ID(in, ret)): \
            return CFLOW_PLAN_RAW_BATCH_NAME(CMETA_U_ID(in, ret));
        CMETA_UNARY_SIGNATURES(CFLOW_RAW_BATCH_CASE)
#undef CFLOW_RAW_BATCH_CASE
        default:
            return NULL;
    }
}

#undef CFLOW_PLAN_RAW_BATCH_NAME
#undef CFLOW_PLAN_RAW_BATCH_NAME_E
#undef CFLOW_PLAN_RAW_BATCH_NAME_I

static bool stats_increment(size_t *counter) {
    if (!counter) return true;
    if (*counter == SIZE_MAX) return false;
    ++*counter;
    return true;
}

typedef struct cflow_fused_resources {
    size_t allocation_calls;
    size_t allocated_bytes;
    size_t live_bytes;
    size_t peak_live_bytes;
    size_t selection_bytes;
    size_t intermediate_bytes;
    size_t result_bytes;
} cflow_fused_resources;

static bool fused_allocate(cflow_fused_resources *resources,
                           size_t bytes,
                           unsigned char **allocation) {
    size_t next_allocated = 0u;
    size_t next_live = 0u;
    unsigned char *data;
    if (!resources || !allocation ||
        !checked_add(resources->allocated_bytes, bytes, &next_allocated) ||
        !checked_add(resources->live_bytes, bytes, &next_live))
        return false;
    data = bytes ? malloc(bytes) : NULL;
    if (bytes && !data) return false;
    if (bytes) ++resources->allocation_calls;
    resources->allocated_bytes = next_allocated;
    resources->live_bytes = next_live;
    if (next_live > resources->peak_live_bytes)
        resources->peak_live_bytes = next_live;
    *allocation = data;
    return true;
}

static bool fused_release(cflow_fused_resources *resources, size_t bytes) {
    if (!resources || bytes > resources->live_bytes) return false;
    resources->live_bytes -= bytes;
    return true;
}

static bool eval_fused_filters(const cflow_plan *plan,
                               const cflow_plan_impl *impl,
                               const unsigned char *input_bytes,
                               size_t input_count,
                               unsigned char *selection,
                               size_t *selected_count,
                               cflow_plan_eval_stats *stats) {
    if (!plan || !impl || !selection || !selected_count) return false;
    for (size_t pc = 0u; pc < impl->fused_filter_count; ++pc) {
        const cflow_plan_call *call = &impl->code[pc].call;
        if (call->raw_batch) {
            if (!call->raw_batch(call, CFLOW_PLAN_BATCH_FILTER, input_bytes, input_count,
                                 selection, NULL, selected_count) ||
                !stats_increment(stats ? &stats->raw_batch_stage_calls : NULL))
                return false;
            continue;
        }
        for (size_t index = 0u; index < input_count; ++index) {
            _Bool keep = false;
            const void *args[1];
            if (!selection_contains(selection, index)) continue;
            args[0] = input_bytes + index * plan->input_type->size;
            if (!call->invoke(&call->fn, &keep, args) ||
                !stats_increment(stats ? &stats->adapter_item_calls : NULL))
                return false;
            if (!keep) {
                selection_remove(selection, index);
                --*selected_count;
            }
        }
    }
    return true;
}

static bool copy_values(cflow_plan_value_vec *dst, const void *src, size_t count,
                        const cmeta_type_desc *type) {
    if (!dst || !type || (!src && count) || !vec_init(dst, type, count))
        return false;
    if (cflow_value_storage_type_supported(type)) {
        if (count) memcpy(dst->data, src, count * type->size);
        dst->count = count;
        return true;
    }
    for (size_t index = 0u; index < count; ++index) {
        if (!vec_copy_append(
                dst, (const unsigned char *)src + index * type->size)) {
            vec_destroy(dst);
            return false;
        }
    }
    return true;
}

static bool step_filter(const cflow_plan_inst *i, cflow_plan_value_vec *v) {
    cflow_plan_value_vec out = {0};

    if (!i || !v || !cmeta_type_equal(v->type, i->input_type)) return false;
    if (cflow_value_storage_type_supported(v->type)) {
        size_t output_count = 0u;
        for (size_t index = 0u; index < v->count; ++index) {
            unsigned char *element = v->data + index * v->type->size;
            _Bool keep = false;
            const void *args[1] = {element};
            if (!i->call.invoke ||
                !i->call.invoke(&i->call.fn, &keep, args))
                return false;
            if (keep) {
                if (output_count != index) {
                    memmove(v->data + output_count * v->type->size,
                            element, v->type->size);
                }
                ++output_count;
            }
        }
        v->count = output_count;
        return true;
    }
    if (!vec_init(&out, v->type, v->count)) return false;
    for (size_t n = 0; n < v->count; ++n) {
        unsigned char *elem = v->data + n * v->type->size;
        _Bool keep = false; const void *args[1] = { elem };
        if (!i->call.invoke || !i->call.invoke(&i->call.fn, &keep, args)) {
            vec_destroy(&out);
            return false;
        }
        if (keep && !vec_copy_append(&out, elem)) {
            vec_destroy(&out);
            return false;
        }
    }
    vec_destroy(v);
    *v = out;
    return true;
}

static bool step_take(const cflow_plan_inst *i, cflow_plan_value_vec *v) {
    if (!i || !v || !i->has_size_parameter ||
        !cmeta_type_equal(v->type, i->input_type) ||
        !cmeta_type_equal(i->input_type, i->output_type))
        return false;
    if (i->size_parameter == 0u) {
        vec_destroy(v);
        v->type = i->output_type;
        return true;
    }
    if (v->count > i->size_parameter) {
        if (!cflow_value_storage_type_supported(v->type)) {
            for (size_t index = i->size_parameter; index < v->count; ++index) {
                cflow_value_destroy(
                    v->type, v->data + index * v->type->size);
            }
        }
        v->count = i->size_parameter;
    }
    return true;
}

static bool step_skip(const cflow_plan_inst *i, cflow_plan_value_vec *v) {
    cflow_plan_value_vec out = {0};
    size_t remaining;
    if (!i || !v || !i->has_size_parameter ||
        !cmeta_type_equal(v->type, i->input_type) ||
        !cmeta_type_equal(i->input_type, i->output_type))
        return false;
    if (i->size_parameter >= v->count) {
        vec_destroy(v);
        v->type = i->output_type;
        return true;
    }
    if (i->size_parameter == 0u) return true;
    remaining = v->count - i->size_parameter;
    if (cflow_value_storage_type_supported(v->type)) {
        memmove(v->data,
                v->data + i->size_parameter * v->type->size,
                remaining * v->type->size);
        v->count = remaining;
        return true;
    }
    if (!vec_init(&out, v->type, remaining)) return false;
    for (size_t index = i->size_parameter; index < v->count; ++index) {
        if (!vec_copy_append(&out, v->data + index * v->type->size)) {
            vec_destroy(&out);
            return false;
        }
    }
    vec_destroy(v);
    *v = out;
    return true;
}

static bool step_map(const cflow_plan_inst *i, cflow_plan_value_vec *v) {
    if (!i || !v || !i->fn_chain_count || !cmeta_type_equal(v->type, i->input_type)) return false;
    cflow_plan_value_vec cur = *v;
    memset(v, 0, sizeof(*v));
    for (size_t k = 0; k < i->fn_chain_count; ++k) {
        const cflow_plan_call *call = &i->fn_chain[k];
        if (!call->invoke || !cmeta_type_equal(call->input_type, cur.type)) { vec_destroy(&cur); return false; }
        cflow_plan_value_vec next = {0};
        if (!vec_init(&next, call->output_type, cur.count)) {
            vec_destroy(&cur);
            return false;
        }
        for (size_t n = 0; n < cur.count; ++n) {
            const void *args[1] = { cur.data + n * cur.type->size };
            if (!call->invoke(
                    &call->fn,
                    next.data + next.count * call->output_type->size,
                    args)) {
                vec_destroy(&next);
                vec_destroy(&cur);
                return false;
            }
            ++next.count;
        }
        vec_destroy(&cur);
        cur = next;
    }
    if (!cmeta_type_equal(cur.type, i->output_type)) { vec_destroy(&cur); return false; }
    *v = cur;
    return true;
}

static bool append_generated(cflow_plan_value_vec *dst,
                             cflow_value_slot *value) {
    if (!dst || !value || !dst->type) return false;
    if (dst->count == dst->capacity) {
        size_t next = dst->capacity ? dst->capacity * 2u : 8u;
        if (next < dst->capacity || !vec_reserve(dst, next)) return false;
    }
    return vec_move_append(dst, value);
}

static bool step_flat_map(const cflow_plan_inst *i, cflow_plan_value_vec *v) {
    if (!i || !v || !cmeta_type_equal(v->type, i->input_type)) return false;
    cflow_plan_value_vec out = {0};
    cflow_value_slot slot = {0};
    if (!vec_init(&out, i->output_type, 0u) ||
        !cflow_value_slot_init(&slot, i->output_type)) {
        vec_destroy(&out);
        cflow_value_slot_destroy(&slot);
        return false;
    }
    for (size_t n = 0; n < v->count; ++n) {
        const void *input = v->data + n * v->type->size;
        size_t cursor = 0;
        for (;;) {
            cmeta_gen_status st = cmeta_callable_generate(
                &i->call.fn, input, slot.storage, &cursor);
            if (st == CMETA_GEN_ERROR) {
                cflow_value_slot_destroy(&slot);
                vec_destroy(&out);
                return false;
            }
            if (st == CMETA_GEN_DONE) break;
            if (st != CMETA_GEN_VALUE && st != CMETA_GEN_VALUE_AND_DONE) {
                cflow_value_slot_destroy(&slot);
                vec_destroy(&out);
                return false;
            }
            slot.live = true;
            if (!append_generated(&out, &slot)) {
                cflow_value_slot_destroy(&slot);
                vec_destroy(&out);
                return false;
            }
            if (st == CMETA_GEN_VALUE_AND_DONE) break;
        }
    }
    cflow_value_slot_destroy(&slot);
    vec_destroy(v);
    *v = out;
    return true;
}

static bool step_reduce(const cflow_plan_inst *i, cflow_plan_value_vec *v) {
    if (!i || !v || !cmeta_type_equal(v->type, i->input_type) ||
        !cmeta_type_equal(i->input_type, i->output_type)) return false;
    cflow_value_slot acc = {0};
    cflow_value_slot next = {0};
    cflow_plan_value_vec out = {0};
    if (!v->count) { vec_destroy(v); v->type = i->output_type; return true; }
    if (cflow_value_storage_type_supported(v->type)) {
        unsigned char *byte_acc = (unsigned char *)malloc(v->type->size);
        unsigned char *tmp = (unsigned char *)malloc(v->type->size);
        if (!byte_acc || !tmp) {
            free(byte_acc);
            free(tmp);
            return false;
        }
        memcpy(byte_acc, v->data, v->type->size);
        for (size_t index = 1u; index < v->count; ++index) {
            const void *args[2] = {
                byte_acc, v->data + index * v->type->size };
            if (!i->call.invoke ||
                !i->call.invoke(&i->call.fn, tmp, args)) {
                free(byte_acc);
                free(tmp);
                return false;
            }
            memcpy(byte_acc, tmp, v->type->size);
        }
        free(tmp);
        vec_destroy(v);
        v->allocation = byte_acc;
        v->data = byte_acc;
        v->count = 1u;
        v->capacity = 1u;
        v->type = i->output_type;
        return true;
    }
    if (!cflow_value_slot_init(&acc, v->type) ||
        !cflow_value_slot_copy(&acc, v->data)) {
        cflow_value_slot_destroy(&acc);
        return false;
    }
    for (size_t n = 1; n < v->count; ++n) {
        const void *args[2] = {
            acc.storage, v->data + n * v->type->size };
        if (!cflow_value_slot_init(&next, v->type) || !i->call.invoke ||
            !i->call.invoke(&i->call.fn, next.storage, args)) {
            cflow_value_slot_destroy(&next);
            cflow_value_slot_destroy(&acc);
            return false;
        }
        next.live = true;
        cflow_value_slot_reset(&acc);
        if (!cflow_value_slot_move(&acc, &next)) {
            cflow_value_slot_destroy(&next);
            cflow_value_slot_destroy(&acc);
            return false;
        }
        cflow_value_slot_destroy(&next);
    }
    if (!vec_init(&out, i->output_type, 1u) ||
        !vec_move_append(&out, &acc)) {
        vec_destroy(&out);
        cflow_value_slot_destroy(&acc);
        return false;
    }
    cflow_value_slot_destroy(&acc);
    vec_destroy(v);
    *v = out;
    return true;
}

cflow_plan_step_fn cflow_plan_step_for_opcode(cflow_plan_opcode opcode) {
    switch (opcode) {
        case CMETA_PLAN_FILTER: return step_filter;
        case CMETA_PLAN_MAP: return step_map;
        case CMETA_PLAN_FLAT_MAP: return step_flat_map;
        case CMETA_PLAN_REDUCE: return step_reduce;
        case CMETA_PLAN_TAKE: return step_take;
        case CMETA_PLAN_SKIP: return step_skip;
    }
    return NULL;
}

static bool eval_materialized(const cflow_plan *plan,
                              const cflow_plan_impl *impl,
                              const void *inputs,
                              size_t input_count,
                              cflow_result *out) {
    cflow_plan_value_vec v = {0};
    if (!cflow_plan_eval_prefix_materialized(
            plan, inputs, input_count, impl->count, &v))
        return false;
    if (!cmeta_type_equal(v.type, plan->output_type)) { vec_destroy(&v); return false; }
    out->data = v.data; out->count = v.count; out->type = v.type;
    memset(&v, 0, sizeof(v));
    return true;
}

bool cflow_plan_eval_prefix_materialized(const cflow_plan *plan,
                                         const void *inputs,
                                         size_t input_count,
                                         size_t instruction_count,
                                         cflow_plan_value_vec *out) {
    const cflow_plan_impl *impl = plan_impl(plan);
    cflow_plan_value_vec values = {0};

    if (!plan || !impl || !out || instruction_count > impl->count ||
        !plan->input_type)
        return false;
    memset(out, 0, sizeof(*out));
    if (!copy_values(&values, inputs, input_count, plan->input_type)) return false;
    for (size_t pc = 0u; pc < instruction_count; ++pc) {
        const cflow_plan_inst *inst = &impl->code[pc];
        if (!inst->step || !inst->step(inst, &values)) {
            vec_destroy(&values);
            return false;
        }
    }
    *out = values;
    return true;
}

static bool eval_fused_value(const cflow_plan *plan,
                             const cflow_plan_impl *impl,
                             const void *inputs,
                             size_t input_count,
                             cflow_result *out,
                             cflow_plan_eval_stats *stats) {
    const unsigned char *input_bytes = inputs;
    unsigned char *selection = NULL;
    unsigned char *current_owned = NULL;
    unsigned char *pending = NULL;
    const unsigned char *current_data = input_bytes;
    size_t input_bytes_count = 0u;
    size_t selected_count = input_count;
    size_t current_type_size = plan->input_type->size;
    size_t current_owned_bytes = 0u;
    cflow_fused_resources resources = {0};

    if (stats) stats->fused_value_path = true;
    if ((!inputs && input_count) ||
        !checked_bytes(input_count, plan->input_type->size, &input_bytes_count))
        return false;
    if (!input_count) {
        out->type = plan->output_type;
        return true;
    }

    if (impl->fused_filter_count)
        resources.selection_bytes = selection_byte_count(input_count);
    if (!fused_allocate(&resources, resources.selection_bytes, &selection)) return false;
    if (resources.selection_bytes) {
        memset(selection, 0xff, resources.selection_bytes);
        if (!eval_fused_filters(plan, impl, input_bytes, input_count,
                                selection, &selected_count, stats))
            goto fail;
    }

    if (impl->fused_map_call_count) {
        size_t map_index = 0u;
        for (size_t pc = impl->fused_filter_count; pc < impl->count; ++pc) {
            const cflow_plan_inst *inst = &impl->code[pc];
            for (size_t k = 0u; k < inst->fn_chain_count; ++k) {
                const cflow_plan_call *call = &inst->fn_chain[k];
                const bool final_map = map_index + 1u == impl->fused_map_call_count;
                size_t next_bytes = 0u;
                size_t output_index = 0u;
                if (!checked_bytes(selected_count, call->output_type->size, &next_bytes) ||
                    !fused_allocate(&resources, next_bytes, &pending))
                    goto fail;

                if (!map_index && selection) {
                    if (call->raw_batch) {
                        output_index = selected_count;
                        if (!call->raw_batch(call, CFLOW_PLAN_BATCH_MAP, input_bytes,
                                             input_count, selection, pending, &output_index) ||
                            !stats_increment(stats ? &stats->raw_batch_stage_calls : NULL))
                            goto fail;
                    } else {
                        for (size_t input_index = 0u; input_index < input_count; ++input_index) {
                            const void *args[1];
                            if (!selection_contains(selection, input_index)) continue;
                            args[0] = input_bytes + input_index * plan->input_type->size;
                            if (!call->invoke(&call->fn,
                                              pending + output_index * call->output_type->size,
                                              args) ||
                                !stats_increment(stats ? &stats->adapter_item_calls : NULL))
                                goto fail;
                            ++output_index;
                        }
                    }
                } else {
                    if (call->raw_batch) {
                        output_index = selected_count;
                        if (!call->raw_batch(call, CFLOW_PLAN_BATCH_MAP, current_data,
                                             selected_count, NULL, pending, &output_index) ||
                            !stats_increment(stats ? &stats->raw_batch_stage_calls : NULL))
                            goto fail;
                    } else {
                        for (size_t index = 0u; index < selected_count; ++index) {
                            const void *args[1] = { current_data + index * current_type_size };
                            if (!call->invoke(&call->fn,
                                              pending + index * call->output_type->size,
                                              args) ||
                                !stats_increment(stats ? &stats->adapter_item_calls : NULL))
                                goto fail;
                        }
                        output_index = selected_count;
                    }
                }
                if (output_index != selected_count) goto fail;

                free(current_owned);
                current_owned = pending;
                pending = NULL;
                current_data = current_owned;
                if (!fused_release(&resources, current_owned_bytes)) goto fail;
                current_owned_bytes = next_bytes;
                current_type_size = call->output_type->size;
                if (!map_index && selection) {
                    free(selection);
                    selection = NULL;
                    if (!fused_release(&resources, resources.selection_bytes)) goto fail;
                }
                if (final_map) resources.result_bytes = next_bytes;
                else if (!checked_add(resources.intermediate_bytes, next_bytes,
                                      &resources.intermediate_bytes))
                    goto fail;
                ++map_index;
            }
        }
        if (map_index != impl->fused_map_call_count) goto fail;
    } else {
        size_t output_index = 0u;
        if (!checked_bytes(selected_count, plan->output_type->size,
                           &resources.result_bytes) ||
            !fused_allocate(&resources, resources.result_bytes, &pending))
            goto fail;
        for (size_t input_index = 0u; input_index < input_count; ++input_index) {
            if (selection && !selection_contains(selection, input_index)) continue;
            memcpy(pending + output_index * plan->output_type->size,
                   input_bytes + input_index * plan->input_type->size,
                   plan->output_type->size);
            ++output_index;
        }
        if (output_index != selected_count) goto fail;
        current_owned = pending;
        pending = NULL;
        current_owned_bytes = resources.result_bytes;
    }

    free(selection);
    if (stats) {
        stats->allocation_calls = resources.allocation_calls;
        stats->allocated_bytes = resources.allocated_bytes;
        stats->peak_live_bytes = resources.peak_live_bytes;
        stats->selection_bytes = resources.selection_bytes;
        stats->intermediate_bytes = resources.intermediate_bytes;
        stats->result_bytes = resources.result_bytes;
    }
    out->data = current_owned;
    out->count = selected_count;
    out->type = plan->output_type;
    return true;

fail:
    free(pending);
    free(current_owned);
    free(selection);
    return false;
}

bool cflow_plan_eval_array_profile(const cflow_plan *plan,
                                   const void *inputs,
                                   size_t input_count,
                                   cflow_result *out,
                                   cflow_plan_eval_stats *stats) {
    const cflow_plan_impl *impl = plan_impl(plan);
    if (stats) memset(stats, 0, sizeof(*stats));
    if (!plan || !impl || !out || !plan->input_type || !plan->output_type) return false;
    memset(out, 0, sizeof(*out));
    if (impl->fused_value)
        return eval_fused_value(plan, impl, inputs, input_count, out, stats);
    return eval_materialized(plan, impl, inputs, input_count, out);
}

bool cflow_plan_eval_array(const cflow_plan *plan,
                           const void *inputs,
                           size_t input_count,
                           cflow_result *out) {
    const cflow_plan_eval_options options = {
        .mode = CFLOW_PLAN_EXECUTION_SEQUENTIAL
    };
    return cflow_plan_eval_array_with_options(
        plan, inputs, input_count, &options, out);
}
