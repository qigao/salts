#include <cstl/typed.h>

typed(List, IntList, int);

static void observe(const IntList *value) {
    (void)value;
}

int main(void) {
    owned(IntList) value;
    defer observe(&value);
}
