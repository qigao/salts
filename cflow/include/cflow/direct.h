#ifndef CFLOW_DIRECT_H
#define CFLOW_DIRECT_H

#include <cflow/stream.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Result of a generated Direct array evaluation. */
typedef enum cflow_direct_status {
  /** Evaluation completed and `output_count` contains the committed count. */
  CFLOW_DIRECT_OK = 0,
  /** A pointer, byte range, size calculation or aliasing contract is invalid. */
  CFLOW_DIRECT_INVALID_ARGUMENT,
  /** At least one schema stage does not satisfy the Direct value contract. */
  CFLOW_DIRECT_INELIGIBLE,
  /** The output capacity is less than the input count. */
  CFLOW_DIRECT_CAPACITY_EXCEEDED
} cflow_direct_status;

typedef enum cflow_direct_stage_kind {
  CFLOW_DIRECT_STAGE_FILTER = 0,
  CFLOW_DIRECT_STAGE_MAP
} cflow_direct_stage_kind;

static inline bool cflow_direct_type_eligible(const cmeta_type_desc *type) {
  const cmeta_trait_flags required = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY;
  return cmeta_type_desc_valid(type) && type->traits != NULL &&
         (type->traits->flags & required) == required;
}

static inline bool cflow_direct_size_bytes(size_t count, size_t item_size, size_t *bytes) {
  if (bytes == NULL || item_size == 0u || count > SIZE_MAX / item_size) return false;
  *bytes = count * item_size;
  return true;
}

static inline bool cflow_direct_range(const void *data, size_t bytes, uintptr_t *begin,
                                      uintptr_t *end) {
  uintptr_t address;

  if (begin == NULL || end == NULL || (bytes != 0u && data == NULL)) return false;
  address = (uintptr_t)data;
  if (bytes > UINTPTR_MAX - address) return false;
  *begin = address;
  *end = address + bytes;
  return true;
}

static inline bool cflow_direct_ranges_overlap(uintptr_t left_begin, uintptr_t left_end,
                                               uintptr_t right_begin, uintptr_t right_end) {
  return left_begin < right_end && right_begin < left_end;
}

static inline bool cflow_direct_buffers_valid(const void *inputs, size_t input_count,
                                              size_t input_size, void *outputs, size_t output_size,
                                              const size_t *output_count) {
  size_t input_bytes;
  size_t output_bytes;
  uintptr_t input_begin;
  uintptr_t input_end;
  uintptr_t output_begin;
  uintptr_t output_end;
  uintptr_t count_begin;
  uintptr_t count_end;

  if (output_count == NULL || !cflow_direct_size_bytes(input_count, input_size, &input_bytes) ||
      !cflow_direct_size_bytes(input_count, output_size, &output_bytes) ||
      !cflow_direct_range(inputs, input_bytes, &input_begin, &input_end) ||
      !cflow_direct_range(outputs, output_bytes, &output_begin, &output_end) ||
      !cflow_direct_range(output_count, sizeof(*output_count), &count_begin, &count_end))
    return false;

  return !cflow_direct_ranges_overlap(input_begin, input_end, output_begin, output_end) &&
         !cflow_direct_ranges_overlap(input_begin, input_end, count_begin, count_end) &&
         !cflow_direct_ranges_overlap(output_begin, output_end, count_begin, count_end);
}

static inline bool cflow_direct_stage_eligible(cmeta_callable callable,
                                               cflow_direct_stage_kind kind,
                                               const cmeta_type_desc **flow_type) {
  const cmeta_properties required =
      CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS;
  cmeta_callable bound;
  const cmeta_sig_desc *signature;

  if (flow_type == NULL || !cflow_direct_type_eligible(*flow_type) ||
      !cmeta_callable_bind(callable, &bound) || bound.capture_size != 0u ||
      !cmeta_effects_are_pure(bound.meta.effects) ||
      !cmeta_properties_include(bound.meta.properties, required))
    return false;

  signature = cmeta_fn_signature(bound.meta);
  if (signature == NULL || signature->protocol != CMETA_FN_PROTOCOL_VALUE ||
      signature->param_count != 1u || !cmeta_type_equal(signature->params[0], *flow_type))
    return false;

  if (kind == CFLOW_DIRECT_STAGE_FILTER)
    return cmeta_type_equal(signature->return_type, &cmeta_type_bool);
  if (kind != CFLOW_DIRECT_STAGE_MAP || !cflow_direct_type_eligible(signature->return_type))
    return false;

  *flow_type = signature->return_type;
  return true;
}

