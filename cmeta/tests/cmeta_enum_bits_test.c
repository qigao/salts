#include <cmeta/data.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* The provider stores canonical bits, independently of the declared width. */
typedef struct enum_value { uint64_t bits; bool engaged; } enum_value;
static unsigned assign_calls;
static bool assign_fails, read_fails, wrong_read, restore_fails;
static bool value_is_zero(const void *object) {
    return !((const enum_value *)object)->engaged;
}
static cmeta_status value_read(const void *object, uint64_t *out) {
    *out = ((const enum_value *)object)->bits + (wrong_read ? 1u : 0u);
    return read_fails ? CMETA_OUT_OF_MEMORY : CMETA_OK;
}
static cmeta_status value_assign(void *object, uint64_t bits) {
    enum_value *value = object;
    ++assign_calls;
    value->bits = bits;
    value->engaged = true;
    return assign_fails ? CMETA_OUT_OF_MEMORY : CMETA_OK;
}
static void value_restore(void *object) {
    if (!restore_fails) memset(object, 0, sizeof(enum_value));
}
static const cmeta_type_identity identity = CMETA_TYPE_ID_ATOM_INIT("test.EnumBits");
static const cmeta_type_desc storage = {
    "enum_value", sizeof(enum_value), _Alignof(enum_value), CMETA_T_OBJECT,
    NULL, NULL, &identity
};
static const cmeta_enum_bits_item items[] = {
    {0u, "ZERO", "zero"}, {1u, "ONE", "one"},
    {128u, "MIN8", "min8"}, {255u, "MAX8", "max8"},
    {UINT64_C(0x8000000000000000), "MIN64", "min64"},
    {UINT64_MAX, "MAX64", "max64"}
};
static cmeta_enum_domain domain = {
    sizeof(cmeta_enum_domain), CMETA_ENUM_DOMAIN_ABI_VERSION,
    CMETA_ENUM_UNSIGNED, 64u, CMETA_ENUM_ORDINARY, items, 6u, 0u
};
static cmeta_data_enum_bits_ops ops = {
    sizeof(cmeta_data_enum_bits_ops), CMETA_DATA_ENUM_BITS_OPS_ABI_VERSION,
    &storage, &domain, value_is_zero, value_read, value_assign, value_restore
};
static cmeta_data_desc desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.EnumBits.data", .display_name = "EnumBits",
    .kind = CMETA_DATA_ENUM, .storage_type = &storage, .enum_bits_ops = &ops
};

static void roundtrip(uint64_t bits) {
    enum_value value = {0};
    uint64_t out = 42u;
    bool zero = false;
    assert(cmeta_data_desc_valid(&desc));
    assert(cmeta_data_enum_bits_ops_of(&desc) == &ops);
    assert(cmeta_data_enum_bits_is_zero(&desc, &value, &zero) == CMETA_OK && zero);
    assert(cmeta_data_enum_assign_bits(&desc, &value, bits) == CMETA_OK);
    assert(value.engaged && value.bits == bits);
    assert(cmeta_data_enum_read_bits(&desc, &value, &out) == CMETA_OK && out == bits);
    assert(cmeta_data_enum_bits_restore_zero(&desc, &value) == CMETA_OK);
    assert(!value.engaged && value.bits == 0u);
}

static void rejects_without_mutation(uint64_t bits, cmeta_status expected) {
    enum_value value = {0}, before = value;
    unsigned calls = assign_calls;
    assert(cmeta_data_enum_assign_bits(&desc, &value, bits) == expected);
    assert(memcmp(&value, &before, sizeof(value)) == 0 && calls == assign_calls);
}

