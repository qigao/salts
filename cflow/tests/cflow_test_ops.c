#include "cflow_test_ops.h"

typed(filter, value, bool, cflow_test_even, (int value)) {
    return value % 2 == 0;
}

typed(map, value, long, cflow_test_square, (int value)) {
    return (long)value * (long)value;
}

typed(map, value, double, cflow_test_half, (long value)) {
    return (double)value / 2.0;
}
