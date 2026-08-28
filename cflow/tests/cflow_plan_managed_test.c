#include <cflow/cflow.h>
#include <cflow/plan_internal.h>
#include "tinytest.h"

#include <stdint.h>
#include <stdlib.h>

typedef struct plan_managed_value {
    _Alignas(64) int *resource;
} plan_managed_value;

static size_t plan_managed_copy_attempts;
static size_t plan_managed_copies;
static size_t plan_managed_moves;
static size_t plan_managed_destroys;
static size_t plan_managed_live_resources;
static size_t plan_managed_fail_copy_at;
static int plan_managed_fail_map_value;

static plan_managed_value plan_managed_make(int value) {
    plan_managed_value result = {0};

    result.resource = (int *)malloc(sizeof(*result.resource));
    if (result.resource) {
        *result.resource = value;
        ++plan_managed_live_resources;
    }
    return result;
}

static bool plan_managed_copy(void *destination_, const void *source_) {
    plan_managed_value *destination = (plan_managed_value *)destination_;
    const plan_managed_value *source = (const plan_managed_value *)source_;
    const size_t attempt = plan_managed_copy_attempts++;

    destination->resource = NULL;
    if (attempt == plan_managed_fail_copy_at) return false;
    if (!source->resource) {
        ++plan_managed_copies;
        return true;
    }
    destination->resource = (int *)malloc(sizeof(*destination->resource));
    if (!destination->resource) return false;
    *destination->resource = *source->resource;
    ++plan_managed_copies;
    ++plan_managed_live_resources;
    return true;
}

static void plan_managed_move(void *destination_, void *source_) {
    plan_managed_value *destination = (plan_managed_value *)destination_;
    plan_managed_value *source = (plan_managed_value *)source_;

    destination->resource = source->resource;
    source->resource = NULL;
    ++plan_managed_moves;
}

static void plan_managed_destroy(void *value_) {
    plan_managed_value *value = (plan_managed_value *)value_;

    if (value->resource) {
        --plan_managed_live_resources;
        free(value->resource);
        value->resource = NULL;
    }
    ++plan_managed_destroys;
}

static const cmeta_type_traits plan_managed_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = plan_managed_copy,
    .move_construct = plan_managed_move,
    .destroy = plan_managed_destroy
};

static const cmeta_type_desc plan_managed_type = {
    .name = "plan_managed_value",
    .size = sizeof(plan_managed_value),
    .align = _Alignof(plan_managed_value),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &plan_managed_traits,
    .identity = NULL
};

static void plan_managed_reset(void) {
    plan_managed_copy_attempts = 0u;
    plan_managed_copies = 0u;
    plan_managed_moves = 0u;
    plan_managed_destroys = 0u;
    plan_managed_live_resources = 0u;
    plan_managed_fail_copy_at = SIZE_MAX;
    plan_managed_fail_map_value = INT32_MIN;
}

static void plan_managed_destroy_inputs(plan_managed_value *values,
                                        size_t count) {
    for (size_t index = 0u; index < count; ++index)
        plan_managed_destroy(&values[index]);
}

static bool plan_managed_compile_slice(cflow_plan *plan,
                                       size_t skip,
                                       size_t take) {
    cflow_stream stream = {0};
    bool ok = cflow_stream_init(&stream, &plan_managed_type);

    if (ok && skip != 0u) ok = stream.skip(&stream, skip) != NULL;
    if (ok && take != SIZE_MAX) ok = stream.take(&stream, take) != NULL;
    if (ok) ok = cflow_plan_compile_surface(plan, &stream.graph, NULL);
    cflow_stream_destroy(&stream);
    return ok;
}

static bool plan_managed_filter_invoke(const cmeta_callable *self,
                                       void *out,
                                       const void *const *args) {
    const plan_managed_value *value;

    (void)self;
    if (!out || !args || !args[0]) return false;
    value = (const plan_managed_value *)args[0];
    if (!value->resource) return false;
    *(_Bool *)out = (*value->resource % 2) == 0;
    return true;
}

static bool plan_managed_map_invoke(const cmeta_callable *self,
                                    void *out,
                                    const void *const *args) {
    const plan_managed_value *value;
    plan_managed_value mapped;

    (void)self;
    if (!out || !args || !args[0]) return false;
    value = (const plan_managed_value *)args[0];
    if (!value->resource || *value->resource == plan_managed_fail_map_value)
        return false;
    mapped = plan_managed_make(*value->resource + 1);
    if (!mapped.resource) return false;
    *(plan_managed_value *)out = mapped;
    return true;
}