#ifndef __cplusplus

  #define CFLOW_DIRECT_STAGE_KIND_filter CFLOW_DIRECT_STAGE_FILTER
  #define CFLOW_DIRECT_STAGE_KIND_map CFLOW_DIRECT_STAGE_MAP
  #define CFLOW_DIRECT_STAGE_KIND_I(kind) CFLOW_DIRECT_STAGE_KIND_##kind
  #define CFLOW_DIRECT_STAGE_KIND(kind) CFLOW_DIRECT_STAGE_KIND_I(kind)

  #define CFlowDirectSteps(M, ...) CMETA_PP_FOR_EACH_I(M, CMETA_PP_NARG(__VA_ARGS__), __VA_ARGS__)

  #define CFLOW_DIRECT_ROW_APPLY_I(M, index, count, ...) M(index, count, __VA_ARGS__)
  #define CFLOW_DIRECT_ROW_APPLY(M, index, row, count)                                             \
    CFLOW_DIRECT_ROW_APPLY_I(M, index, count, CMETA_PP_UNPAREN row)

  #define CFLOW_DIRECT_COUNT_INDEXED(index, row, count) +1

  #define CFLOW_DIRECT_ELIGIBILITY_INDEXED(index, row, count)                                      \
    CFLOW_DIRECT_ROW_APPLY(CFLOW_DIRECT_ELIGIBILITY_ROW, index, row, count)
  #define CFLOW_DIRECT_ELIGIBILITY_ROW(index, count, kind, input_type, output_type, callable)      \
    if (!cflow_direct_stage_eligible((callable).fn, CFLOW_DIRECT_STAGE_KIND(kind),                 \
                                     &_cflow_direct_flow_type))                                    \
      return false;

  #define CFLOW_DIRECT_BUILD_INDEXED(index, row, count)                                            \
    CFLOW_DIRECT_ROW_APPLY(CFLOW_DIRECT_BUILD_ROW, index, row, count)
  #define CFLOW_DIRECT_BUILD_ROW(index, count, kind, input_type, output_type, callable)            \
    if (_cflow_direct_stream->kind(_cflow_direct_stream, (callable)) == NULL) return false;

  #define CFLOW_DIRECT_VALUE_I(index) _cflow_direct_value_##index
  #define CFLOW_DIRECT_VALUE(index) CFLOW_DIRECT_VALUE_I(index)
  #define CFLOW_DIRECT_INPUT_0 _cflow_direct_source_value
  #define CFLOW_DIRECT_INPUT_1 CFLOW_DIRECT_VALUE(0)
  #define CFLOW_DIRECT_INPUT_2 CFLOW_DIRECT_VALUE(1)
  #define CFLOW_DIRECT_INPUT_3 CFLOW_DIRECT_VALUE(2)
  #define CFLOW_DIRECT_INPUT_4 CFLOW_DIRECT_VALUE(3)
  #define CFLOW_DIRECT_INPUT_5 CFLOW_DIRECT_VALUE(4)
  #define CFLOW_DIRECT_INPUT_6 CFLOW_DIRECT_VALUE(5)
  #define CFLOW_DIRECT_INPUT_7 CFLOW_DIRECT_VALUE(6)
  #define CFLOW_DIRECT_INPUT_8 CFLOW_DIRECT_VALUE(7)
  #define CFLOW_DIRECT_INPUT_9 CFLOW_DIRECT_VALUE(8)
  #define CFLOW_DIRECT_INPUT_10 CFLOW_DIRECT_VALUE(9)
  #define CFLOW_DIRECT_INPUT_11 CFLOW_DIRECT_VALUE(10)
  #define CFLOW_DIRECT_INPUT_12 CFLOW_DIRECT_VALUE(11)
  #define CFLOW_DIRECT_INPUT_13 CFLOW_DIRECT_VALUE(12)
  #define CFLOW_DIRECT_INPUT_14 CFLOW_DIRECT_VALUE(13)
  #define CFLOW_DIRECT_INPUT_15 CFLOW_DIRECT_VALUE(14)
  #define CFLOW_DIRECT_INPUT_I(index) CFLOW_DIRECT_INPUT_##index
  #define CFLOW_DIRECT_INPUT(index) CFLOW_DIRECT_INPUT_I(index)

  #define CFLOW_DIRECT_LAST_1 0
  #define CFLOW_DIRECT_LAST_2 1
  #define CFLOW_DIRECT_LAST_3 2
  #define CFLOW_DIRECT_LAST_4 3
  #define CFLOW_DIRECT_LAST_5 4
  #define CFLOW_DIRECT_LAST_6 5
  #define CFLOW_DIRECT_LAST_7 6
  #define CFLOW_DIRECT_LAST_8 7
  #define CFLOW_DIRECT_LAST_9 8
  #define CFLOW_DIRECT_LAST_10 9
  #define CFLOW_DIRECT_LAST_11 10
  #define CFLOW_DIRECT_LAST_12 11
  #define CFLOW_DIRECT_LAST_13 12
  #define CFLOW_DIRECT_LAST_14 13
  #define CFLOW_DIRECT_LAST_15 14
  #define CFLOW_DIRECT_LAST_16 15
  #define CFLOW_DIRECT_LAST_I(count) CFLOW_DIRECT_LAST_##count
  #define CFLOW_DIRECT_LAST(count) CFLOW_DIRECT_LAST_I(count)

  #define CFLOW_DIRECT_EVAL_filter(index, input_type, output_type, callable)                       \
    _Static_assert(_Generic(&(CFLOW_DIRECT_INPUT(index)), input_type *: 1, default: 0),            \
                   "Direct filter input does not match the preceding stage");                      \
    _Static_assert(_Generic(&(typed_call(callable)), bool (*)(input_type): 1, default: 0),         \
                   "Direct filter callable does not match its declared input type");               \
    _Static_assert(_Generic((input_type *)0, output_type *: 1, default: 0),                        \
                   "Direct filter must preserve its input type");                                  \
    if (!typed_call(callable)(CFLOW_DIRECT_INPUT(index))) continue;                                \
    output_type CFLOW_DIRECT_VALUE(index) = CFLOW_DIRECT_INPUT(index);

  #define CFLOW_DIRECT_EVAL_map(index, input_type, output_type, callable)                          \
    _Static_assert(_Generic(&(CFLOW_DIRECT_INPUT(index)), input_type *: 1, default: 0),            \
                   "Direct map input does not match the preceding stage");                         \
    _Static_assert(_Generic(&(typed_call(callable)), output_type (*)(input_type): 1, default: 0),  \
                   "Direct map callable does not match its declared stage types");                 \
    output_type CFLOW_DIRECT_VALUE(index) = typed_call(callable)(CFLOW_DIRECT_INPUT(index));

  #define CFLOW_DIRECT_EVAL_INDEXED(index, row, count)                                             \
    CFLOW_DIRECT_ROW_APPLY(CFLOW_DIRECT_EVAL_ROW, index, row, count)
  #define CFLOW_DIRECT_EVAL_ROW(index, count, kind, input_type, output_type, callable)             \
    CMETA_PP_CAT(CFLOW_DIRECT_EVAL_, kind)(index, input_type, output_type, callable)

  /**
   * Generate a closed synchronous Filter/Map pipeline and its Surface builder.
   *
   * @param name Prefix for the generated `<name>_eligible`, `<name>_build` and
   *             `<name>_eval_array` translation-unit-local functions.
   * @param input_type C type of each borrowed input array item.
   * @param input_desc Pointer to the matching CMeta source descriptor.
   * @param output_type C type stored in the caller-owned output array.
   * @param stage_count Literal number of schema rows, from 1 through 16. A static
   *                    assertion rejects a count that differs from the schema.
   * @param pipeline_schema Function-like schema macro created with
   *                        `CFlowDirectSteps` and normalized
   *                        `(kind, in-type, out-type, callable)` Filter/Map rows.
   *
   * `<name>_build(stream)` initializes `stream` and appends the schema's Surface
   * operators. The caller must pass zeroed or otherwise uninitialized storage and
   * call `cflow_stream_destroy` after a successful or partially failed build.
   *
   * `<name>_eval_array(inputs, input_count, outputs, output_capacity,
   * output_count)` borrows disjoint input storage and writes to disjoint
   * caller-owned output storage. For non-empty input, capacity must be at least
   * `input_count`. No stage callback is indirectly invoked and no allocation or
   * scheduler operation occurs. The returned `cflow_direct_status` distinguishes
   * invalid storage, ineligible schemas and insufficient capacity; no failure
   * selects Plan or Kernel implicitly.
   *
   * Example:
   * @code
   * #define Steps(M) CFlowDirectSteps(M, (map, int, long, square))
   * cflow_direct_pipeline(squares, int, &cmeta_type_int, long, 1, Steps);
   * @endcode
   */
  #define cflow_direct_pipeline(name, input_type, input_desc, output_type, stage_count,            \
                                pipeline_schema)                                                   \
    enum { name##_direct_stage_count = 0 Replay(pipeline_schema, CFLOW_DIRECT_COUNT_INDEXED) };    \
    _Static_assert(name##_direct_stage_count == (stage_count),                                     \
                   "Direct stage count does not match its schema");                                \
    CMETA_INLINE bool name##_eligible(void) {                                                      \
      const cmeta_type_desc *_cflow_direct_flow_type = (input_desc);                               \
      Replay(pipeline_schema, CFLOW_DIRECT_ELIGIBILITY_INDEXED) return true;                       \
    }                                                                                              \
    CMETA_INLINE bool name##_build(cflow_stream *_cflow_direct_stream) {                           \
      if (_cflow_direct_stream == NULL || !name##_eligible() ||                                    \
          cflow_stream_init(_cflow_direct_stream, (input_desc)) == NULL)                           \
        return false;                                                                              \
      Replay(pipeline_schema,                                                                      \
             CFLOW_DIRECT_BUILD_INDEXED) return cflow_stream_ok(_cflow_direct_stream);             \
    }                                                                                              \
    CMETA_INLINE cflow_direct_status name##_eval_array(                                            \
        const input_type *_cflow_direct_inputs, size_t _cflow_direct_count,                        \
        output_type *_cflow_direct_outputs, size_t _cflow_direct_capacity,                         \
        size_t *_cflow_direct_output_count) {                                                      \
      size_t _cflow_direct_index;                                                                  \
      size_t _cflow_direct_written = 0u;                                                           \
      if (!cflow_direct_buffers_valid(_cflow_direct_inputs, _cflow_direct_count,                   \
                                      sizeof(input_type), _cflow_direct_outputs,                   \
                                      sizeof(output_type), _cflow_direct_output_count))            \
        return CFLOW_DIRECT_INVALID_ARGUMENT;                                                      \
      *_cflow_direct_output_count = 0u;                                                            \
      if (!name##_eligible()) return CFLOW_DIRECT_INELIGIBLE;                                      \
      if (_cflow_direct_capacity < _cflow_direct_count) return CFLOW_DIRECT_CAPACITY_EXCEEDED;     \
      for (_cflow_direct_index = 0u; _cflow_direct_index < _cflow_direct_count;                    \
           ++_cflow_direct_index) {                                                                \
        input_type _cflow_direct_source_value = _cflow_direct_inputs[_cflow_direct_index];         \
        Replay(pipeline_schema, CFLOW_DIRECT_EVAL_INDEXED) _Static_assert(                         \
            _Generic(&(CFLOW_DIRECT_VALUE(CFLOW_DIRECT_LAST(stage_count))),                        \
                output_type *: 1,                                                                  \
                default: 0),                                                                       \
            "Direct pipeline output type does not match its declaration");                         \
        _cflow_direct_outputs[_cflow_direct_written++] =                                           \
            CFLOW_DIRECT_VALUE(CFLOW_DIRECT_LAST(stage_count));                                    \
      }                                                                                            \
      *_cflow_direct_output_count = _cflow_direct_written;                                         \
      return CFLOW_DIRECT_OK;                                                                      \
    }                                                                                              \
    typedef int name##_direct_pipeline_declaration

#endif /* !__cplusplus */

#ifdef __cplusplus
}
#endif

#endif
