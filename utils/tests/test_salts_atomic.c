#include <stdatomic.h>
#include "tinytest.h"
#include <stdint.h>
#include <stddef.h>

spec("C11 Atomic Operations") {

    it("should handle 32-bit int operations") {
        atomic_int val = 0;
        check_equal(atomic_load(&val), 0);

        atomic_store(&val, 10);
        check_equal(atomic_load(&val), 10);

        // inc: fetch_add + 1
        check_equal(atomic_fetch_add(&val, 1) + 1, 11);
        check_equal(atomic_load(&val), 11);

        // dec: fetch_sub - 1
        check_equal(atomic_fetch_sub(&val, 1) - 1, 10);
        check_equal(atomic_load(&val), 10);

        check_equal(atomic_fetch_add(&val, 5), 10);
        check_equal(atomic_load(&val), 15);

        check_equal(atomic_fetch_sub(&val, 3), 15);
        check_equal(atomic_load(&val), 12);

        int expected = 12;
        bool exchanged = atomic_compare_exchange_strong(&val, &expected, 42);
        check_equal(exchanged, true);
        check_equal(atomic_load(&val), 42);

        expected = 12;
        exchanged = atomic_compare_exchange_strong(&val, &expected, 99);
        check_equal(exchanged, false);
        check_equal(atomic_load(&val), 42);
    }

    it("should handle 64-bit int operations") {
        _Atomic int64_t val = 0;
        check(atomic_load(&val) == 0);

        atomic_store(&val, 1000000000000LL);
        check(atomic_load(&val) == 1000000000000LL);

        check(atomic_load_explicit(&val, memory_order_relaxed) == 1000000000000LL);

        check(atomic_fetch_add(&val, 5) == 1000000000000LL);
        check(atomic_load(&val) == 1000000000005LL);

        check(atomic_fetch_sub(&val, 5) == 1000000000005LL);
        check(atomic_load(&val) == 1000000000000LL);
    }

    it("should handle 16-bit uint operations") {
        _Atomic uint16_t val = 0;
        check_equal(atomic_load(&val), 0);

        atomic_store(&val, 65000);
        check_equal(atomic_load(&val), 65000);

        check_equal(atomic_fetch_add(&val, 100), 65000);
        check_equal(atomic_load(&val), 65100);
    }

    it("should handle pointer operations") {
        void * volatile ptr = NULL;
        int dummy1 = 1;
        int dummy2 = 2;

        void *expected = NULL;
        bool exchanged =
            atomic_compare_exchange_strong((_Atomic(void *) *)&ptr, &expected, &dummy1);
        check_equal(exchanged, true);
        check_equal((const void *)(ptr), (const void *)(&dummy1));

        expected = NULL;
        exchanged = atomic_compare_exchange_strong((_Atomic(void *) *)&ptr, &expected, &dummy2);
        check_equal(exchanged, false);
        check_equal((const void *)(ptr), (const void *)(&dummy1));

        check_equal((const void *)(atomic_exchange((_Atomic(void *) *)&ptr, &dummy2)), (const void *)(&dummy1));
        check_equal((const void *)(ptr), (const void *)(&dummy2));
    }

    it("should handle size_t operations") {
        _Atomic size_t val = 0;
        check_equal(atomic_load(&val), 0);

        atomic_store(&val, 123456);
        check_equal(atomic_load(&val), 123456);
        check_equal(atomic_load_explicit(&val, memory_order_relaxed), 123456);

        atomic_store_explicit(&val, 654321, memory_order_relaxed);
        check_equal(atomic_load(&val), 654321);
        check_equal(atomic_load_explicit(&val, memory_order_relaxed), 654321);
    }

    it("should handle uint32_t operations") {
        _Atomic uint32_t val = 0;
        check_equal(atomic_load(&val), 0);

        atomic_store(&val, 42);
        check_equal(atomic_load(&val), 42);

        check_equal(atomic_fetch_add(&val, 10), 42);
        check_equal(atomic_load(&val), 52);

        check_equal(atomic_fetch_sub(&val, 2), 52);
        check_equal(atomic_load(&val), 50);

        uint32_t expected = 50;
        bool exchanged = atomic_compare_exchange_strong(&val, &expected, 100);
        check_equal(exchanged, true);
        check_equal(atomic_load(&val), 100);
    }

    it("should handle uint64_t operations") {
        _Atomic uint64_t val = 0;
        check(atomic_load(&val) == 0);

        atomic_store(&val, 9999999999ULL);
        check(atomic_load(&val) == 9999999999ULL);

        check(atomic_fetch_add(&val, 1) == 9999999999ULL);
        check(atomic_load(&val) == 10000000000ULL);

        check(atomic_fetch_sub(&val, 1) == 10000000000ULL);
        check(atomic_load(&val) == 9999999999ULL);

        uint64_t expected = 9999999999ULL;
        check(atomic_compare_exchange_strong(&val, &expected, 12345ULL) == 1);
        check(atomic_load(&val) == 12345ULL);
    }

    it("should handle bool operations") {
        _Atomic int val = 0;
        check_equal(atomic_load(&val) != 0, 0);

        atomic_store(&val, 1);
        check_equal(atomic_load(&val) != 0, 1);

        atomic_store(&val, 0);
        check_equal(atomic_load(&val) != 0, 0);

        int expected = 0;
        bool exchanged = atomic_compare_exchange_strong(&val, &expected, 1);
        check_equal(exchanged, true);
        check_equal(atomic_load(&val) != 0, 1);
    }
}
