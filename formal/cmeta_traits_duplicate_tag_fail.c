#include <cmeta/type_traits.h>

static bool eq1(const void *a, const void *b) { return a == b; }
static bool eq2(const void *a, const void *b) { return a == b; }

Traits(duplicate_traits,
    (equal, eq1),
    (equal, eq2)
);

int main(void) { return 0; }
