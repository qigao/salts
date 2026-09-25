#include "cmeta_function_admission_fixture.h"
#include "tinytest.h"

typedef int cmeta_admission_scalar;
FunctionDecl(value, cmeta_admission_scalar, admission_scalar,
    (cmeta_admission_scalar, input, CMETA_PARAM_IN));
FunctionDeclAsAbi(value, cmeta_admission_pointer, &cmeta_type_int_ptr,
    CMETA_ABI_OBJECT_POINTER, admission_pointer,
    (cmeta_admission_pointer, input, CMETA_PARAM_IN,
     &cmeta_type_int_ptr, CMETA_ABI_OBJECT_POINTER));
FunctionDeclAsAbi(value, cmeta_admission_box, &cmeta_admission_box_type,
    CMETA_ABI_AGGREGATE, admission_box,
    (cmeta_admission_box, input, CMETA_PARAM_IN,
     &cmeta_admission_box_type, CMETA_ABI_AGGREGATE));
Function0Decl(stateful, void, admission_void);

suite("CMeta function declaration admission") {
    it("preserves scalar typedef and void inference") {
        check_true(cmeta_function_abi_desc_valid(FunctionAbi(admission_scalar)));
        check_equal(FunctionAbi(admission_scalar)->return_carrier,
                    (cmeta_abi_carrier)CMETA_ABI_SCALAR);
        check_true(cmeta_function_abi_desc_valid(FunctionAbi(admission_void)));
        check_equal(FunctionAbi(admission_void)->return_carrier,
                    (cmeta_abi_carrier)CMETA_ABI_VOID);
    }

    it("admits registered objects and pointers through explicit carriers") {
        check_true(cmeta_function_abi_desc_valid(FunctionAbi(admission_pointer)));
        check_true(cmeta_function_abi_desc_valid(FunctionAbi(admission_box)));
        check_equal(cmeta_function_param_abi(FunctionAbi(admission_pointer), 0u),
                    (cmeta_abi_carrier)CMETA_ABI_OBJECT_POINTER);
        check_equal(cmeta_function_param_abi(FunctionAbi(admission_box), 0u),
                    (cmeta_abi_carrier)CMETA_ABI_AGGREGATE);
    }
}
