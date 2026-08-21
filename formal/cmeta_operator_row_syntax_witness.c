#include <cmeta/pp.h>

#include <assert.h>

#define CAPTURE_STRUCT(E, method, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect) \
    static const int structured[] = { E, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect };
#define CAPTURE_FLAT(E, method, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect) \
    static const int flat[] = { E, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect };

#define StructuredOps(M) \
    Operators(M, \
        (10, sample, \
            (call, 1, 2, 3), \
            (fn, 4, 5, 6, 7, 8), \
            (flow, 9, 10, 11), \
            (semantic, 12), \
            (effect, 13)))

#define FlatOps(M) \
    Operators(M, \
        (10, sample, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13))

Replay(StructuredOps, CAPTURE_STRUCT)
Replay(FlatOps, CAPTURE_FLAT)

int main(void) {
    unsigned i;
    assert(sizeof(structured) == sizeof(flat));
    for (i = 0; i < sizeof(flat) / sizeof(flat[0]); ++i)
        assert(structured[i] == flat[i]);
    return 0;
}
