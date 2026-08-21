#include "tinymock.hpp"
#include "tinytest.hpp"

#include <stdexcept>

struct tinymock_cpp_service {
  virtual ~tinymock_cpp_service() = default;
  virtual int add(int left, int right) = 0;
  virtual int status() const noexcept = 0;
  virtual int many(int, int, int, int, int, int, int, int, int, int, int, int, int) = 0;
};

struct tinymock_cpp_service_mock final : tinymock_cpp_service {
  TINYMOCK_CPP_MOCK_METHOD2(int, add, int, int)
  TINYMOCK_CPP_MOCK_METHOD0_CONST_NOEXCEPT(int, status)
  TINYMOCK_CPP_MOCK_METHOD13(int, many, int, int, int, int, int, int, int, int, int, int, int,
                             int, int)
};

suite("tinymock cpp") {
  it("matches arguments and records calls") {
    tinymock::function_mock<int(int, int)> mock;
    mock.on([](int left, int right) { return left == 2 && right == 3; },
            [](int left, int right) { return left + right; });

    check_equal(mock.invoke(2, 3), 5);
    check_equal(mock.calls().size(), 1u);
    mock.verify();
  }

  it("supports ordered expectations") {
    tinymock::sequence order;
    tinymock::function_mock<void(int)> first;
    tinymock::function_mock<void(int)> second;

    first.on(tinymock::eq(1), [](int) {}, order);
    second.on(tinymock::eq(2), [](int) {}, order);
    first.invoke(1);
    second.invoke(2);
    first.verify();
    second.verify();
  }

  it("generates qualified and high-arity methods") {
    tinymock_cpp_service_mock mock;
    mock.add_mock.then_return([](int left, int right) { return left + right; });
    mock.status_mock.then_return([] { return 7; });
    mock.many_mock.then_return([](int a0, int a1, int a2, int a3, int a4, int a5, int a6,
                                  int a7, int a8, int a9, int a10, int a11, int a12) {
      return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 + a12;
    });

    const tinymock_cpp_service &service = mock;
    check_equal(mock.add(4, 5), 9);
    check_equal(service.status(), 7);
    check_equal(mock.many(1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1), 13);
  }

  it("reports missing calls") {
    tinymock::function_mock<int()> mock;
    mock.then_return([] { return 1; }, 2);
    check_throws_as(mock.verify(), std::runtime_error);
  }
}
