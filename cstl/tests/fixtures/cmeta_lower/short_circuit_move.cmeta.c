#include <cstl/typed.h>

typed(List, IntList, int);

static int consume(IntList value) {
    IntList_destroy(&value);
    return 1;
}

static void probe(int condition) {
    owned(IntList) value;
    (void)(condition && consume(move(value)));
}

int main(void) {
    return 0;
}
