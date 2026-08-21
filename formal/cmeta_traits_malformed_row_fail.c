#include <cmeta/type_traits.h>

static bool eq1(const void *a, const void *b) { return a == b; }

Traits(malformed_traits,
    (equal, eq1, unexpected)
);

int main(void) { return 0; }
