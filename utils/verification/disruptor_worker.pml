/*
 * TurboUtils Disruptor worker-pool protocol model.
 *
 * Scope:
 *   - bounded publisher claim against the completed cursor
 *   - out-of-order publication without exposing a gap
 *   - one worker claim and one release per entry
 *   - contiguous completion advancing capacity
 *   - shutdown after all published entries have drained
 *
 * The model abstracts entry storage to one byte per sequence.  It verifies
 * ownership and cursor protocol, not C11 implementation details or cache
 * ordering; those remain covered by sanitizer/stress validation.
 */

#define CAPACITY 2
#define TOTAL_ENTRIES 4
#define PRODUCER_COUNT 2
#define WORKER_COUNT 2

byte next_sequence = 0;
byte worker_claim_cursor = 0;
byte worker_completed_cursor = 0;
byte published[TOTAL_ENTRIES];
byte claimed[TOTAL_ENTRIES];
byte released[TOTAL_ENTRIES];
byte processed[TOTAL_ENTRIES];
byte payload[TOTAL_ENTRIES];
byte claim_owner[TOTAL_ENTRIES];

byte producer_sequence[PRODUCER_COUNT];
byte producer_has_claim[PRODUCER_COUNT];
byte producer_done[PRODUCER_COUNT];
byte worker_sequence[WORKER_COUNT];
byte worker_has_claim[WORKER_COUNT];
byte worker_done[WORKER_COUNT];

byte published_count = 0;
byte shutdown_requested = 0;

#define all_producers_done (producer_done[0] && producer_done[1])
#define all_workers_done (worker_done[0] && worker_done[1])
#define all_entries_published (published[0] && published[1] && \
                               published[2] && published[3])
#define all_entries_released (released[0] && released[1] && \
                              released[2] && released[3])
#define all_entries_processed (processed[0] && processed[1] && \
                               processed[2] && processed[3])

proctype Producer(byte id) {
    do
    :: producer_has_claim[id] == 0 && next_sequence == 0 &&
       (next_sequence - worker_completed_cursor) < CAPACITY ->
        atomic {
            if
            :: next_sequence == 0 &&
               (next_sequence - worker_completed_cursor) < CAPACITY ->
                producer_sequence[id] = 0;
                claim_owner[0] = id;
                claimed[0] = 1;
                next_sequence = 1;
                producer_has_claim[id] = 1
            :: else -> skip
            fi
        }
    :: producer_has_claim[id] == 0 && next_sequence == 1 &&
       (next_sequence - worker_completed_cursor) < CAPACITY ->
        atomic {
            if
            :: next_sequence == 1 &&
               (next_sequence - worker_completed_cursor) < CAPACITY ->
                producer_sequence[id] = 1;
                claim_owner[1] = id;
                claimed[1] = 1;
                next_sequence = 2;
                producer_has_claim[id] = 1
            :: else -> skip
            fi
        }
    :: producer_has_claim[id] == 0 && next_sequence == 2 &&
       (next_sequence - worker_completed_cursor) < CAPACITY ->
        atomic {
            if
            :: next_sequence == 2 &&
               (next_sequence - worker_completed_cursor) < CAPACITY ->
                producer_sequence[id] = 2;
                claim_owner[2] = id;
                claimed[2] = 1;
                next_sequence = 3;
                producer_has_claim[id] = 1
            :: else -> skip
            fi
        }
    :: producer_has_claim[id] == 0 && next_sequence == 3 &&
       (next_sequence - worker_completed_cursor) < CAPACITY ->
        atomic {
            if
            :: next_sequence == 3 &&
               (next_sequence - worker_completed_cursor) < CAPACITY ->
                producer_sequence[id] = 3;
                claim_owner[3] = id;
                claimed[3] = 1;
                next_sequence = 4;
                producer_has_claim[id] = 1
            :: else -> skip
            fi
        }
    :: producer_has_claim[id] == 1 ->
        atomic {
            assert(claimed[producer_sequence[id]] == 1);
            assert(published[producer_sequence[id]] == 0);
            payload[producer_sequence[id]] = producer_sequence[id] + 1;
            published[producer_sequence[id]] = 1;
            producer_has_claim[id] = 0;
            published_count++;
        }
    :: producer_has_claim[id] == 0 && next_sequence >= TOTAL_ENTRIES ->
        producer_done[id] = 1;
        break
    od
}

