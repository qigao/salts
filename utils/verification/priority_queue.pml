/*
 * Rocida MPMC bucket-priority queue protocol model.
 *
 * Scope:
 *   - four independent priority buckets with a small hard capacity
 *   - concurrent try_push producers and one mutex-serialized popper
 *   - full-bucket rejection followed by retry after a pop
 *   - strict priority among entries visible at pop time
 *   - per-bucket FIFO counters
 *   - notify/wait epoch for the blocking pop path
 *
 * The model abstracts each bucket's Disruptor storage to count and serial
 * cursors.  The underlying claim/publish/release protocol is covered by the
 * disruptor_worker model; this model targets wrapper-level priority/wakeup
 * semantics.
 */

#define PRIORITY_COUNT 4
#define BUCKET_CAPACITY 1
#define PRODUCER_COUNT 2
#define JOBS_PER_PRODUCER 2
#define TOTAL_JOBS 4

#define LOW 0
#define NORMAL 1
#define HIGH 2
#define CRITICAL 3

byte bucket_count[PRIORITY_COUNT];
byte bucket_next_serial[PRIORITY_COUNT];
byte bucket_pop_serial[PRIORITY_COUNT];
byte pushed_count = 0;
byte popped_count = 0;

byte producer_phase[PRODUCER_COUNT];
byte producer_done[PRODUCER_COUNT];
byte push_rejected[PRODUCER_COUNT];
byte producer_waiting[PRODUCER_COUNT];

byte notify_epoch = 0;
byte waiting = 0;
byte wait_epoch = 0;
byte popper_done = 0;

#define all_producers_done (producer_done[0] && producer_done[1])
#define all_buckets_empty (bucket_count[0] == 0 && \
                           bucket_count[1] == 0 && \
                           bucket_count[2] == 0 && \
                           bucket_count[3] == 0)

proctype Producer(byte id) {
    do
    :: id == 0 && producer_phase[id] == 0 &&
       bucket_count[LOW] < BUCKET_CAPACITY ->
        atomic {
            if
            :: bucket_count[LOW] < BUCKET_CAPACITY ->
                bucket_count[LOW]++;
                bucket_next_serial[LOW]++;
                pushed_count++;
                notify_epoch++;
                producer_phase[id] = 1
            :: else -> skip
            fi
        }
    :: id == 0 && producer_phase[id] == 1 &&
       bucket_count[CRITICAL] < BUCKET_CAPACITY ->
        atomic {
            if
            :: bucket_count[CRITICAL] < BUCKET_CAPACITY ->
                bucket_count[CRITICAL]++;
                bucket_next_serial[CRITICAL]++;
                pushed_count++;
                notify_epoch++;
                producer_phase[id] = 2
            :: else -> skip
            fi
        }
    :: id == 1 && producer_phase[id] == 0 &&
       bucket_count[LOW] < BUCKET_CAPACITY ->
        atomic {
            if
            :: bucket_count[LOW] < BUCKET_CAPACITY ->
                bucket_count[LOW]++;
                bucket_next_serial[LOW]++;
                pushed_count++;
                notify_epoch++;
                producer_phase[id] = 1
            :: else -> skip
            fi
        }
    :: id == 1 && producer_phase[id] == 0 &&
       push_rejected[id] == 0 &&
       bucket_count[LOW] == BUCKET_CAPACITY ->
        atomic {
            push_rejected[id] = 1;
            producer_waiting[id] = 1;
        }
        progress_full_rejection: skip;
    :: id == 1 && producer_phase[id] == 0 &&
       producer_waiting[id] == 1 &&
       bucket_count[LOW] < BUCKET_CAPACITY ->
        atomic {
            if
            :: bucket_count[LOW] < BUCKET_CAPACITY ->
                bucket_count[LOW]++;
                bucket_next_serial[LOW]++;
                pushed_count++;
                notify_epoch++;
                producer_phase[id] = 1;
                producer_waiting[id] = 0
            :: else -> skip
            fi
        }
    :: id == 1 && producer_phase[id] == 1 &&
       bucket_count[HIGH] < BUCKET_CAPACITY ->
        atomic {
            if
            :: bucket_count[HIGH] < BUCKET_CAPACITY ->
                bucket_count[HIGH]++;
                bucket_next_serial[HIGH]++;
                pushed_count++;
                notify_epoch++;
                producer_phase[id] = 2
            :: else -> skip
            fi
        }
    :: producer_phase[id] == JOBS_PER_PRODUCER &&
       producer_done[id] == 0 ->
        producer_done[id] = 1;
        break
    od
}

