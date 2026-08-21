#ifndef CMETA_FORMAL_TYPE_IDENTITY_TU_SHARED_H
#define CMETA_FORMAL_TYPE_IDENTITY_TU_SHARED_H

#include <cmeta/cmeta.h>

const cmeta_type_desc *cmeta_tu_a_desc(void);
const cmeta_type_identity *cmeta_tu_a_identity(void);
const cmeta_type_desc *cmeta_tu_a_ptr_desc(void);
const cmeta_type_identity *cmeta_tu_a_ptr_identity(void);

const cmeta_type_desc *cmeta_tu_b_desc(void);
const cmeta_type_identity *cmeta_tu_b_identity(void);
const cmeta_type_desc *cmeta_tu_b_ptr_desc(void);
const cmeta_type_identity *cmeta_tu_b_ptr_identity(void);
const cmeta_type_desc *cmeta_tu_b_different_same_layout_desc(void);
const cmeta_type_desc *cmeta_tu_b_legacy_same_layout_desc(void);

#endif
