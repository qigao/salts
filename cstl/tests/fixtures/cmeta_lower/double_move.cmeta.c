#include <cstl/typed.h>

typed(List, IntList, int);

int main(void) {
    owned(IntList) list = {0};
    IntList first = {0};
    IntList second = {0};

    first = move(list);
    second = move(list);
    return (int)(IntList_size(&first) + IntList_size(&second));
}
