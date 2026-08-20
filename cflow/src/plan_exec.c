#include <cflow/plan_internal.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const cflow_plan_impl *plan_impl(const cflow_plan *p) {
    return p ? (const cflow_plan_impl *)p->impl : NULL;
}

static void vec_destroy(cflow_plan_value_vec *v) {
    if (!v) return;
    free(v->data);
    memset(v, 0, sizeof(*v));
}

static bool checked_bytes(size_t n, size_t size, size_t *bytes) {
    if (!bytes) return false;
    if (size && n > SIZE_MAX / size) return false;
    *bytes = n * size;
    return true;
}

static bool copy_values(cflow_plan_value_vec *dst, const void *src, size_t count,
                        const cmeta_type_desc *type) {
    size_t bytes = 0;
    if (!dst || !type || (!src && count) || !checked_bytes(count, type->size, &bytes))
        return false;
    unsigned char *p = bytes ? malloc(bytes) : NULL;
    if (bytes && !p) return false;
    if (bytes) memcpy(p, src, bytes);
    dst->data = p; dst->count = count; dst->type = type;
    return true;
}

static bool step_filter(const cflow_plan_inst *i, cflow_plan_value_vec *v) {
    if (!i || !v || !cmeta_type_equal(v->type, i->input_type)) return false;
    size_t out = 0;
    for (size_t n = 0; n < v->count; ++n) {
        unsigned char *elem = v->data + n * v->type->size;
        _Bool keep = false; const void *args[1] = { elem };
        if (!cmeta_callable_invoke(&i->fn, &keep, args)) return false;
        if (keep) {
            if (out != n) memmove(v->data + out * v->type->size, elem, v->type->size);
            ++out;
        }
    }
    v->count = out;
    return true;
}

static bool step_map(const cflow_plan_inst *i, cflow_plan_value_vec *v) {
    if (!i || !v || !i->fn_chain_count || !cmeta_type_equal(v->type, i->input_type)) return false;
    cflow_plan_value_vec cur = *v;
    v->data = NULL; v->count = 0; v->type = NULL;
    for (size_t k = 0; k < i->fn_chain_count; ++k) {
        const cmeta_sig_desc *sig = cmeta_callable_signature(i->fn_chain[k]);
        if (!sig || sig->param_count != 1u || !cmeta_type_equal(sig->params[0], cur.type)) { vec_destroy(&cur); return false; }
        size_t bytes = 0;
        if (!checked_bytes(cur.count, sig->return_type->size, &bytes)) { vec_destroy(&cur); return false; }
        unsigned char *next = bytes ? malloc(bytes) : NULL;
        if (bytes && !next) { vec_destroy(&cur); return false; }
        for (size_t n = 0; n < cur.count; ++n) {
            const void *args[1] = { cur.data + n * cur.type->size };
            if (!cmeta_callable_invoke(&i->fn_chain[k], next + n * sig->return_type->size, args)) {
                free(next); vec_destroy(&cur); return false;
            }
        }
        size_t next_count = cur.count;
        vec_destroy(&cur);
        cur.data = next; cur.count = next_count; cur.type = sig->return_type;
    }
    if (!cmeta_type_equal(cur.type, i->output_type)) { vec_destroy(&cur); return false; }
    *v = cur;
    return true;
}

static bool append_generated(cflow_plan_value_vec *dst, size_t *capacity, const void *value) {
    if (!dst || !capacity || !dst->type) return false;
    if (dst->count == *capacity) {
        size_t next = *capacity ? *capacity * 2u : 8u;
        if (next < *capacity || (dst->type->size && next > SIZE_MAX / dst->type->size)) return false;
        unsigned char *p = realloc(dst->data, next * dst->type->size);
        if (!p) return false;
        dst->data = p; *capacity = next;
    }
    memcpy(dst->data + dst->count * dst->type->size, value, dst->type->size);
    ++dst->count;
    return true;
}

static bool step_flat_map(const cflow_plan_inst *i, cflow_plan_value_vec *v) {
    if (!i || !v || !cmeta_type_equal(v->type, i->input_type)) return false;
    cflow_plan_value_vec out = {0}; out.type = i->output_type;
    size_t capacity = 0;
    unsigned char *slot = malloc(out.type->size ? out.type->size : 1u);
    if (!slot) return false;
    for (size_t n = 0; n < v->count; ++n) {
        const void *input = v->data + n * v->type->size;
        size_t cursor = 0;
        for (;;) {
            cmeta_gen_status st = cmeta_callable_generate(&i->fn, input, slot, &cursor);
            if (st == CMETA_GEN_ERROR) { free(slot); vec_destroy(&out); return false; }
            if (st == CMETA_GEN_DONE) break;
            if (st != CMETA_GEN_VALUE && st != CMETA_GEN_VALUE_AND_DONE) { free(slot); vec_destroy(&out); return false; }
            if (!append_generated(&out, &capacity, slot)) { free(slot); vec_destroy(&out); return false; }
            if (st == CMETA_GEN_VALUE_AND_DONE) break;
        }
    }
    free(slot); vec_destroy(v); *v = out; return true;
}

static bool step_reduce(const cflow_plan_inst *i, cflow_plan_value_vec *v) {
    if (!i || !v || !cmeta_type_equal(v->type, i->input_type) ||
        !cmeta_type_equal(i->input_type, i->output_type)) return false;
    if (!v->count) { vec_destroy(v); v->type = i->output_type; return true; }
    unsigned char *acc = malloc(v->type->size ? v->type->size : 1u);
    unsigned char *tmp = malloc(v->type->size ? v->type->size : 1u);
    if (!acc || !tmp) { free(acc); free(tmp); return false; }
    memcpy(acc, v->data, v->type->size);
    for (size_t n = 1; n < v->count; ++n) {
        const void *args[2] = { acc, v->data + n * v->type->size };
        if (!cmeta_callable_invoke(&i->fn, tmp, args)) { free(acc); free(tmp); return false; }
        memcpy(acc, tmp, v->type->size);
    }
    free(tmp); vec_destroy(v);
    v->data = acc; v->count = 1u; v->type = i->output_type;
    return true;
}

cflow_plan_step_fn cflow_plan_step_for_opcode(cflow_plan_opcode opcode) {
    switch (opcode) {
        case CMETA_PLAN_FILTER: return step_filter;
        case CMETA_PLAN_MAP: return step_map;
        case CMETA_PLAN_FLAT_MAP: return step_flat_map;
        case CMETA_PLAN_REDUCE: return step_reduce;
    }
    return NULL;
}

bool cflow_plan_eval_array(const cflow_plan *plan,
                           const void *inputs,
                           size_t input_count,
                           cflow_result *out) {
    const cflow_plan_impl *impl = plan_impl(plan);
    if (!plan || !impl || !out || !plan->input_type || !plan->output_type) return false;
    memset(out, 0, sizeof(*out));
    cflow_plan_value_vec v = {0};
    if (!copy_values(&v, inputs, input_count, plan->input_type)) return false;
    for (size_t pc = 0; pc < impl->count; ++pc) {
        const cflow_plan_inst *inst = &impl->code[pc];
        if (!inst->step || !inst->step(inst, &v)) { vec_destroy(&v); return false; }
    }
    if (!cmeta_type_equal(v.type, plan->output_type)) { vec_destroy(&v); return false; }
    out->data = v.data; out->count = v.count; out->type = v.type;
    return true;
}
