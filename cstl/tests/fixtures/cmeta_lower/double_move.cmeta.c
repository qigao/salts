#include <cstl/typed.h>

typed(List, IntList, int);

int main(void) {
    cmeta_owned(IntList) list = {0};
    IntList first = {0};
    IntList second = {0};

    first = cmeta_move(list);
    second = cmeta_move(list);
    return (int)(IntList_size(&first) + IntList_size(&second));
}
