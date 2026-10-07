#include <coro.h>
#include <tinytest.h>

#include <string.h>

enum { COMPLETION_REUSE_ROUNDS = 32 };

/* The test thread owns both frames and the borrowed state through teardown.
 * Callbacks record observations instead of longjmp-based test assertions. */
static coro_t *parent;
static coro_t *child;
static struct {
  int calls;
  int child_yield;
  int parent_yield;
  int first_resume;
  int second_resume;
  coro_state_t child_suspended;
  coro_t *child_owner;
  coro_t *parent_before;
  coro_t *parent_after;
} state;

static void return_immediately(coro_t *coroutine, void *argument) {
  (void)coroutine;
  (void)argument;
  state.child_owner = coro_running();
  ++state.calls;
}

static void return_after_yield(coro_t *coroutine, void *argument) {
  (void)coroutine;
  (void)argument;
  state.child_yield = coro_yield();
  state.child_owner = coro_running();
  ++state.calls;
}

static void resume_nested_child(coro_t *coroutine, void *argument) {
  (void)coroutine;
  (void)argument;
  state.parent_before = coro_running();
  state.first_resume = coro_resume(child);
  state.child_suspended = coro_state(child);
  state.parent_yield = coro_yield();
  state.second_resume = coro_resume(child);
  state.parent_after = coro_running();
}

spec("Coroutine completion restores the caller context") {
  before_each() {
    parent = NULL;
    child = NULL;
    memset(&state, 0, sizeof(state));
  }

  after_each() {
    if (child != NULL) coro_destroy(child);
    if (parent != NULL) coro_destroy(parent);
  }

  it("returns to the thread and reuses a completed frame") {
    child = coro_create(return_immediately, NULL, NULL);
    check_not_null(child);
    for (int round = 0; round < COMPLETION_REUSE_ROUNDS; ++round) {
      if (round != 0)
        check_equal(coro_reset(child, return_immediately, NULL), 0);
      check_equal(coro_resume(child), 0);
      check_equal(coro_state(child), coro_DEAD);
      check_true(state.child_owner == child);
      check_equal(state.calls, round + 1);
      check_null(coro_running());
    }
  }

  it("returns after a suspended child and its parent both finish") {
    child = coro_create(return_after_yield, NULL, NULL);
    parent = coro_create(resume_nested_child, NULL, NULL);
    check_not_null(child);
    check_not_null(parent);
    check_equal(coro_resume(parent), 0);
    check_equal(coro_state(parent), coro_SUSPENDED);
    check_equal(state.first_resume, 0);
    check_equal(state.child_suspended, coro_SUSPENDED);
    check_equal(state.calls, 0);
    check_null(coro_running());

    check_equal(coro_resume(parent), 0);
    check_equal(state.parent_yield, 0);
    check_equal(state.second_resume, 0);
    check_equal(state.child_yield, 0);
    check_equal(state.calls, 1);
    check_true(state.child_owner == child);
    check_true(state.parent_before == parent);
    check_true(state.parent_after == parent);
    check_equal(coro_state(child), coro_DEAD);
    check_equal(coro_state(parent), coro_DEAD);
    check_null(coro_running());
  }
}
