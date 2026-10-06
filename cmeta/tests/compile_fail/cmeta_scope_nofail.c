#include <cmeta/scope.h>
typedef int Fallible;
enum { Fallible_cmeta_lifecycle_flags = 0 };
static const cmeta_data_construct_ops *Fallible_cmeta_lifecycle(const Fallible *value) {
    (void)value;
    return cmeta_data_int.construct_ops;
}
int main(void) {
    cmeta_status status;
    cmeta_scope_nofail(status, cmeta_autos((Fallible, value)), cmeta_body(CMETA_OK));
    return (int)status;
}
