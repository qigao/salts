#include <cstl/typed.h>

typed(List, IntList, int);

static void probe(void) {
    owned(IntList) value;
    goto done;
done:
    (void)0;
}

int main(void) {
    return 0;
}
