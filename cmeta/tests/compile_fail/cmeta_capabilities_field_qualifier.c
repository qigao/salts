#include <cmeta/struct.h>
typedef struct FieldOwner { const int id; } FieldOwner;
cmeta_require_field(FieldOwner, id, int);
int main(void) { return 0; }
