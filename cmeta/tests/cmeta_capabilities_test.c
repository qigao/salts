#include "cmeta_capabilities_fixture.h"
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

typedef unsigned char capability_triplet[3];
cmeta_struct(CapabilityRecord,
    cmeta_field(int, id)
    cmeta_field(capability_triplet, bytes)
);
static bool record_equal(const void *left, const void *right) {
    return ((const CapabilityRecord *)left)->id == ((const CapabilityRecord *)right)->id;
}
static uint64_t record_hash(const void *object) {
    return (uint64_t)((const CapabilityRecord *)object)->id;
}
cmeta_traits(CapabilityRecord, cmeta_trait(Equal, record_equal),
             cmeta_trait(Hashable, record_hash));
cmeta_require_trait(CapabilityRecord, Hashable);
cmeta_require_field(CapabilityRecord, id, int);
cmeta_require_field(CapabilityRecord, bytes, capability_triplet);
_Static_assert(!cmeta_has_trait(CapabilityRecord, Movable), "undeclared traits stay absent");

typedef unsigned char capability_byte[1];
cmeta_variant(CapabilityBadSize, "test.BadSize",
    cmeta_case(Bad, 1, capability_byte, &cmeta_data_int64)
);

static const cmeta_data_desc unsupported_data = {
    .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.Unsupported.data", .display_name = "Unsupported",
    .kind = CMETA_DATA_CUSTOM, .storage_type = &cmeta_type_int,
    .shape = &cmeta_type_int
};
cmeta_variant(CapabilityUnsupported, "test.UnsupportedVariant",
    cmeta_case(Value, 1, int, &unsupported_data)
);