proctype Popper() {
    do
    :: popped_count < TOTAL_JOBS && !all_buckets_empty ->
        atomic {
            if
            :: bucket_count[CRITICAL] > 0 ->
                assert(bucket_pop_serial[CRITICAL] < bucket_next_serial[CRITICAL]);
                bucket_count[CRITICAL]--;
                bucket_pop_serial[CRITICAL]++
            :: bucket_count[HIGH] > 0 &&
               bucket_count[CRITICAL] == 0 ->
                assert(bucket_pop_serial[HIGH] < bucket_next_serial[HIGH]);
                bucket_count[HIGH]--;
                bucket_pop_serial[HIGH]++
            :: bucket_count[NORMAL] > 0 &&
               bucket_count[CRITICAL] == 0 &&
               bucket_count[HIGH] == 0 ->
                assert(bucket_pop_serial[NORMAL] < bucket_next_serial[NORMAL]);
                bucket_count[NORMAL]--;
                bucket_pop_serial[NORMAL]++
            :: bucket_count[LOW] > 0 &&
               bucket_count[CRITICAL] == 0 &&
               bucket_count[HIGH] == 0 &&
               bucket_count[NORMAL] == 0 ->
                assert(bucket_pop_serial[LOW] < bucket_next_serial[LOW]);
                bucket_count[LOW]--;
                bucket_pop_serial[LOW]++
            fi;
            popped_count++;
            waiting = 0;
        }
        progress_pop: skip;
    :: popped_count < TOTAL_JOBS && all_buckets_empty &&
       !all_producers_done && waiting == 0 ->
        atomic {
            wait_epoch = notify_epoch;
            waiting = 1;
        }
    :: waiting == 1 && notify_epoch != wait_epoch ->
        atomic {
            waiting = 0;
        }
        progress_wakeup: skip;
    :: popped_count == TOTAL_JOBS && all_producers_done &&
       all_buckets_empty ->
        popper_done = 1;
        break
    od
}

proctype Owner() {
    do
    :: popper_done == 1 ->
        assert(pushed_count == TOTAL_JOBS);
        assert(popped_count == TOTAL_JOBS);
        assert(bucket_pop_serial[0] == bucket_next_serial[0]);
        assert(bucket_pop_serial[1] == bucket_next_serial[1]);
        assert(bucket_pop_serial[2] == bucket_next_serial[2]);
        assert(bucket_pop_serial[3] == bucket_next_serial[3]);
        break
    od
}

init {
    atomic {
        run Producer(0);
        run Producer(1);
        run Popper();
        run Owner();
    }
}

ltl capacity_is_never_exceeded {
    [] (bucket_count[0] <= BUCKET_CAPACITY &&
        bucket_count[1] <= BUCKET_CAPACITY &&
        bucket_count[2] <= BUCKET_CAPACITY &&
        bucket_count[3] <= BUCKET_CAPACITY)
}

ltl all_pushed_jobs_eventually_pop {
    [] (pushed_count == TOTAL_JOBS -> <> popper_done)
}

ltl full_try_push_is_observable {
    [] (bucket_count[LOW] == BUCKET_CAPACITY ->
        <> (push_rejected[1] == 1 || bucket_count[LOW] == 0))
}

ltl blocking_pop_wakes_after_publish {
    [] (waiting && !all_producers_done -> <> !waiting)
}

ltl priority_queue_eventually_quiesces {
    [] (all_producers_done -> <> popper_done)
}
