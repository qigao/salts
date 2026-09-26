#include <cstl/typed.h>

typed(Vec, IntVec, int);
typed(List, IntList, int);

int main(void) {
    IntList list = {0};
    return Vec_add(&list, 10);
}