spec("CMeta type capabilities and variants") {
    static CapabilityValue first, second, third;
    static capability_owned input;

    before_each() {
        owned_live = owned_frees = owned_moves = 0;
        fail_copy = fail_init = false;
        input = (capability_owned){0};
        check_equal(CapabilityValue_init(&first), CMETA_OK);
        check_equal(CapabilityValue_init(&second), CMETA_OK);
        check_equal(CapabilityValue_init(&third), CMETA_OK);
    }
    after_each() {
        fail_copy = fail_init = false;
        CapabilityValue_destroy(&first);
        CapabilityValue_destroy(&second);
        CapabilityValue_destroy(&third);
        check_equal(cmeta_data_value_restore_zero(&capability_owned_data, &input), CMETA_OK);
        check_equal(owned_live, 0);
    }

    it("derives compile-time requirements and callback flags from one trait declaration") {
        CapabilityRecord value = {0};
        value.id = 7;
        check_equal(cmeta_traits_CapabilityRecord.flags,
                    (cmeta_trait_flags)CapabilityRecord__cmeta_traits_flags);
        check_true(cmeta_traits_CapabilityRecord.equal(&value, &value));
        check_equal(cmeta_traits_CapabilityRecord.hash(&value), (uint64_t)7u);
        check_false(cmeta_has_trait(CapabilityRecord, Copyable));
    }

    it("reflects explicit tags and compares independent TU descriptors semantically") {
        const cmeta_data_desc *data = CapabilityValue_cmeta_data();
        const cmeta_data_variant_shape *shape = data->shape;
        const cmeta_data_desc *peer = capability_variant_peer();
        check_true(cmeta_data_desc_valid(data));
        check_true(data != peer);
        check_true(cmeta_data_desc_equal(data, peer));
        check_true(cmeta_type_equal(data->storage_type, peer->storage_type));
        check_equal(shape->case_count, (size_t)3u);
        check_equal(shape->cases[1].tag, (int64_t)CapabilityValue_Owned);
        check_equal(shape->cases[1].stable_id, "test.CapabilityValue.Owned");
        check_equal(shape->cases[1].offset, offsetof(CapabilityValue, as.Owned));
        check_true(shape->cases[1].value == &capability_owned_data);
        check_true(cmeta_data_desc_equal(CapabilityFlags_cmeta_data(), capability_flags_peer()));
    }

    it("initializes poisoned raw variant storage without reading or freeing inactive payloads") {
        CapabilityValue raw;
        memset(&raw, CAPABILITY_POISON_BYTE, sizeof(raw));
        check_equal(cmeta_data_value_init_zero(CapabilityValue_cmeta_data(), &raw), CMETA_OK);
        check_equal(raw.tag, (int64_t)0);
        check_equal(owned_frees, 0);
        int number = 9;
        check_equal(CapabilityValue_copy_Number(&first, &number), CMETA_OK);
        memset(&raw, CAPABILITY_POISON_BYTE, sizeof(raw));
        check_true(cmeta_traits_CapabilityValue.copy_construct(&raw, &first));
        check_equal(*CapabilityValue_get_Number(&raw), number);
        CapabilityValue_destroy(&raw);
        check_equal(owned_frees, 0);
    }

    it("rejects an explicitly invalid raw constructor without falling through to restore") {
        CapabilityValue raw;
        cmeta_data_desc data = *CapabilityValue_cmeta_data();
        cmeta_data_construct_ops constructor = *data.construct_ops;
        constructor.abi_version = 0u;
        data.construct_ops = &constructor;
        memset(&raw, CAPABILITY_POISON_BYTE, sizeof(raw));
        CapabilityValue before;
        memcpy(&before, &raw, sizeof(raw));
        check_equal(cmeta_data_value_init_zero(&data, &raw), CMETA_INVALID_ARGUMENT);
        check_equal(&raw, &before, sizeof(raw));
        check_equal(owned_frees, 0);
    }

    it("copies and moves empty values and constructs an owned move into raw storage") {
        CapabilityValue raw;
        check_equal(CapabilityValue_copy(&second, &first), CMETA_OK);
        check_equal(CapabilityValue_move(&third, &second), CMETA_OK);
        check_equal(third.tag, (int64_t)0);
        static const unsigned char bytes[] = {6u};
        check_equal(cmeta_data_buffer_assign(&capability_owned_data, &input,
                    bytes, sizeof(bytes), CAPABILITY_PAYLOAD_LIMIT), CMETA_OK);
        check_equal(CapabilityValue_move_Owned(&first, &input), CMETA_OK);
        unsigned char *retained = CapabilityValue_mut_Owned(&first)->data;
        memset(&raw, CAPABILITY_POISON_BYTE, sizeof(raw));
        cmeta_traits_CapabilityValue.move_construct(&raw, &first);
        check_equal(first.tag, (int64_t)0);
        check_true(CapabilityValue_get_Owned(&raw)->data == retained);
        check_equal(owned_live, 1);
        CapabilityValue_destroy(&raw);
        check_equal(owned_frees, 1);
    }

    it("copies owned payloads independently and moves without allocating or duplicating ownership") {
        static const unsigned char bytes[] = {1u, 2u, 3u};
        check_equal(cmeta_data_buffer_assign(&capability_owned_data, &input,
                    bytes, sizeof(bytes), CAPABILITY_PAYLOAD_LIMIT), CMETA_OK);
        check_equal(CapabilityValue_copy_Owned(&first, &input), CMETA_OK);
        check_true(CapabilityValue_get_Owned(&first)->data != input.data);
        check_equal(CapabilityValue_get_Owned(&first)->data, input.data, sizeof(bytes));
        unsigned char *retained = CapabilityValue_mut_Owned(&first)->data;
        check_equal(CapabilityValue_move(&second, &first), CMETA_OK);
        check_equal(first.tag, (int64_t)0);
        check_null(CapabilityValue_get_Owned(&first));
        check_true(CapabilityValue_get_Owned(&second)->data == retained);
        check_equal(owned_live, 2);
        check_equal(owned_moves, 1);
        CapabilityValue_destroy(&second);
        CapabilityValue_destroy(&second);
        check_equal(owned_frees, 1);
    }

    it("restores failed payload copies to empty while preserving the source") {
        static const unsigned char bytes[] = {4u, 5u};
        check_equal(cmeta_data_buffer_assign(&capability_owned_data, &input,
                    bytes, sizeof(bytes), CAPABILITY_PAYLOAD_LIMIT), CMETA_OK);
        check_equal(CapabilityValue_move_Owned(&first, &input), CMETA_OK);
        const unsigned char *retained = CapabilityValue_get_Owned(&first)->data;
        fail_copy = true;
        check_equal(CapabilityValue_copy(&second, &first), CMETA_OUT_OF_MEMORY);
        check_equal(second.tag, (int64_t)0);
        check_true(CapabilityValue_get_Owned(&first)->data == retained);
        check_equal(owned_live, 1);
        check_equal(owned_frees, 1);
    }

    it("cleans partially initialized selected payloads exactly once") {
        fail_init = true;
        check_equal(CapabilityValue_select(&first, CapabilityValue_Owned), CMETA_OUT_OF_MEMORY);
        check_equal(first.tag, (int64_t)0);
        check_equal(owned_live, 0);
        check_equal(owned_frees, 1);
        CapabilityValue_destroy(&first);
        check_equal(owned_frees, 1);
    }

    it("rejects bad tags, aliases and nonempty destinations before mutation") {
        int number = 17;
        check_equal(CapabilityValue_select(&first, 0), CMETA_INVALID_ARGUMENT);
        check_equal(CapabilityValue_select(&first, INT_MAX), CMETA_INVALID_ARGUMENT);
        check_equal(CapabilityValue_copy_Number(&first, &number), CMETA_OK);
        check_equal(CapabilityValue_copy(&first, &first), CMETA_INVALID_ARGUMENT);
        check_equal(CapabilityValue_move(&first, &first), CMETA_INVALID_ARGUMENT);
        check_equal(CapabilityValue_copy_Number(&first, &number), CMETA_INVALID_ARGUMENT);
        check_equal(*CapabilityValue_get_Number(&first), number);
        check_null(CapabilityValue_get_Owned(&first));
        check_null(CapabilityValue_mut_Number(NULL));
        check_equal(CapabilityValue_copy_Number(&second, &second.as.Number), CMETA_INVALID_ARGUMENT);
        check_equal(CapabilityValue_move_Number(&second, &second.as.Number), CMETA_INVALID_ARGUMENT);
        first.tag = INT_MAX;
        check_equal(CapabilityValue_copy(&second, &first), CMETA_CALLBACK_ERROR);
        check_equal(second.tag, (int64_t)0);
        first.tag = CapabilityValue_Number;
    }

    it("visits through ordinary switch and exposes checked typed members") {
        int number = 19, visited = 0;
        check_equal(CapabilityValue_copy_Number(&first, &number), CMETA_OK);
        cmeta_match(&first) {
            case CapabilityValue_Number: visited = *CapabilityValue_get_Number(&first); break;
            default: break;
        }
        check_equal(visited, number);
        *CapabilityValue_mut_Number(&first) = 21;
        check_equal(*CapabilityValue_get_Number(&first), 21);
    }

    it("preserves canonical lifecycle through nested generated variants") {
        CapabilityNested nested = {0}, moved = {0};
        int number = 29;
        check_equal(CapabilityValue_copy_Number(&first, &number), CMETA_OK);
        check_equal(CapabilityNested_copy_Value(&nested, &first), CMETA_OK);
        check_equal(CapabilityNested_move(&moved, &nested), CMETA_OK);
        check_equal(nested.tag, (int64_t)0);
        check_equal(*CapabilityValue_get_Number(CapabilityNested_get_Value(&moved)), number);
        CapabilityNested_destroy(&nested);
        CapabilityNested_destroy(&moved);
    }

    it("deep-copies nested ownership and cleans nested partial failure") {
        CapabilityNested nested = {0}, copy = {0};
        static const unsigned char bytes[] = {8u, 9u};
        check_equal(cmeta_data_buffer_assign(&capability_owned_data, &input,
                    bytes, sizeof(bytes), CAPABILITY_PAYLOAD_LIMIT), CMETA_OK);
        check_equal(CapabilityValue_move_Owned(&first, &input), CMETA_OK);
        check_equal(CapabilityNested_copy_Value(&nested, &first), CMETA_OK);
        const capability_owned *retained = CapabilityValue_get_Owned(CapabilityNested_get_Value(&nested));
        check_true(retained->data != CapabilityValue_get_Owned(&first)->data);
        fail_copy = true;
        check_equal(CapabilityNested_copy(&copy, &nested), CMETA_OUT_OF_MEMORY);
        check_equal(copy.tag, (int64_t)0);
        check_equal(owned_live, 2);
        check_equal(owned_frees, 1);
        fail_copy = false;
        check_equal(CapabilityNested_copy(&copy, &nested), CMETA_OK);
        check_true(CapabilityValue_get_Owned(CapabilityNested_get_Value(&copy))->data != retained->data);
        CapabilityNested_destroy(&nested);
        CapabilityNested_destroy(&copy);
        check_equal(owned_live, 1);
    }

    it("rejects a payload descriptor whose native extent does not match the case") {
        CapabilityBadSize bad;
        memset(&bad, CAPABILITY_POISON_BYTE, sizeof(bad));
        check_equal(CapabilityBadSize_init(&bad), CMETA_TYPE_MISMATCH);
        check_equal(bad.tag, (int64_t)0);
        check_equal(CapabilityBadSize_select(&bad, CapabilityBadSize_Bad), CMETA_TYPE_MISMATCH);
    }

    it("requires canonical lifecycle support before admitting a payload provider") {
        CapabilityUnsupported value;
        check_true(cmeta_data_desc_valid(&unsupported_data));
        check_equal(CapabilityUnsupported_init(&value), CMETA_TRAIT_MISSING);
        check_equal(value.tag, (int64_t)0);
        check_equal(CapabilityUnsupported_select(&value, CapabilityUnsupported_Value), CMETA_TRAIT_MISSING);
    }

    it("supports the finite case limit and both signed tag boundaries") {
        CapabilityBoundary value = {0}, copy = {0};
        const cmeta_data_variant_shape *shape = CapabilityBoundary_cmeta_data()->shape;
        enum { CAPABILITY_CASE_LIMIT = 16 };
        int number = 31;
        check_equal(shape->case_count, (size_t)CAPABILITY_CASE_LIMIT);
        check_equal(CapabilityBoundary_copy_Min(&value, &number), CMETA_OK);
        check_equal(value.tag, (int64_t)INT_MIN);
        check_equal(CapabilityBoundary_copy(&copy, &value), CMETA_OK);
        check_equal(*CapabilityBoundary_get_Min(&copy), number);
        CapabilityBoundary_destroy(&value);
        CapabilityBoundary_destroy(&copy);
        check_equal(CapabilityBoundary_copy_Max(&value, &number), CMETA_OK);
        check_equal(value.tag, (int64_t)INT_MAX);
        CapabilityBoundary_destroy(&value);
    }

    it("keeps full-width flags, named masks and reflection in one unsigned domain") {
        CapabilityFlags flags = {0}, moved = {0}, copy = {0};
        uint64_t bits = 0u;
        check_equal(CapabilityFlags_or(CapabilityFlags_Read, CapabilityFlags_High, &flags), CMETA_OK);
        check_true(CapabilityFlags_contains(flags, CapabilityFlags_High));
        check_equal(CapabilityFlags_meta()->declared_mask, flags.bits);
        check_equal(CapabilityFlags_meta()->items[1].bits, CapabilityFlags_High.bits);
        check_equal(cmeta_data_enum_read_bits(CapabilityFlags_cmeta_data(), &flags, &bits), CMETA_OK);
        check_equal(bits, flags.bits);
        check_equal(cmeta_data_value_copy(CapabilityFlags_cmeta_data(), &copy, &flags), CMETA_OK);
        check_equal(cmeta_data_value_move(CapabilityFlags_cmeta_data(), &moved, &copy), CMETA_OK);
        check_equal(copy.bits, (uint64_t)0u);
        check_equal(moved.bits, flags.bits);
        uint64_t before = flags.bits;
        check_equal(CapabilityFlags_from_bits(UINT64_C(2), &flags), CMETA_INVALID_ARGUMENT);
        check_equal(flags.bits, before);
        check_equal(CapabilityValue_copy_Flags(&first, &flags), CMETA_OK);
        check_equal(CapabilityValue_get_Flags(&first)->bits, before);
    }
}

