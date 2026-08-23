#include <cmeta/compute.h>

enum {
    cmeta_missing_type_size = sizeof(TypeEval1(CMetaMissingType, absent))
};

int main(void) {
    return cmeta_missing_type_size;
}
