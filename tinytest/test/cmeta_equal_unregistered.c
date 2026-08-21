typedef struct Unregistered {
    int value;
} Unregistered;

#include "tinytest.h"

int main(void) {
    check_equal((Unregistered){1}, (Unregistered){1});
    return 0;
}
