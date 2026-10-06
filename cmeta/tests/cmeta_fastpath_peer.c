#include "cmeta_fastpath_fixture.h"

enum { FASTPATH_ADDEND = 3, FASTPATH_MULTIPLIER = 2, FASTPATH_ANSWER = 42 };
cmeta_static_key(fastpath_shared_key, false);
int fastpath_void_sink;
int fastpath_target_payload;

int fastpath_add(int value) { return value + FASTPATH_ADDEND; }
int fastpath_other(int value) { return value * FASTPATH_MULTIPLIER; }
int fastpath_effectful(int value) { return fastpath_add(value); }
double fastpath_float(float left, double right) { return left + right; }
fastpath_pair fastpath_aggregate(fastpath_pair pair, double value) {
    pair.value += value;
    ++pair.count;
    return pair;
}
fastpath_callback fastpath_choose(fastpath_callback callback) { return callback; }
void fastpath_void(int value) { fastpath_void_sink = value; }
char *fastpath_borrow(char *input) { return input; }
int fastpath_zero(void) { return FASTPATH_ANSWER; }
void fastpath_void_zero(void) { fastpath_void_sink = FASTPATH_ANSWER; }
int fastpath_unpublished(void) { return 0; }
int fastpath_published(void) { return fastpath_target_payload; }
const cmeta_function_abi_desc *fastpath_peer_abi(void) {
    return fastpath_other_function_abi();
}
cmeta_static_call(fastpath_cpp_slot, fastpath_add);
int fastpath_cpp_invoke(int value) { return cmeta_static_invoke(fastpath_cpp_slot, value); }
