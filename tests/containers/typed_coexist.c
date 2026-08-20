#include <cflow/meta.h>
#include <container/typed.h>

#include <assert.h>

typed(Pair, CoexistPair, int, long);
typed(List, CoexistList, int);

typed(map, value, long, coexist_square, (int x)) {
    return (long)x * x;
}

int main(void) {
    CoexistPair p = PairMake(CoexistPair, 3, 9L);
    CoexistList list;
    assert(p.second == 9L);
    assert(CoexistList_init(&list) == TURBO_OK);
    assert(CoexistList_push_back(&list, 4) == TURBO_OK);
    assert(*CoexistList_front(&list) == 4);
    CoexistList_destroy(&list);
    return coexist_square.fn.meta.effects == CMETA_EFFECT_PURE ? 0 : 1;
}
