#include <cstl/typed.h>

typed(List, IntList, int);

static void probe(int condition) {
    cmeta_owned(IntList) value;
    while (condition)
        cmeta_move(value);
}

int main(void) {
    return 0;
}
