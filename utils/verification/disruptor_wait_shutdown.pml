/*
 * Disruptor worker wait/shutdown protocol model.
 *
 * Scope:
 *   - a parked worker returns without claiming when should_run becomes false
 *   - wake_all causes the parked worker to re-evaluate the predicate
 *   - a published entry wakes another parked worker and is claimed once
 *   - a claimed entry reaches exactly one release
 */

byte shutdown_entered = 0;
byte shutdown_running = 1;
byte shutdown_wake = 0;
byte shutdown_requested = 0;
byte shutdown_done = 0;
byte shutdown_result = 2;
byte shutdown_claimed = 0;

byte publish_entered = 0;
byte publish_running = 1;
byte publish_wake = 0;
byte publisher_claimed = 0;
byte published = 0;
byte worker_claimed = 0;
byte worker_released = 0;
byte publish_done = 0;
byte worker_result = 2;
byte worker_sequence = 0;

proctype ShutdownWaiter() {
    do
    :: shutdown_entered == 0 ->
        shutdown_entered = 1
    :: shutdown_entered == 1 && shutdown_wake == 1 &&
       shutdown_running == 0 ->
        atomic {
            shutdown_result = 0;
            shutdown_done = 1
        }
        break
    od
}

proctype ShutdownController() {
    do
    :: shutdown_entered == 1 && shutdown_requested == 0 ->
        atomic {
            shutdown_running = 0;
            shutdown_requested = 1;
            shutdown_wake = 1
        }
    :: shutdown_done == 1 ->
        break
    od
}

proctype PublishingWaiter() {
    do
    :: publish_entered == 0 ->
        publish_entered = 1
    :: publish_entered == 1 && published == 1 && worker_claimed == 0 ->
        atomic {
            worker_claimed = 1;
            worker_sequence = 1;
            worker_result = 1
        }
    :: worker_claimed == 1 && worker_released == 0 ->
        atomic {
            worker_released = 1;
            publish_done = 1
        }
        break
    od
}

proctype Publisher() {
    do
    :: publish_entered == 1 && publisher_claimed == 0 ->
        atomic {
            publisher_claimed = 1;
            published = 1;
            publish_wake = 1
        }
        break
    od
}

proctype Owner() {
    do
    :: shutdown_done == 1 && publish_done == 1 ->
        assert(shutdown_result == 0);
        assert(shutdown_claimed == 0);
        assert(shutdown_requested == 1);
        assert(worker_result == 1);
        assert(publisher_claimed == 1);
        assert(published == 1);
        assert(worker_sequence == 1);
        assert(worker_released == 1);
        break
    od
}

init {
    atomic {
        run ShutdownWaiter();
        run ShutdownController();
        run PublishingWaiter();
        run Publisher();
        run Owner()
    }
}

ltl shutdown_waiter_returns_without_claim {
    [] (shutdown_done -> shutdown_claimed == 0)
}

ltl shutdown_wake_eventually_returns {
    [] (shutdown_requested -> <> shutdown_done)
}

ltl worker_claims_only_published_entry {
    [] (worker_claimed -> published)
}

ltl published_entry_eventually_releases {
    [] (published -> <> worker_released)
}

ltl worker_release_follows_claim {
    [] (worker_released -> worker_claimed)
}
