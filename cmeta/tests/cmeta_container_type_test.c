#include <cmeta/range.h>
#include "tinytest.h"

static const cmeta_generic_desc cmeta_test_sequence_generic =
    CMETA_GENERIC_DESC_INIT("test.Sequence", "Sequence", 1u, 1u,
                            CMETA_GENERIC_CONTAINER);
static const cmeta_generic_desc cmeta_test_sequence_generic_alias =
    CMETA_GENERIC_DESC_INIT("test.Sequence", "Sequence alias", 1u, 1u,
                            CMETA_GENERIC_CONTAINER);

typedef struct cmeta_test_sequence {
    cmeta_container_header cmeta;
    const cmeta_type_desc *element_type;
} cmeta_test_sequence;

static const cmeta_type_desc cmeta_test_opaque_type = {
    .name = "opaque",
    .size = sizeof(int),
    .align = _Alignof(int),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_type_desc cmeta_test_sequence_storage_type = {
    .name = "test_sequence_storage",
    .size = sizeof(cmeta_test_sequence),
    .align = _Alignof(cmeta_test_sequence),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_type_desc *cmeta_test_sequence_argument(
    const void *object, size_t index) {
    const cmeta_test_sequence *sequence =
        (const cmeta_test_sequence *)object;
    return sequence != NULL && index == 0u ? sequence->element_type : NULL;
}

static const cmeta_container_type_ops cmeta_test_sequence_type_ops = {
    .struct_size = offsetof(cmeta_container_type_ops, argument) +
                   sizeof(((cmeta_container_type_ops *)0)->argument),
    .abi_version = CMETA_CONTAINER_TYPE_OPS_ABI_VERSION,
    .constructor = &cmeta_test_sequence_generic,
    .arity = 1u,
    .argument = cmeta_test_sequence_argument
};

static const cmeta_container_ext cmeta_test_sequence_ext = {
    .struct_size = offsetof(cmeta_container_ext, type) +
                   sizeof(((cmeta_container_ext *)0)->type),
    .abi_version = CMETA_CONTAINER_EXT_ABI_VERSION,
    .type = &cmeta_test_sequence_type_ops
};

static const cmeta_container_ext cmeta_test_semantic_sequence_ext = {
    .struct_size = offsetof(cmeta_container_ext, data) +
                   sizeof(((cmeta_container_ext *)0)->data),
    .abi_version = CMETA_CONTAINER_EXT_ABI_VERSION,
    .type = &cmeta_test_sequence_type_ops,
    .data = &cmeta_data_sequence
};

static const cmeta_container_desc cmeta_test_sequence_desc = {
    .name = "test_sequence",
    .container_type = NULL,
    .element_type = NULL,
    .key_type = NULL,
    .value_type = NULL,
    .range = NULL,
    .keys_range = NULL,
    .values_range = NULL,
    .entries_range = NULL,
    .collector = NULL,
    .ext = &cmeta_test_sequence_ext
};

static const cmeta_container_desc cmeta_test_semantic_sequence_desc = {
    .name = "test_semantic_sequence",
    .container_type = NULL,
    .element_type = NULL,
    .key_type = NULL,
    .value_type = NULL,
    .range = NULL,
    .keys_range = NULL,
    .values_range = NULL,
    .entries_range = NULL,
    .collector = NULL,
    .ext = &cmeta_test_semantic_sequence_ext
};

static const cmeta_container_desc cmeta_test_constructible_sequence_desc;

static cmeta_status cmeta_test_sequence_bind_types(
    void *object, const cmeta_type_desc *const *arguments, size_t arity) {
    cmeta_test_sequence *sequence = (cmeta_test_sequence *)object;
    if (sequence == NULL || arguments == NULL || arity != 1u ||
        arguments[0] == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (sequence->cmeta.descriptor != NULL || sequence->element_type != NULL)
        return CMETA_TYPE_MISMATCH;
    sequence->cmeta.descriptor = &cmeta_test_constructible_sequence_desc;
    sequence->element_type = arguments[0];
    return CMETA_OK;
}

static const cmeta_container_construct_ops cmeta_test_sequence_construct_ops = {
    .struct_size = offsetof(cmeta_container_construct_ops, bind_types) +
                   sizeof(((cmeta_container_construct_ops *)0)->bind_types),
    .abi_version = CMETA_CONTAINER_CONSTRUCT_OPS_ABI_VERSION,
    .descriptor = &cmeta_test_constructible_sequence_desc,
    .bind_types = cmeta_test_sequence_bind_types
};

static const cmeta_container_ext cmeta_test_constructible_sequence_ext = {
    .struct_size = offsetof(cmeta_container_ext, construction) +
                   sizeof(((cmeta_container_ext *)0)->construction),
    .abi_version = CMETA_CONTAINER_EXT_ABI_VERSION,
    .type = &cmeta_test_sequence_type_ops,
    .data = NULL,
    .construction = &cmeta_test_sequence_construct_ops
};

static const cmeta_container_desc cmeta_test_constructible_sequence_desc = {
    .name = "test_constructible_sequence",
    .container_type = NULL,
    .element_type = NULL,
    .key_type = NULL,
    .value_type = NULL,
    .range = NULL,
    .keys_range = NULL,
    .values_range = NULL,
    .entries_range = NULL,
    .collector = NULL,
    .ext = &cmeta_test_constructible_sequence_ext
};

spec("CMeta container generic type applications") {
  it("exposes a validated generic application from a concrete container") {
    cmeta_test_sequence sequence = {
        .cmeta = {&cmeta_test_sequence_desc},
        .element_type = &cmeta_type_int
    };

    check_true(cmeta_container_type_application_valid(&sequence));
    check_true(cmeta_container_type_constructor(&sequence) ==
               &cmeta_test_sequence_generic);
    check_equal(cmeta_container_type_arity(&sequence), (size_t)1u);
    check_true(cmeta_container_type_argument(&sequence, 0u) ==
               &cmeta_type_int);
    check_null(cmeta_container_type_argument(&sequence, 1u));
  }

  it("keeps older extension prefixes valid and construction absent") {
    cmeta_test_sequence type_only = {
        .cmeta = {&cmeta_test_sequence_desc},
        .element_type = &cmeta_type_int
    };
    cmeta_test_sequence semantic = {
        .cmeta = {&cmeta_test_semantic_sequence_desc},
        .element_type = &cmeta_type_int
    };

    check_true(cmeta_container_extension(&type_only) ==
               &cmeta_test_sequence_ext);
    check_null(cmeta_container_extension(NULL));
    check_null(cmeta_container_data(&type_only));
    check_null(cmeta_container_construction(&type_only));

    check_true(cmeta_container_data(&semantic) == &cmeta_data_sequence);
    check_null(cmeta_container_construction(&semantic));
  }

  it("binds an all-zero object from declaration-side construction metadata") {
    static const cmeta_type_desc *const arguments[] = { &cmeta_type_int };
    cmeta_declared_type declared = {
        &cmeta_test_sequence_storage_type,
        &cmeta_test_sequence_generic_alias,
        arguments,
        1u,
        &cmeta_test_sequence_construct_ops
    };
    cmeta_test_sequence sequence = {0};

    check_true(cmeta_declared_type_constructible(&declared));
    check_equal(cmeta_container_bind_types(&sequence, &declared), CMETA_OK);
    check_true(sequence.cmeta.descriptor ==
               &cmeta_test_constructible_sequence_desc);
    check_true(sequence.element_type == &cmeta_type_int);
    check_true(cmeta_container_construction(&sequence) ==
               &cmeta_test_sequence_construct_ops);
    check_true(cmeta_container_type_application_valid(&sequence));
  }

  it("rejects a concrete argument without a CMeta type identity") {
    cmeta_test_sequence sequence = {
        .cmeta = {&cmeta_test_sequence_desc},
        .element_type = &cmeta_test_opaque_type
    };

    check_false(cmeta_container_type_application_valid(&sequence));
    check_true(cmeta_container_type_argument(&sequence, 0u) ==
               &cmeta_test_opaque_type);
  }
}
