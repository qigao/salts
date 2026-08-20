#ifndef C11_STREAM_TYPED_H
#define C11_STREAM_TYPED_H

#include "stream.h"

/*
 * Optional typed facade.
 *
 * It deliberately does NOT implement stream semantics. It only creates
 * small, compiler-checked adapters from typed business callbacks to the
 * type-erased core ABI.
 */

#define STREAM_DEFINE_PREDICATE(adapter_name, Type, typed_fn) \
    static bool adapter_name(const void *value)                \
    {                                                           \
        return typed_fn((const Type *)value);                   \
    }

#define STREAM_DEFINE_MAPPER(adapter_name, InType, OutType, typed_fn) \
    static stream_result_t adapter_name(const void *input, void *output) \
    {                                                                  \
        const InType *in_ = (const InType *)input;                     \
        OutType *out_ = (OutType *)output;                             \
        *out_ = typed_fn(in_);                                         \
        return STREAM_OK;                                              \
    }

#define STREAM_DEFINE_MAPPER_RESULT(adapter_name, InType, OutType, typed_fn) \
    static stream_result_t adapter_name(const void *input, void *output)     \
    {                                                                         \
        return typed_fn((const InType *)input, (OutType *)output);            \
    }

#define STREAM_DEFINE_CONSUMER(adapter_name, Type, typed_fn) \
    static void adapter_name(const void *value)                 \
    {                                                            \
        typed_fn((const Type *)value);                           \
    }

#define STREAM_FILTER_TYPED(s, adapter_name) \
    ((s)->filter((s), (adapter_name)))

#define STREAM_MAP_TYPED(s, OutType, adapter_name) \
    ((s)->map((s), sizeof(OutType), (adapter_name)))

#define STREAM_FOREACH_TYPED(s, adapter_name) \
    ((s)->for_each((s), (adapter_name)))

#endif
