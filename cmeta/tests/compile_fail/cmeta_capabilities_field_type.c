#include <cmeta/struct.h>
cmeta_struct(FieldOwner, cmeta_field(int, id));
cmeta_require_field(FieldOwner, id, double);
int main(void) { return 0; }
