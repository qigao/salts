#ifndef CMETA_INSPECTION_METADATA_H
#define CMETA_INSPECTION_METADATA_H

#include "cmeta_inspection_fixture.h"

#include <cmeta/data.h>
#include <cmeta/declared_type.h>
#include <cmeta/function.h>

#ifdef __cplusplus
extern "C" {
#endif

const cmeta_data_desc *cmeta_inspection_record_data(void);
const cmeta_declared_type *cmeta_inspection_declared_type(void);
const cmeta_function_desc *cmeta_inspection_function(void);
const cmeta_function_abi_desc *cmeta_inspection_function_abi(void);

#ifdef __cplusplus
}
#endif

#endif /* CMETA_INSPECTION_METADATA_H */
