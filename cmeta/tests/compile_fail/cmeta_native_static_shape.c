#include <cmeta/native/static_call.h>
FunctionDecl(value, double, native_wrong_shape, (double, value, CMETA_PARAM_IN));
cmeta_static_thunk_call(native_wrong_slot, native_wrong_shape);
int main(void) { return 0; }