static void domains_and_membership(void) {
    enum_value unknown = {2u, true};
    uint64_t out = 42u;
    assert(cmeta_data_enum_read_bits(&desc, &unknown, &out) == CMETA_CALLBACK_ERROR);
    assert(out == 42u);
    domain.signedness = CMETA_ENUM_SIGNED;
    domain.bits = 8u; domain.count = 4u;
    roundtrip(128u); roundtrip(255u); roundtrip(0u);
    rejects_without_mutation(256u, CMETA_INVALID_ARGUMENT);
    rejects_without_mutation(UINT64_MAX, CMETA_INVALID_ARGUMENT);
    domain.signedness = CMETA_ENUM_UNSIGNED;
    roundtrip(255u);
    rejects_without_mutation(2u, CMETA_INVALID_ARGUMENT);
    domain.bits = 64u; domain.count = 6u;
    roundtrip(UINT64_MAX);
    domain.signedness = CMETA_ENUM_SIGNED;
    roundtrip(UINT64_C(0x8000000000000000)); roundtrip(UINT64_MAX);
    domain.signedness = CMETA_ENUM_UNSIGNED;
}

static void flags_and_atomicity(void) {
    static const cmeta_enum_bits_item flags[] = {
        {1u, "READ", "read"}, {4u, "WRITE", "write"}
    };
    enum_value value = {2u, true}, before = value;
    uint64_t out = 42u;
    domain.kind = CMETA_ENUM_FLAGS; domain.bits = 8u;
    domain.items = flags; domain.count = 2u; domain.declared_mask = 5u;
    roundtrip(0u); roundtrip(5u);
    rejects_without_mutation(2u, CMETA_INVALID_ARGUMENT);
    rejects_without_mutation(256u, CMETA_INVALID_ARGUMENT);
    assert(cmeta_data_enum_read_bits(&desc, &value, &out) == CMETA_CALLBACK_ERROR);
    assert(out == 42u && memcmp(&value, &before, sizeof(value)) == 0);
    assert(cmeta_data_enum_assign_bits(&desc, &value, 1u) == CMETA_INVALID_ARGUMENT);
    assert(memcmp(&value, &before, sizeof(value)) == 0);
    value_restore(&value);
    assign_fails = true;
    assert(cmeta_data_enum_assign_bits(&desc, &value, 1u) == CMETA_CALLBACK_ERROR);
    assert(value_is_zero(&value) && value.bits == 0u);
    assign_fails = false; read_fails = true;
    assert(cmeta_data_enum_read_bits(&desc, &value, &out) == CMETA_CALLBACK_ERROR);
    assert(out == 42u);
    assert(cmeta_data_enum_assign_bits(&desc, &value, 1u) == CMETA_CALLBACK_ERROR);
    assert(value_is_zero(&value));
    read_fails = false; wrong_read = true;
    assert(cmeta_data_enum_assign_bits(&desc, &value, 1u) == CMETA_CALLBACK_ERROR);
    assert(value_is_zero(&value));
    wrong_read = false; restore_fails = true; value.engaged = true;
    assert(cmeta_data_enum_bits_restore_zero(&desc, &value) == CMETA_CALLBACK_ERROR);
    restore_fails = false;
    domain.items = items; domain.count = 6u; domain.bits = 64u;
    domain.kind = CMETA_ENUM_ORDINARY; domain.declared_mask = 0u;
}

