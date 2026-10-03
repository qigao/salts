#include <cstl/typed.h>

typed(List, IntList, int);

static int probe(void) {
    owned(IntList) value;
    (void)IntList_init(&value, 2u);
    return 1;
}

int main(void) {
    return 0;
}
