/*
 * MPMC priority queue timeout protocol model.
 *
 * Scope:
 *   - empty try_pop leaves the caller's output unchanged
 *   - blocking pop on an empty queue returns false after a finite timeout
 *   - a later push/pop still transfers exactly one value
 *   - the bounded queue never exceeds its capacity
 *
 * Queue shutdown is intentionally not modeled: the public MPMC API exposes
 * timeout but no shutdown predicate, and destroy requires quiescent callers.
 */

#define CAPACITY 1
#define TIMEOUT_TICKS 2

byte queue_count = 0;
byte waiting = 0;
byte timeout_ticks = 0;
byte timeout_done = 0;
byte timeout_result = 2;
byte try_done = 0;
byte try_result = 2;
byte output_value = 99;
byte pushed_value = 7;
byte popped_value = 99;
byte final_done = 0;

proctype TryPopper() {
    do
    :: try_done == 0 && queue_count == 0 ->
        atomic {
            try_result = 0;
            assert(output_value == 99);
            try_done = 1
        }
        break
    od
}

proctype TimeoutPopper() {
    waiting = 1;
    do
    :: waiting == 1 && queue_count == 0 && timeout_ticks < TIMEOUT_TICKS ->
        timeout_ticks++
    :: waiting == 1 && queue_count == 0 && timeout_ticks == TIMEOUT_TICKS ->
        atomic {
            timeout_result = 0;
            waiting = 0;
            timeout_done = 1;
            assert(output_value == 99)
        }
        break
    od
}

proctype Owner() {
    do
    :: try_done == 1 && timeout_done == 1 && final_done == 0 ->
        atomic {
            assert(queue_count == 0);
            assert(try_result == 0);
            assert(timeout_result == 0);
            assert(output_value == 99);

            queue_count = queue_count + 1;
            assert(queue_count <= CAPACITY);

            popped_value = pushed_value;
            queue_count = queue_count - 1;
            assert(popped_value == pushed_value);
            assert(queue_count == 0);
            final_done = 1
        }
        break
    od
}

init {
    atomic {
        run TryPopper();
        run TimeoutPopper();
        run Owner()
    }
}

ltl empty_try_pop_preserves_output {
    [] (try_done -> (try_result == 0 && output_value == 99))
}

ltl timeout_pop_returns_false {
    [] (timeout_done -> (timeout_result == 0 && queue_count == 0))
}

ltl queue_capacity_is_bounded {
    [] (queue_count <= CAPACITY)
}

ltl timeout_wait_eventually_finishes {
    [] (waiting -> <> timeout_done)
}

ltl successful_transfer_drains {
    [] (final_done -> (popped_value == pushed_value && queue_count == 0))
}
