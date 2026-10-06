#include <cmeta/struct.h>
cmeta_struct(FieldOwner, cmeta_field(int, id));
cmeta_require_field(FieldOwner, absent_member, int);
int main(void) { return 0; }
