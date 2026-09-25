#include "../cmeta_function_admission_fixture.h"

#if defined(CMETA_TEST_POINTER_PARAM)
FunctionDecl(value, int, invalid_pointer_parameter,
    (cmeta_admission_pointer, input, CMETA_PARAM_IN));
#elif defined(CMETA_TEST_OBJECT_PARAM)
FunctionDecl(value, int, invalid_object_parameter,
    (cmeta_admission_box, input, CMETA_PARAM_IN));
#elif defined(CMETA_TEST_POINTER_RETURN)
FunctionDecl(value, cmeta_admission_pointer, invalid_pointer_return,
    (int, input, CMETA_PARAM_IN));
#elif defined(CMETA_TEST_OBJECT_RETURN)
Function0Decl(value, cmeta_admission_box, invalid_object_return);
#elif defined(CMETA_TEST_VOID_ALIAS_RETURN)
typedef void cmeta_admission_void;
Function0Decl(stateful, cmeta_admission_void, invalid_void_alias_return);
#else
#error "select a function admission regression"
#endif

int main(void) { return 0; }
