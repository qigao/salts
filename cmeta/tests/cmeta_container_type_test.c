#include <cmeta/data.h>
#include <cmeta/range.h>
#include "tinytest.h"

static const cmeta_generic_desc cmeta_test_sequence_generic =
    CMETA_GENERIC_DESC_INIT("test.Sequence", "Sequence", 1u, 1u,
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

  it("exposes a validated container extension prefix") {
    cmeta_test_sequence sequence = {
        .cmeta = {&cmeta_test_sequence_desc},
        .element_type = &cmeta_type_int
    };

    check_true(cmeta_container_extension(&sequence) ==
               &cmeta_test_sequence_ext);
    check_null(cmeta_container_extension(NULL));
  }

  it("keeps a legacy extension valid after semantic tails are appended") {
    cmeta_test_sequence sequence = {
        .cmeta = {&cmeta_test_sequence_desc},
        .element_type = &cmeta_type_int
    };

    check_equal(cmeta_test_sequence_ext.struct_size,
                offsetof(cmeta_container_ext, type) +
                    sizeof(cmeta_test_sequence_ext.type));
    check_true(cmeta_container_extension(&sequence) ==
               &cmeta_test_sequence_ext);
    check_true(cmeta_container_type_application_valid(&sequence));
    check_null(cmeta_container_data_descriptor(&sequence));
  }

  it("projects semantic data only for a valid generic application") {
    cmeta_test_sequence sequence = {
        .cmeta = {&cmeta_test_semantic_sequence_desc},
        .element_type = &cmeta_type_int
    };

    check_true(cmeta_container_type_application_valid(&sequence));
    check_true(cmeta_container_data_descriptor(&sequence) ==
               &cmeta_data_sequence);

    sequence.element_type = &cmeta_test_opaque_type;
    check_false(cmeta_container_type_application_valid(&sequence));
    check_null(cmeta_container_data_descriptor(&sequence));
  }

  it("rejects a non-container semantic descriptor in the container tail") {
    cmeta_container_ext ext = cmeta_test_semantic_sequence_ext;
    cmeta_container_desc desc = cmeta_test_semantic_sequence_desc;
    cmeta_test_sequence sequence = {
        .cmeta = {&desc},
        .element_type = &cmeta_type_int
    };

    ext.data = &cmeta_data_int;
    desc.ext = &ext;

    check_true(cmeta_container_type_application_valid(&sequence));
    check_true(cmeta_data_desc_valid(&cmeta_data_int));
    check_null(cmeta_container_data_descriptor(&sequence));
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
