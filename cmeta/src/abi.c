#include <cmeta/abi.h>
#include <cmeta/cmeta.h>

uint32_t cmeta_reflection_abi_version(void) {
    return CMETA_REFLECTION_ABI_VERSION;
}

bool cmeta_abi_carrier_valid(cmeta_abi_carrier carrier) {
    return carrier >= CMETA_ABI_UNSPECIFIED &&
           carrier <= CMETA_ABI_ENUM;
}

const char *cmeta_abi_carrier_name(cmeta_abi_carrier carrier) {
    switch (carrier) {
    case CMETA_ABI_UNSPECIFIED: return "unspecified";
    case CMETA_ABI_VOID: return "void";
    case CMETA_ABI_SCALAR: return "scalar";
    case CMETA_ABI_OBJECT_POINTER: return "object_pointer";
    case CMETA_ABI_AGGREGATE: return "aggregate";
    case CMETA_ABI_FUNCTION_POINTER: return "function_pointer";
    case CMETA_ABI_OPAQUE: return "opaque";
    case CMETA_ABI_ENUM: return "enum";
    default: return "invalid";
    }
}

bool cmeta_abi_carrier_matches_type(cmeta_abi_carrier carrier,
                                    const cmeta_type_desc *type) {
    if (!cmeta_abi_carrier_valid(carrier) ||
        !cmeta_type_desc_valid(type))
        return false;

    switch (carrier) {
    case CMETA_ABI_UNSPECIFIED:
        return true;
    case CMETA_ABI_VOID:
        return type->kind == CMETA_T_VOID;
    case CMETA_ABI_SCALAR:
        return type->kind == CMETA_T_BOOL ||
               type->kind == CMETA_T_INTEGER ||
               type->kind == CMETA_T_FLOAT;
    case CMETA_ABI_OBJECT_POINTER:
        return type->kind == CMETA_T_POINTER;
    case CMETA_ABI_AGGREGATE:
        return type->kind == CMETA_T_OBJECT;
    case CMETA_ABI_FUNCTION_POINTER:
        /*
         * Function pointers do not have a dedicated cmeta_type_kind yet.
         * The explicit carrier is the semantic distinction; do not infer it
         * from descriptor names or object-pointer layout.
         */
        return type->kind != CMETA_T_VOID;
    case CMETA_ABI_OPAQUE:
        return type->kind != CMETA_T_VOID;
    case CMETA_ABI_ENUM:
        return type->kind == CMETA_T_INTEGER;
    default:
        return false;
    }
}
