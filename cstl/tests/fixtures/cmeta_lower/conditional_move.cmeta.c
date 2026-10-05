#include <cstl/typed.h>

typed(List, IntList, int);

static void probe(int condition) {
    cmeta_cmeta_owned(IntList) value;
    IntList sink = {0};
    if (condition)
        sink = cmeta_cmeta_move(value);
    IntList_destroy(&sink);
}

int main(void) {
    return 0;
}