proctype CompletionTracker() {
    do
    :: worker_completed_cursor < TOTAL_ENTRIES &&
       released[worker_completed_cursor] == 1 ->
        atomic {
            worker_completed_cursor++
        }
    :: worker_completed_cursor == TOTAL_ENTRIES ->
        break
    od
}

proctype Worker(byte id) {
    do
    :: worker_has_claim[id] == 0 && worker_claim_cursor == 0 &&
       published[0] == 1 ->
        atomic {
            if
            :: worker_claim_cursor == 0 && published[0] == 1 ->
                assert(claimed[0] == 1);
                assert(released[0] == 0);
                worker_sequence[id] = 0;
                worker_claim_cursor = 1;
                worker_has_claim[id] = 1
            :: else -> skip
            fi
        }
    :: worker_has_claim[id] == 0 && worker_claim_cursor == 1 &&
       published[1] == 1 ->
        atomic {
            if
            :: worker_claim_cursor == 1 && published[1] == 1 ->
                assert(claimed[1] == 1);
                assert(released[1] == 0);
                worker_sequence[id] = 1;
                worker_claim_cursor = 2;
                worker_has_claim[id] = 1
            :: else -> skip
            fi
        }
    :: worker_has_claim[id] == 0 && worker_claim_cursor == 2 &&
       published[2] == 1 ->
        atomic {
            if
            :: worker_claim_cursor == 2 && published[2] == 1 ->
                assert(claimed[2] == 1);
                assert(released[2] == 0);
                worker_sequence[id] = 2;
                worker_claim_cursor = 3;
                worker_has_claim[id] = 1
            :: else -> skip
            fi
        }
    :: worker_has_claim[id] == 0 && worker_claim_cursor == 3 &&
       published[3] == 1 ->
        atomic {
            if
            :: worker_claim_cursor == 3 && published[3] == 1 ->
                assert(claimed[3] == 1);
                assert(released[3] == 0);
                worker_sequence[id] = 3;
                worker_claim_cursor = 4;
                worker_has_claim[id] = 1
            :: else -> skip
            fi
        }
    :: worker_has_claim[id] == 1 ->
        atomic {
            assert(worker_sequence[id] < TOTAL_ENTRIES);
            assert(published[worker_sequence[id]] == 1);
            assert(processed[worker_sequence[id]] == 0);
            assert(payload[worker_sequence[id]] == worker_sequence[id] + 1);
            processed[worker_sequence[id]] = 1;
            released[worker_sequence[id]] = 1;
            worker_has_claim[id] = 0;
        }
        progress_worker_release: skip;
    :: shutdown_requested == 1 &&
       worker_has_claim[id] == 0 &&
       worker_claim_cursor >= TOTAL_ENTRIES ->
        worker_done[id] = 1;
        break
    od
}

proctype Stopper() {
    do
    :: all_producers_done -> break
    od;

    atomic {
        assert(all_entries_published);
        shutdown_requested = 1;
    }
}

proctype Owner() {
    do
    :: all_workers_done && worker_completed_cursor == TOTAL_ENTRIES ->
        assert(all_entries_processed);
        assert(all_entries_released);
        assert(next_sequence - worker_completed_cursor == 0);
        break
    od
}

init {
    atomic {
        run Producer(0);
        run Producer(1);
        run CompletionTracker();
        run Worker(0);
        run Worker(1);
        run Stopper();
        run Owner();
    }
}

ltl no_double_worker_processing {
    [] (processed[0] <= 1 && processed[1] <= 1 &&
        processed[2] <= 1 && processed[3] <= 1)
}

ltl worker_only_claims_published {
    [] ((!worker_has_claim[0] || published[worker_sequence[0]]) &&
        (!worker_has_claim[1] || published[worker_sequence[1]]))
}

ltl completed_cursor_never_exceeds_capacity {
    [] ((next_sequence - worker_completed_cursor) <= CAPACITY)
}

ltl published_entries_eventually_processed {
    [] ((published[0] -> <> processed[0]) &&
        (published[1] -> <> processed[1]) &&
        (published[2] -> <> processed[2]) &&
        (published[3] -> <> processed[3]))
}

ltl shutdown_eventually_quiesces {
    [] (shutdown_requested -> <> (all_workers_done && all_entries_released))
}
