#include <cmeta/type_traits.h>
static uint64_t hash(const void *value) { return value != NULL; }
cmeta_traits(MissingTrait, cmeta_trait(Hashable, hash));
cmeta_require_trait(MissingTrait, Movable);
int main(void) { return 0; }