static void malformed_descriptors(void) {
    cmeta_data_desc saved_desc = desc;
    cmeta_data_enum_bits_ops saved_ops = ops;
    cmeta_enum_domain saved_domain = domain;
    uint64_t out = 42u;
    enum_value value = {0};
#define REJECT() do { \
    rejects_without_mutation(1u, CMETA_INVALID_ARGUMENT); \
    assert(cmeta_data_enum_bits_ops_of(&desc) == NULL); \
    assert(cmeta_data_enum_read_bits(&desc, &value, &out) == CMETA_INVALID_ARGUMENT); \
    assert(out == 42u); \
} while (0)
    desc.struct_size = offsetof(cmeta_data_desc, enum_bits_ops); REJECT(); desc = saved_desc;
    desc.abi_version++; REJECT(); desc = saved_desc;
    desc.enum_bits_ops = NULL; REJECT(); desc = saved_desc;
    desc.shape = &domain; REJECT(); desc = saved_desc;
    desc.enum_ops = (const cmeta_data_enum_ops *)&ops; REJECT(); desc = saved_desc;
    ops.struct_size = offsetof(cmeta_data_enum_bits_ops, restore_zero); REJECT(); ops = saved_ops;
    ops.abi_version++; REJECT(); ops = saved_ops;
    ops.domain = NULL; REJECT(); ops = saved_ops;
    ops.is_zero = NULL; REJECT(); ops = saved_ops;
    ops.read = NULL; REJECT(); ops = saved_ops;
    ops.assign = NULL; REJECT(); ops = saved_ops;
    ops.restore_zero = NULL; REJECT(); ops = saved_ops;
    domain.struct_size = offsetof(cmeta_enum_domain, declared_mask); REJECT(); domain = saved_domain;
    domain.abi_version++; REJECT(); domain = saved_domain;
    domain.bits = 7u; REJECT(); domain = saved_domain;
    domain.signedness = (cmeta_enum_signedness)99; REJECT(); domain = saved_domain;
    domain.kind = (cmeta_enum_domain_kind)99; REJECT(); domain = saved_domain;
    domain.items = NULL; REJECT(); domain = saved_domain;
    {
        const cmeta_enum_bits_item invalid_item = {1u, NULL, "one"};
        domain.items = &invalid_item; domain.count = 1u; REJECT(); domain = saved_domain;
    }
    domain.bits = 8u; REJECT(); domain = saved_domain;
    domain.declared_mask = 1u; REJECT(); domain = saved_domain;
    domain.kind = CMETA_ENUM_FLAGS; domain.declared_mask = 1u; REJECT(); domain = saved_domain;
    domain.count = 0u; rejects_without_mutation(1u, CMETA_INVALID_ARGUMENT); domain = saved_domain;
    ops.storage_type = &cmeta_type_int;
    rejects_without_mutation(1u, CMETA_TYPE_MISMATCH); ops = saved_ops;
    assert(cmeta_data_enum_read_bits(&desc, NULL, &out) == CMETA_INVALID_ARGUMENT);
    assert(cmeta_data_enum_read_bits(&desc, &value, NULL) == CMETA_INVALID_ARGUMENT);
    assert(cmeta_data_enum_assign_bits(&desc, NULL, 1u) == CMETA_INVALID_ARGUMENT);
    assert(cmeta_data_enum_bits_is_zero(&desc, NULL, NULL) == CMETA_INVALID_ARGUMENT);
    assert(cmeta_data_enum_bits_restore_zero(&desc, NULL) == CMETA_INVALID_ARGUMENT);
#undef REJECT
}

static void legacy_prefix_stays_legacy(void) {
    static const cmeta_enum_item_desc old_items[] = {{1, "ONE", "one"}};
    static const cmeta_enum_desc old_meta = {"Legacy", old_items, 1u};
    static const cmeta_data_enum_shape old_shape = {&old_meta};
    cmeta_data_desc legacy = desc;
    void *old_allocation;
    legacy.struct_size = offsetof(cmeta_data_desc, enum_bits_ops);
    legacy.shape = &old_shape;
    /* Out-of-prefix pointer must not be inspected. */
    legacy.enum_bits_ops = &ops;
    assert(cmeta_data_desc_valid(&legacy));
    assert(cmeta_data_enum_bits_ops_of(&legacy) == NULL);
    old_allocation = malloc(legacy.struct_size);
    assert(old_allocation != NULL);
    memcpy(old_allocation, &legacy, legacy.struct_size);
    assert(cmeta_data_desc_valid(old_allocation));
    assert(cmeta_data_enum_bits_ops_of(old_allocation) == NULL);
    free(old_allocation);
    legacy.struct_size = sizeof(legacy);
    assert(!cmeta_data_desc_valid(&legacy));
}

int main(void) {
    domains_and_membership(); flags_and_atomicity();
    malformed_descriptors(); legacy_prefix_stays_legacy();
    return 0;
}
