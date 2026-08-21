#include <cmeta/pp.h>

#define CONSUME(E, method, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect) \
    enum { consumed_##method = (E) + (margc) + (fnarg) + (childarg) + (farity) + (p0) + (p1) + (p2) + (ret) + (out) + (card) + (childrule) + (semantic) + (effect) };

#define InvalidOps(M) \
    Operators(M, \
        (10, sample, \
            (call, 1, 2, 3), \
            (fn, 4, 5, 6, 7, 8, 99), \
            (flow, 9, 10, 11), \
            (semantic, 12), \
            (effect, 13)))

Replay(InvalidOps, CONSUME)

int main(void) { return 0; }
