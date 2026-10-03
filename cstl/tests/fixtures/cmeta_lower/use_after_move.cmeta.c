#include <cstl/typed.h>

typed(List, IntList, int);

int main(void) {
    owned(IntList) list = {0};
    IntList transferred = {0};

    transferred = move(list);
    return (int)IntList_size(&list);
}
