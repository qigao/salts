/*
 * Salts Disruptor broadcast protocol model.
 *
 * Scope:
 *   - one publisher and two registered consumers
 *   - every consumer observes every published entry
 *   - the slowest consumer gates slot reuse
 *   - each consumer releases each entry exactly once
 *   - shutdown waits for the broadcast stream to drain
 */

#define CAPACITY 2
#define TOTAL_ENTRIES 3
#define CONSUMER_COUNT 2

byte next_sequence = 0;
byte gate_cursor = 0;
byte published[TOTAL_ENTRIES];
byte claimed[TOTAL_ENTRIES];
byte seen0[TOTAL_ENTRIES];
byte seen1[TOTAL_ENTRIES];
byte released0[TOTAL_ENTRIES];
byte released1[TOTAL_ENTRIES];

byte producer_sequence = 0;
byte producer_has_claim = 0;
byte producer_done = 0;
byte consumer_next[CONSUMER_COUNT];
byte consumer_has_entry[CONSUMER_COUNT];
byte consumer_sequence[CONSUMER_COUNT];
byte consumer_done[CONSUMER_COUNT];

byte shutdown_requested = 0;

#define all_consumers_done (consumer_done[0] && consumer_done[1])
#define all_entries_published (published[0] && published[1] && published[2])
#define all_entries_released \
    (released0[0] && released0[1] && released0[2] && \
     released1[0] && released1[1] && released1[2])

proctype Producer() {
    do
    :: producer_has_claim == 0 &&
       next_sequence < TOTAL_ENTRIES &&
       (next_sequence - gate_cursor) < CAPACITY ->
        atomic {
            producer_sequence = next_sequence;
            claimed[next_sequence] = 1;
            next_sequence++;
            producer_has_claim = 1;
        }
    :: producer_has_claim == 1 ->
        atomic {
            assert(claimed[producer_sequence] == 1);
            assert(published[producer_sequence] == 0);
            published[producer_sequence] = 1;
            producer_has_claim = 0;
        }
        progress_publish: skip;
    :: producer_has_claim == 0 && next_sequence >= TOTAL_ENTRIES ->
        producer_done = 1;
        break
    od
}

proctype Gate() {
    do
    :: gate_cursor < TOTAL_ENTRIES &&
       released0[gate_cursor] == 1 &&
       released1[gate_cursor] == 1 ->
        atomic {
            gate_cursor++
        }
    :: gate_cursor == TOTAL_ENTRIES ->
        break
    od
}

proctype Consumer(byte id) {
    do
    :: consumer_has_entry[id] == 0 &&
       consumer_next[id] < TOTAL_ENTRIES &&
       published[consumer_next[id]] == 1 ->
        atomic {
            if
            :: id == 0 -> assert(seen0[consumer_next[id]] == 0)
            :: id == 1 -> assert(seen1[consumer_next[id]] == 0)
            fi;
            consumer_sequence[id] = consumer_next[id];
            consumer_next[id]++;
            consumer_has_entry[id] = 1;
            if
            :: id == 0 -> seen0[consumer_sequence[id]] = 1
            :: id == 1 -> seen1[consumer_sequence[id]] = 1
            fi
        }
    :: consumer_has_entry[id] == 1 ->
        atomic {
            if
            :: id == 0 ->
                assert(seen0[consumer_sequence[id]] == 1);
                assert(released0[consumer_sequence[id]] == 0);
                released0[consumer_sequence[id]] = 1
            :: id == 1 ->
                assert(seen1[consumer_sequence[id]] == 1);
                assert(released1[consumer_sequence[id]] == 0);
                released1[consumer_sequence[id]] = 1
            fi;
            consumer_has_entry[id] = 0;
        }
        progress_consumer_release: skip;
    :: shutdown_requested == 1 &&
       consumer_has_entry[id] == 0 &&
       consumer_next[id] >= TOTAL_ENTRIES ->
        consumer_done[id] = 1;
        break
    od
}

proctype Stopper() {
    do
    :: producer_done == 1 -> break
    od;

    atomic {
        assert(all_entries_published);
        shutdown_requested = 1;
    }
}

proctype Owner() {
    do
    :: all_consumers_done && gate_cursor == TOTAL_ENTRIES ->
        assert(all_entries_released);
        break
    od
}

init {
    atomic {
        run Producer();
        run Gate();
        run Consumer(0);
        run Consumer(1);
        run Stopper();
        run Owner();
    }
}

ltl every_consumer_sees_every_entry {
    [] ((published[0] -> <> (seen0[0] && seen1[0])) &&
        (published[1] -> <> (seen0[1] && seen1[1])) &&
        (published[2] -> <> (seen0[2] && seen1[2])))
}

ltl broadcast_releases_after_observe {
    [] ((!released0[0] || seen0[0]) &&
        (!released0[1] || seen0[1]) &&
        (!released0[2] || seen0[2]) &&
        (!released1[0] || seen1[0]) &&
        (!released1[1] || seen1[1]) &&
        (!released1[2] || seen1[2]))
}

ltl slowest_consumer_gates_reuse {
    [] ((next_sequence - gate_cursor) <= CAPACITY)
}

ltl broadcast_eventually_drains {
    [] (producer_done -> <> (all_consumers_done && gate_cursor == TOTAL_ENTRIES))
}
