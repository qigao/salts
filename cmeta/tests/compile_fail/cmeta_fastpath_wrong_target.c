#include <cmeta/fastpath.h>
FunctionDecl(value, int, right_target, (int, value, CMETA_PARAM_IN));
FunctionDecl(value, double, wrong_target, (double, value, CMETA_PARAM_IN));
cmeta_static_call(slot, right_target);
int main(void) { return cmeta_static_update(slot, wrong_target); }
