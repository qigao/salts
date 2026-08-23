#include <cmeta/compute.h>

ValueFunction(CMetaConflictingValue,
    (same, 1),
    (same, 2)
);

int main(void) {
    return ValueEval(CMetaConflictingValue, same);
}
