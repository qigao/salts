#include <cmeta/scope.h>

typedef struct ScopeValue { int value; } ScopeValue;

/* A provider cannot authorize an unrelated native type through its name. */
static const cmeta_data_construct_ops *ScopeValue_cmeta_lifecycle(const int *value) {
    (void)value;
    return cmeta_data_int.construct_ops;
}

int main(void) {
    cmeta_status status;
    cmeta_scope(status, cmeta_autos((ScopeValue, value)), cmeta_body(CMETA_OK));
    return status;
}
