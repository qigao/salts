#include "type_identity_tu_shared.h"

static const cmeta_type_identity shared_id =
    CMETA_TYPE_ID_ATOM_INIT("app.Shared");
static const cmeta_type_identity shared_ptr_id =
    CMETA_TYPE_ID_POINTER_INIT(&shared_id);
static const cmeta_type_identity different_id =
    CMETA_TYPE_ID_ATOM_INIT("app.Different");

static const cmeta_type_desc shared_desc = {
    .name = "SharedFromTU_B",
    .size = 16u,
    .align = 8u,
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .identity = &shared_id
};

static const cmeta_type_desc shared_ptr_desc = {
    .name = "SharedFromTU_B *",
    .size = sizeof(void *),
    .align = _Alignof(void *),
    .kind = CMETA_T_POINTER,
    .pointee = &shared_desc,
    .identity = &shared_ptr_id
};

static const cmeta_type_desc different_same_layout_desc = {
    .name = "SharedFromTU_A",
    .size = 16u,
    .align = 8u,
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .identity = &different_id
};

static const cmeta_type_desc legacy_same_layout_desc = {
    .name = "SharedFromTU_A",
    .size = 16u,
    .align = 8u,
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .identity = NULL
};

const cmeta_type_desc *cmeta_tu_b_desc(void) { return &shared_desc; }
const cmeta_type_identity *cmeta_tu_b_identity(void) { return &shared_id; }
const cmeta_type_desc *cmeta_tu_b_ptr_desc(void) { return &shared_ptr_desc; }
const cmeta_type_identity *cmeta_tu_b_ptr_identity(void) { return &shared_ptr_id; }
const cmeta_type_desc *cmeta_tu_b_different_same_layout_desc(void) {
    return &different_same_layout_desc;
}
const cmeta_type_desc *cmeta_tu_b_legacy_same_layout_desc(void) {
    return &legacy_same_layout_desc;
}
