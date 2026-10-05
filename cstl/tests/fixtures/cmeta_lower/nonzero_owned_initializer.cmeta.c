#include <cstl/typed.h>

typed(List, IntList, int);

static void probe(void) {
    cmeta_owned(IntList) value = {1};
}

int main(void) {
    return 0;
}
