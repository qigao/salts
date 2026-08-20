#include <cmeta/meta.h>
#include "demo_cmeta_standalone_schema.h"

#include <stdio.h>
#include <string.h>

#define COUNTER_METHODS(X, I) \
    X(I, R1, int, add, int, delta) \
    X(I, R0, int, value, _) \
    X(I, V0, void, reset, _)

interface(counter, COUNTER_METHODS);

static int simple_counter_add(void *self, int delta) {
    int *v = (int *)self;
    *v += delta;
    return *v;
}
static int simple_counter_value(void *self) { return *(int *)self; }
static void simple_counter_reset(void *self) { *(int *)self = 0; }

implements(counter, simple_counter, 1u,
    .add = simple_counter_add,
    .value = simple_counter_value,
    .reset = simple_counter_reset
);

interface_impl(counter, COUNTER_METHODS)

int main(void) {
    color c;
    int value = 2;
    counter obj = simple_counter_as_counter(&value);
    if (!color_from_string("green", &c) || c != COLOR_GREEN) return 1;
    if (strcmp(color_to_string(c), "green") != 0) return 2;
    if (counter_add(&obj, 3) != 5 || counter_value(&obj) != 5) return 3;
    counter_reset(&obj);
    if (counter_value(&obj) != 0) return 4;
    if (cmeta_type_find("int") != &cmeta_type_int) return 5;
    if (FieldCount(point) != 2u || !FieldFind(point, "y")) return 6;
    printf("standalone CMeta: Enum + Struct + interface + type registry PASS\n");
    return 0;
}
