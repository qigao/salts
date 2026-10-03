#include <cstl/typed.h>

typed(List, IntList, int);

int main(void) {
    owned(IntList) list = {0};
    return 0;
}
