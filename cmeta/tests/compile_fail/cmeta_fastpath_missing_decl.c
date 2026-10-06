#include <cmeta/fastpath.h>
int undeclared_target(int value);
cmeta_static_call(slot, undeclared_target);
int main(void) { return cmeta_static_invoke(slot, 1); }
