#include <cstl/typed.h>

typed(List, IntList, int);

static void probe(void) {
    for (;;) {
        cmeta_owned(IntList) value;
        break;
    }
}

int main(void) {
    return 0;
}
