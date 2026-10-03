#include <cstl/typed.h>

typed(List, IntList, int);

static void probe(int condition) {
    owned(IntList) value;
    IntList sink = {0};
    if (condition)
        sink = move(value);
    IntList_destroy(&sink);
}

int main(void) {
    return 0;
}
