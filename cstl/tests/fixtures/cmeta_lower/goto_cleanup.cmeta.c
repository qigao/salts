#include <cstl/typed.h>

typed(List, IntList, int);

static void probe(void) {
    cmeta_owned(IntList) value;
    goto done;
done:
    (void)0;
}

int main(void) {
    return 0;
}
