#include <cmeta/lifecycle.h>

typedef struct ScopeValue { int value; } ScopeValue;
static cmeta_data_construct_ops mutable_ops;
CMETA_DEFINE_STATIC_LIFECYCLE(ScopeValue, mutable_ops)

int main(void) { return 0; }
