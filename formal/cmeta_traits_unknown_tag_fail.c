#include <cmeta/type_traits.h>

static void serialize_value(void) {}

Traits(unknown_traits,
    (serialize, serialize_value)
);

int main(void) { return 0; }
