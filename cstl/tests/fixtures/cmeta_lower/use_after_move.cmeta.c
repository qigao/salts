#include <cstl/typed.h>

typed(List, IntList, int);

int main(void) {
    cmeta_owned(IntList) list = {0};
    IntList transferred = {0};

    transferred = cmeta_move(list);
    return (int)IntList_size(&list);
}
