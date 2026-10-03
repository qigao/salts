#include <cstl/typed.h>

int main(void) {
    owned(MissingResource) value = {0};
    (void)value;
    return 0;
}