static bool plan_managed_reduce_invoke(const cmeta_callable *self,
                                       void *out,
                                       const void *const *args) {
    const plan_managed_value *left;
    const plan_managed_value *right;
    plan_managed_value sum;

    (void)self;
    if (!out || !args || !args[0] || !args[1]) return false;
    left = (const plan_managed_value *)args[0];
    right = (const plan_managed_value *)args[1];
    if (!left->resource || !right->resource) return false;
    sum = plan_managed_make(*left->resource + *right->resource);
    if (!sum.resource) return false;
    *(plan_managed_value *)out = sum;
    return true;
}

static cmeta_gen_status plan_managed_generate(
    const cmeta_callable *self,
    const void *input_,
    void *out,
    size_t *cursor) {
    const plan_managed_value *input =
        (const plan_managed_value *)input_;
    plan_managed_value generated;

    (void)self;
    if (!input || !input->resource || !out || !cursor)
        return CMETA_GEN_ERROR;
    if (*cursor > 1u) return CMETA_GEN_DONE;
    generated = plan_managed_make(
        *input->resource + (*cursor == 0u ? 0 : 100));
    if (!generated.resource) return CMETA_GEN_ERROR;
    *(plan_managed_value *)out = generated;
    ++*cursor;
    return *cursor == 1u ? CMETA_GEN_VALUE : CMETA_GEN_VALUE_AND_DONE;
}

static bool plan_managed_eval_instructions(cflow_plan_inst *instructions,
                                           size_t instruction_count,
                                           const plan_managed_value *input,
                                           size_t input_count,
                                           cflow_result *out) {
    cflow_plan_impl impl = {
        .code = instructions,
        .count = instruction_count,
        .terminal_reduce_index = SIZE_MAX,
        .managed_values = true
    };
    const cflow_plan plan = {
        .impl = &impl,
        .input_type = &plan_managed_type,
        .output_type = &plan_managed_type
    };

    return cflow_plan_eval_array(&plan, input, input_count, out);
}

