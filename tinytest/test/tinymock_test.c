#include "tinymock.h"

TINYMOCk_MOCK2(int, tinymock_add, int, int)
TINYMOCk_MOCK1(const char *, tinymock_greet, const char *)
TINYMOCk_MOCK3(int, tinymock_dispatch, const char *, void *, unsigned long long)
TINYMOCk_MOCK1_VOID(tinymock_log, int)
TINYMOCk_MOCK1(int, tinymock_default, int)
TINYMOCk_MOCK2(int, tinymock_sequence, int, int)
TINYMOCk_MOCK2(uint64_t, tinymock_u64_pair, uint64_t, uint64_t)
TINYMOCk_MOCK2(size_t, tinymock_size_pair, size_t, size_t)
TINYMOCk_MOCK2(int, tinymock_bad_arg, int, int)

static int g_tinymock_mismatch_marker = 0;

static bool tinymock_match_int_gt(const void *actual, void *context) {
  const int threshold = *(const int *)context;
  return *(const int *)actual > threshold;
}

static tinymock_value_t tinymock_answer_sum(size_t argc,
                                            const void *const *actual_args,
                                            void *context) {
  int bias = context ? *(const int *)context : 0;
  int result;
  check_size_eq(argc, (size_t)2);
  result = *(const int *)actual_args[0] + *(const int *)actual_args[1] + bias;
  return TINYMOCk_RETURN(result);
}

suite("TinyMock") {

  it("should match int arguments and return mocked value") {
    mock_tinymock_add_reset();
    mock_tinymock_add_expect(TINYMOCk_ARG(2), TINYMOCk_ARG(3), TINYMOCk_RETURN(5));
    check_int_eq(tinymock_add(2, 3), 5);
    mock_tinymock_add_verify();
  }

  it("should match arguments with a predicate matcher") {
    int threshold = 10;

    mock_tinymock_add_reset();
    mock_tinymock_add_expect(TINYMOCk_ARG_THAT(tinymock_match_int_gt, &threshold),
                             TINYMOCk_ARG(3), TINYMOCk_RETURN(14));

    check_int_eq(tinymock_add(11, 3), 14);
    mock_tinymock_add_verify();
  }

  it("should compute an answer from invocation arguments") {
    int bias = 7;

    mock_tinymock_add_reset();
    mock_tinymock_add_expect(TINYMOCk_ARG(2), TINYMOCk_ARG(3),
                             TINYMOCk_ANSWER(tinymock_answer_sum, &bias));

    check_int_eq(tinymock_add(2, 3), 12);
    mock_tinymock_add_verify();
  }

  it("should support cstring and void* args with TINYMOCk_ANY") {
    struct foo {
      int v;
    } payload = {7};

    mock_tinymock_dispatch_reset();
    mock_tinymock_dispatch_expect(TINYMOCk_ARG("cmd"), TINYMOCk_ANY, TINYMOCk_ARG((unsigned long long)42),
                                 TINYMOCk_RETURN(1234));

    check_int_eq(tinymock_dispatch("cmd", &payload, 42), 1234);
    mock_tinymock_dispatch_verify();
  }

  it("should return mocked C string value") {
    mock_tinymock_greet_reset();
    mock_tinymock_greet_expect(TINYMOCk_ARG("name"), TINYMOCk_RETURN("hello"));

    const char *actual = tinymock_greet("name");
    check_str_eq(actual, "hello");
    mock_tinymock_greet_verify();
  }

  it("should accept uint64_t arguments and return") {
    mock_tinymock_u64_pair_reset();
    mock_tinymock_u64_pair_expect(TINYMOCk_ARG((uint64_t)1), TINYMOCk_ARG((uint64_t)2), TINYMOCk_RETURN((uint64_t)3));

    check_true(tinymock_u64_pair((uint64_t)1, (uint64_t)2) == (uint64_t)3);
    mock_tinymock_u64_pair_verify();
  }

  it("should accept size_t arguments and return") {
    mock_tinymock_size_pair_reset();
    mock_tinymock_size_pair_expect(TINYMOCk_ARG((size_t)10), TINYMOCk_ARG((size_t)20), TINYMOCk_RETURN((size_t)30));

    check_size_eq(tinymock_size_pair((size_t)10, (size_t)20), (size_t)30);
    mock_tinymock_size_pair_verify();
  }

  it("should verify void mock calls") {
    mock_tinymock_log_reset();
    mock_tinymock_log_expect(TINYMOCk_ARG(10));

    tinymock_log(10);
    mock_tinymock_log_verify();
  }

  it("should use default return when no expectations are queued") {
    mock_tinymock_default_reset();
    mock_tinymock_default_set_default_return(TINYMOCk_RETURN(-9));

    check_int_eq(tinymock_default(777), -9);
    mock_tinymock_default_verify();
  }

  it("should consume expectations in sequence order") {
    mock_tinymock_sequence_reset();
    mock_tinymock_sequence_expect(TINYMOCk_ARG(1), TINYMOCk_ARG(2), TINYMOCk_RETURN(3));
    mock_tinymock_sequence_expect(TINYMOCk_ARG(3), TINYMOCk_ARG(4), TINYMOCk_RETURN(7));

    check_int_eq(tinymock_sequence(1, 2), 3);
    check_int_eq(tinymock_sequence(3, 4), 7);
    mock_tinymock_sequence_verify();
  }

  it_should_fail("should stop on argument mismatch and not execute subsequent code") {
    g_tinymock_mismatch_marker = 1;

    mock_tinymock_bad_arg_reset();
    mock_tinymock_bad_arg_expect(TINYMOCk_ARG(1), TINYMOCk_ARG(2), TINYMOCk_RETURN(4));

    check_int_eq(tinymock_bad_arg(1, 3), 4);
    g_tinymock_mismatch_marker = 2;
  }

  it("should keep execution marker unchanged after mismatch failure") {
    check_int_eq(g_tinymock_mismatch_marker, 1);
    g_tinymock_mismatch_marker = 0;
  }
}
