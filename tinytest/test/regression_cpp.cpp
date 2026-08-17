#include "tinytest.hpp"

#include <stdexcept>

static void throw_boom() { throw std::runtime_error("boom"); }

suite("tinytest cpp regression") {
  it("check_equal_warn should evaluate operands once") {
    int value = 1;
    check_equal_warn(value++, 1);
    check_int_eq(value, 2);
  }

  it("check_not_equal_warn should evaluate operands once") {
    int value = 1;
    check_not_equal_warn(value++, 2);
    check_int_eq(value, 2);
  }

  it("check_greater_warn should evaluate operands once") {
    int value = 1;
    check_greater_warn(value++, 0);
    check_int_eq(value, 2);
  }

  it("check_less_warn should evaluate operands once") {
    int value = 1;
    check_less_warn(value++, 2);
    check_int_eq(value, 2);
  }

  it("check_warn should compose with else in C++") {
    if (true)
      check_warn(true);
    else
      check_warn(false);
    check_true(true);
  }

  it_should_fail("should convert an uncaught C++ exception into a test failure") {
    throw_boom();
  }

  it("should continue after an uncaught C++ exception") { check_true(true); }

  bench("explicit benchmark units compile in C++") {
    benchmark_io("C++ batched I/O", 1, 2, 8) {
      volatile int value = 1;
      value += 1;
    }
  }
}
namespace {
/* Destructor probe ... */
struct unwind_probe {
  static int count;
  unwind_probe() {}
  ~unwind_probe() { ++count; }
};
int unwind_probe::count = 0;
} /* namespace */

suite("tinytest cpp unwind") {
  it_should_fail("runs destructors when an assertion fails") {
    unwind_probe p;
    check_int_eq(1, 2);
  }
  it("observed the destructor running during unwind") {
    check_int_eq(unwind_probe::count, 1);
  }

  it_should_fail("propagates a sub-assertion failure out of check_throws") {
    check_throws([] { check_int_eq(1, 2); });
  }
}
