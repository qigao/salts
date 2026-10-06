#include <cmeta/type_traits.h>
static int wrong_hash(const void *value) { return value != NULL; }
cmeta_traits(WrongCallback, cmeta_trait(Hashable, wrong_hash));
int main(void) { return 0; }
