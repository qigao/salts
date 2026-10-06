#include "cmeta_capabilities_fixture.h"
#include <stdlib.h>
#include <string.h>

int owned_live;
int owned_frees;
int owned_moves;
bool fail_copy;
bool fail_init;

static bool owned_zero(const void *object) {
    const capability_owned *value = object;
    return value->data == NULL && value->size == 0u;
}
static void owned_restore(void *object) {
    capability_owned *value = object;
    if (value->data != NULL) {
        free(value->data);
        --owned_live;
        ++owned_frees;
    }
    value->data = NULL;
    value->size = 0u;
}
static cmeta_status owned_init(void *object) {
    capability_owned *value = object;
    value->data = NULL;
    value->size = 0u;
    if (!fail_init) return CMETA_OK;
    value->data = malloc(1u);
    if (value->data != NULL) { ++owned_live; value->size = 1u; }
    return CMETA_OUT_OF_MEMORY;
}
static cmeta_status owned_assign(void *object, const unsigned char *data,
                                  size_t size, size_t max_bytes) {
    capability_owned *value = object;
    if (size > CAPABILITY_PAYLOAD_LIMIT || size > max_bytes)
        return CMETA_CAPACITY_EXCEEDED;
    if (size != 0u) {
        value->data = malloc(size);
        if (value->data == NULL) return CMETA_OUT_OF_MEMORY;
        ++owned_live;
        memcpy(value->data, data, size);
    }
    value->size = size;
    return fail_copy ? CMETA_OUT_OF_MEMORY : CMETA_OK;
}
static cmeta_status owned_read(const void *object, const unsigned char **data,
                                size_t *size) {
    const capability_owned *value = object;
    *data = value->data;
    *size = value->size;
    return CMETA_OK;
}
static void owned_move(void *destination, void *source) {
    capability_owned *out = destination;
    capability_owned *value = source;
    *out = *value;
    value->data = NULL;
    value->size = 0u;
    ++owned_moves;
}
static const cmeta_type_identity owned_identity = CMETA_TYPE_ID_ATOM_INIT("test.Owned");
static const cmeta_type_desc owned_type = {
    "capability_owned", sizeof(capability_owned), _Alignof(capability_owned),
    CMETA_T_OBJECT, NULL, NULL, &owned_identity
};
static const cmeta_data_buffer_shape owned_shape = {CMETA_DATA_BUFFER_OWNED};
static const cmeta_data_buffer_ops owned_ops = {
    sizeof(cmeta_data_buffer_ops), CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    &owned_type, CMETA_DATA_BUFFER_OWNED, owned_zero, owned_assign,
    owned_restore, owned_read, owned_init, owned_move
};
const cmeta_data_desc capability_owned_data = {
    .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.Owned.data", .display_name = "Owned",
    .kind = CMETA_DATA_BYTES, .storage_type = &owned_type,
    .shape = &owned_shape, .buffer_ops = &owned_ops
};


