#include <cmeta/meta.h>

typedef struct IntrusiveNode {
    int marker;
} IntrusiveNode;

typedef struct IntrusiveOwner {
    int wrong_member;
} IntrusiveOwner;

cmeta_intrusive(IntrusiveOwner, wrong_member, IntrusiveNode);

int main(void) {
    return 0;
}
