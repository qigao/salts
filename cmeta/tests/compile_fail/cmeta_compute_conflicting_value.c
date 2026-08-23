#include <cmeta/compute.h>

ValueFunction1(CMetaConflictingValue,
    (same, 1),
    (same, 2)
);

int main(void) {
    return ValueEval1(CMetaConflictingValue, same);
}