spec("CFlow compiled Plan managed values") {
    before_each() {
        plan_managed_reset();
    }

    it("owns managed source results independently until result destroy") {
        plan_managed_value input[] = {
            plan_managed_make(4), plan_managed_make(8), plan_managed_make(12)
        };
        cflow_plan plan = {0};
        cflow_result result = {0};
        const plan_managed_value *output;

        check_true(plan_managed_compile_slice(&plan, 0u, SIZE_MAX));
        check_true(cflow_plan_eval_array(&plan, input, 3u, &result));
        check_equal(result.count, (size_t)3u);
        check_true(cmeta_type_equal(result.type, &plan_managed_type));
        output = (const plan_managed_value *)result.data;
        check_not_null(output);
        check_equal((uintptr_t)output % _Alignof(plan_managed_value),
                    (uintptr_t)0u);
        check_equal(*output[0].resource, 4);
        check_equal(*output[1].resource, 8);
        check_equal(*output[2].resource, 12);
        check_true(output[0].resource != input[0].resource);
        check_equal(plan_managed_copies, (size_t)3u);
        check_equal(plan_managed_live_resources, (size_t)6u);

        cflow_result_destroy(&result);
        check_equal(plan_managed_live_resources, (size_t)3u);
        plan_managed_destroy_inputs(input, 3u);
        cflow_plan_destroy(&plan);
        check_equal(plan_managed_live_resources, (size_t)0u);
        check_equal(plan_managed_destroys, (size_t)6u);
    }

    it("destroys managed values discarded by take exactly once") {
        plan_managed_value input[] = {
            plan_managed_make(3), plan_managed_make(6), plan_managed_make(9)
        };
        cflow_plan plan = {0};
        cflow_result result = {0};

        check_true(plan_managed_compile_slice(&plan, 0u, 2u));
        check_true(cflow_plan_eval_array(&plan, input, 3u, &result));
        check_equal(result.count, (size_t)2u);
        check_equal(plan_managed_live_resources, (size_t)5u);

        cflow_result_destroy(&result);
        plan_managed_destroy_inputs(input, 3u);
        cflow_plan_destroy(&plan);
        check_equal(plan_managed_live_resources, (size_t)0u);
        check_equal(plan_managed_copies, (size_t)3u);
        check_equal(plan_managed_moves, (size_t)0u);
        check_equal(plan_managed_destroys, (size_t)6u);
    }

    it("rebuilds skip output without aliasing borrowed managed inputs") {
        plan_managed_value input[] = {
            plan_managed_make(5), plan_managed_make(10), plan_managed_make(15)
        };
        cflow_plan plan = {0};
        cflow_result result = {0};
        const plan_managed_value *output;

        check_true(plan_managed_compile_slice(&plan, 1u, SIZE_MAX));
        check_true(cflow_plan_eval_array(&plan, input, 3u, &result));
        check_equal(result.count, (size_t)2u);
        output = (const plan_managed_value *)result.data;
        check_equal(*output[0].resource, 10);
        check_equal(*output[1].resource, 15);
        check_true(output[0].resource != input[1].resource);
        check_equal(plan_managed_live_resources, (size_t)5u);

        cflow_result_destroy(&result);
        plan_managed_destroy_inputs(input, 3u);
        cflow_plan_destroy(&plan);
        check_equal(plan_managed_live_resources, (size_t)0u);
        check_equal(plan_managed_copies, (size_t)5u);
        check_equal(plan_managed_moves, (size_t)0u);
        check_equal(plan_managed_destroys, (size_t)8u);
    }

    it("cleans a partially copied input and leaves result zero on failure") {
        plan_managed_value input[] = {
            plan_managed_make(7), plan_managed_make(14), plan_managed_make(21)
        };
        cflow_plan plan = {0};
        cflow_result result = {0};

        check_true(plan_managed_compile_slice(&plan, 0u, SIZE_MAX));
        plan_managed_fail_copy_at = 1u;
        check_false(cflow_plan_eval_array(&plan, input, 3u, &result));
        check_null(result.data);
        check_equal(result.count, (size_t)0u);
        check_null(result.type);
        check_equal(plan_managed_copy_attempts, (size_t)2u);
        check_equal(plan_managed_copies, (size_t)1u);
        check_equal(plan_managed_live_resources, (size_t)3u);

        cflow_result_destroy(&result);
        plan_managed_destroy_inputs(input, 3u);
        cflow_plan_destroy(&plan);
        check_equal(plan_managed_live_resources, (size_t)0u);
        check_equal(plan_managed_destroys, (size_t)4u);
    }

    it("keeps managed filter and map ownership balanced") {
        plan_managed_value input[] = {
            plan_managed_make(4), plan_managed_make(5), plan_managed_make(12)
        };
        cflow_plan_call map_call = {
            .invoke = plan_managed_map_invoke,
            .input_type = &plan_managed_type,
            .output_type = &plan_managed_type
        };
        cflow_plan_inst instructions[] = {
            {
                .opcode = CMETA_PLAN_FILTER,
                .step = cflow_plan_step_for_opcode(CMETA_PLAN_FILTER),
                .input_type = &plan_managed_type,
                .output_type = &plan_managed_type,
                .call = {
                    .invoke = plan_managed_filter_invoke,
                    .input_type = &plan_managed_type,
                    .output_type = &cmeta_type_bool
                }
            },
            {
                .opcode = CMETA_PLAN_MAP,
                .step = cflow_plan_step_for_opcode(CMETA_PLAN_MAP),
                .input_type = &plan_managed_type,
                .output_type = &plan_managed_type,
                .fn_chain = &map_call,
                .fn_chain_count = 1u
            }
        };
        cflow_result result = {0};
        const plan_managed_value *output;

        check_true(plan_managed_eval_instructions(
            instructions, 2u, input, 3u, &result));
        check_equal(result.count, (size_t)2u);
        output = (const plan_managed_value *)result.data;
        check_equal(*output[0].resource, 5);
        check_equal(*output[1].resource, 13);
        check_equal(plan_managed_live_resources, (size_t)5u);

        cflow_result_destroy(&result);
        plan_managed_destroy_inputs(input, 3u);
        check_equal(plan_managed_live_resources, (size_t)0u);
        check_equal(plan_managed_copies, (size_t)5u);
        check_equal(plan_managed_moves, (size_t)0u);
        check_equal(plan_managed_destroys, (size_t)10u);
    }

    it("destroys managed map prefixes when a later invocation fails") {
        plan_managed_value input[] = {
            plan_managed_make(7), plan_managed_make(14), plan_managed_make(21)
        };
        cflow_plan_call map_call = {
            .invoke = plan_managed_map_invoke,
            .input_type = &plan_managed_type,
            .output_type = &plan_managed_type
        };
        cflow_plan_inst instruction = {
            .opcode = CMETA_PLAN_MAP,
            .step = cflow_plan_step_for_opcode(CMETA_PLAN_MAP),
            .input_type = &plan_managed_type,
            .output_type = &plan_managed_type,
            .fn_chain = &map_call,
            .fn_chain_count = 1u
        };
        cflow_result result = {0};

        plan_managed_fail_map_value = 14;
        check_false(plan_managed_eval_instructions(
            &instruction, 1u, input, 3u, &result));
        check_null(result.data);
        check_equal(result.count, (size_t)0u);
        check_null(result.type);
        check_equal(plan_managed_live_resources, (size_t)3u);
        check_equal(plan_managed_destroys, (size_t)4u);

        cflow_result_destroy(&result);
        plan_managed_destroy_inputs(input, 3u);
        check_equal(plan_managed_live_resources, (size_t)0u);
        check_equal(plan_managed_destroys, (size_t)7u);
    }

    it("reduces managed values through owned accumulator slots") {
        plan_managed_value input[] = {
            plan_managed_make(2), plan_managed_make(3), plan_managed_make(5)
        };
        cflow_plan_inst instruction = {
            .opcode = CMETA_PLAN_REDUCE,
            .step = cflow_plan_step_for_opcode(CMETA_PLAN_REDUCE),
            .input_type = &plan_managed_type,
            .output_type = &plan_managed_type,
            .call = {
                .invoke = plan_managed_reduce_invoke,
                .input_type = &plan_managed_type,
                .output_type = &plan_managed_type
            }
        };
        cflow_result result = {0};
        const plan_managed_value *output;

        check_true(plan_managed_eval_instructions(
            &instruction, 1u, input, 3u, &result));
        check_equal(result.count, (size_t)1u);
        output = (const plan_managed_value *)result.data;
        check_equal(*output[0].resource, 10);
        check_equal(plan_managed_live_resources, (size_t)4u);

        cflow_result_destroy(&result);
        plan_managed_destroy_inputs(input, 3u);
        check_equal(plan_managed_live_resources, (size_t)0u);
        check_equal(plan_managed_copies, (size_t)4u);
        check_equal(plan_managed_moves, (size_t)3u);
        check_equal(plan_managed_destroys, (size_t)12u);
    }

    it("moves each managed flat-map yield into result storage") {
        plan_managed_value input[] = {
            plan_managed_make(1), plan_managed_make(2), plan_managed_make(3)
        };
        cflow_plan_inst instruction = {
            .opcode = CMETA_PLAN_FLAT_MAP,
            .step = cflow_plan_step_for_opcode(CMETA_PLAN_FLAT_MAP),
            .input_type = &plan_managed_type,
            .output_type = &plan_managed_type,
            .call = {
                .fn = {
                    .meta = {
                        .sig = (cmeta_sig)(CMETA_SIG_INVALID + 1)
                    },
                    .generate = plan_managed_generate
                },
                .input_type = &plan_managed_type,
                .output_type = &plan_managed_type
            }
        };
        cflow_result result = {0};
        const plan_managed_value *output;

        check_true(plan_managed_eval_instructions(
            &instruction, 1u, input, 3u, &result));
        check_equal(result.count, (size_t)6u);
        output = (const plan_managed_value *)result.data;
        check_equal(*output[0].resource, 1);
        check_equal(*output[1].resource, 101);
        check_equal(*output[4].resource, 3);
        check_equal(*output[5].resource, 103);
        check_equal(plan_managed_live_resources, (size_t)9u);

        cflow_result_destroy(&result);
        plan_managed_destroy_inputs(input, 3u);
        check_equal(plan_managed_live_resources, (size_t)0u);
        check_equal(plan_managed_copies, (size_t)3u);
        check_equal(plan_managed_moves, (size_t)6u);
        check_equal(plan_managed_destroys, (size_t)18u);
    }

    it("rejects a value type with an incomplete lifecycle contract") {
        const cmeta_type_traits incomplete_traits = {
            .flags = CMETA_TRAIT_COPY,
            .copy_construct = plan_managed_copy
        };
        const cmeta_type_desc incomplete_type = {
            .name = "incomplete_plan_value",
            .size = sizeof(plan_managed_value),
            .align = _Alignof(plan_managed_value),
            .kind = CMETA_T_OBJECT,
            .traits = &incomplete_traits
        };
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_plan plan = {0};

        normalized.root = CMETA_INVALID_ID;
        cflow_graph_init(&surface, &incomplete_type);
        check_true(cflow_graph_normalize(&normalized, &surface));
        check_false(cflow_plan_graph_supported(&normalized));
        check_false(cflow_plan_compile(&plan, &normalized, NULL));
        check_null(plan.impl);
        check_equal(plan.error, "plan requires supported value lifecycle");

        cflow_plan_destroy(&plan);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }
}
