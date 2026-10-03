#include <cstl/typed.h>

typed(List, IntList, int);

static void probe(int condition) {
    owned(IntList) value;
    while (condition)
        move(value);
}

int main(void) {
    return 0;
}
