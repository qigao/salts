#if !defined(__STDC_VERSION__) || __STDC_VERSION__ != 201112L
#error "nested replay direct-failure witness must compile as exact C11"
#endif

/*
 * Expected-failure witness.
 *
 * While P is expanding, OUTER emits another invocation of the same P macro.
 * The strict C preprocessor suppresses that inner invocation while rescanning
 * P's active replacement context, so the inner P(COUNT) remains as C tokens and
 * this translation unit must not compile.
 */
#define P(M) M(1) M(2)
#define COUNT(x) + 1
#define OUTER(x) + (0 P(COUNT))

enum {
    direct_same_producer_count = 0 P(OUTER)
};

int main(void) {
    return direct_same_producer_count == 4 ? 0 : 1;
}
