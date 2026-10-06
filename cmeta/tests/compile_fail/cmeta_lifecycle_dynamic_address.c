#include <cmeta/lifecycle.h>

typedef int ScopeValue;
static const cmeta_data_construct_ops *foreign_ops;
CMETA_DEFINE_STATIC_LIFECYCLE(ScopeValue, *foreign_ops)

int main(void) { return 0; }
