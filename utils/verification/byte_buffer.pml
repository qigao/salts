/*
 * TurboUtils bounded byte-buffer protocol model.
 *
 * Scope:
 *   - one owner; no concurrent access
 *   - bounded unread bytes and cursor invariants
 *   - append through tail space, compaction, and abstract growth
 *   - append from the current unread view across growth
 *   - atomic ENOSPC and ERANGE failures
 *   - FIFO consumption, reset, borrowed-view invalidation, and destroy
 *
 * The small initial capacity is intentional: it makes compaction and growth
 * reachable in a finite model.  It abstracts the implementation's allocator
 * growth factor while preserving the public capacity and ownership protocol.
 */

#define INITIAL_CAPACITY 4
#define MAX_BYTES 8
#define TOTAL_APPENDED 10
#define TOTAL_CONSUMED 10
#define FINAL_PHASE 19

byte storage[MAX_BYTES];
byte expected[MAX_BYTES];
byte snapshot[MAX_BYTES];
byte snapshot_expected[MAX_BYTES];

byte read_pos = 0;
byte write_pos = 0;
byte capacity = 0;
byte next_value = 1;
byte expected_used = 0;
byte consumed_bytes = 0;
byte appended_bytes = 0;

byte generation = 0;
byte view_generation = 0;
byte view_size = 0;
byte view_live = 0;
byte destroyed = 0;

byte append_failed = 0;
byte consume_failed = 0;
byte drain_started = 0;
byte phase = 0;

byte snapshot_read = 0;
byte snapshot_write = 0;
byte snapshot_capacity = 0;
byte snapshot_generation = 0;
byte snapshot_used = 0;
byte snapshot_expected_used = 0;
byte i = 0;
byte current_size = 0;
byte source0 = 0;
byte source1 = 0;

#define unread (write_pos - read_pos)

inline save_snapshot() {
    snapshot_read = read_pos;
    snapshot_write = write_pos;
    snapshot_capacity = capacity;
    snapshot_generation = generation;
    snapshot_used = unread;
    snapshot_expected_used = expected_used;

    i = 0;
    do
    :: i < MAX_BYTES ->
        snapshot[i] = storage[i];
        snapshot_expected[i] = expected[i];
        i++
    :: else -> break
    od
}

inline assert_snapshot_unchanged() {
    assert(read_pos == snapshot_read);
    assert(write_pos == snapshot_write);
    assert(capacity == snapshot_capacity);
    assert(generation == snapshot_generation);
    assert(unread == snapshot_used);
    assert(expected_used == snapshot_expected_used);

    i = 0;
    do
    :: i < MAX_BYTES ->
        assert(storage[i] == snapshot[i]);
        assert(expected[i] == snapshot_expected[i]);
        i++
    :: else -> break
    od
}

inline invalidate_view() {
    view_live = 0;
    generation++
}

inline append_fresh(n) {
    current_size = unread;
    assert(n <= MAX_BYTES - current_size);

    if
    :: n <= capacity - write_pos ->
        skip
    :: n <= capacity - current_size ->
        i = 0;
        do
        :: i < current_size ->
            storage[i] = storage[read_pos + i];
            i++
        :: else -> break
        od;
        read_pos = 0;
        write_pos = current_size
    :: else ->
        i = 0;
        do
        :: i < current_size ->
            storage[i] = storage[read_pos + i];
            i++
        :: else -> break
        od;
        read_pos = 0;
        write_pos = current_size;
        if
        :: capacity == 0 ->
            capacity = INITIAL_CAPACITY
        :: else ->
            capacity = current_size + n
        fi;
        assert(capacity >= current_size + n);
        assert(capacity <= MAX_BYTES)
    fi;

    i = 0;
    do
    :: i < n ->
        storage[write_pos + i] = next_value + i;
        expected[expected_used + i] = next_value + i;
        i++
    :: else -> break
    od;
    write_pos = write_pos + n;
    expected_used = expected_used + n;
    next_value = next_value + n;
    appended_bytes = appended_bytes + n;
    invalidate_view()
}

inline append_alias_two() {
    current_size = unread;
    assert(view_live == 1);
    assert(view_generation == generation);
    assert(view_size == current_size);
    assert(current_size >= 3);

    /* Capture the source before compaction or abstract reallocation. */
    source0 = storage[read_pos + 1];
    source1 = storage[read_pos + 2];
    assert(source0 == expected[1]);
    assert(source1 == expected[2]);
    assert(current_size + 2 <= MAX_BYTES);

    if
    :: 2 <= capacity - write_pos ->
        skip
    :: 2 <= capacity - current_size ->
        i = 0;
        do
        :: i < current_size ->
            storage[i] = storage[read_pos + i];
            i++
        :: else -> break
        od;
        read_pos = 0;
        write_pos = current_size
    :: else ->
        i = 0;
        do
        :: i < current_size ->
            storage[i] = storage[read_pos + i];
            i++
        :: else -> break
        od;
        read_pos = 0;
        write_pos = current_size;
        capacity = current_size + 2;
        assert(capacity <= MAX_BYTES)
    fi;

    storage[write_pos] = source0;
    storage[write_pos + 1] = source1;
    expected[expected_used] = expected[1];
    expected[expected_used + 1] = expected[2];
    write_pos = write_pos + 2;
    expected_used = expected_used + 2;
    appended_bytes = appended_bytes + 2;
    invalidate_view()
}

inline consume_bytes(n) {
    current_size = unread;
    assert(n <= current_size);

    i = 0;
    do
    :: i < n ->
        assert(storage[read_pos + i] == expected[i]);
        i++
    :: else -> break
    od;

    i = 0;
    do
    :: i < current_size - n ->
        expected[i] = expected[i + n];
        i++
    :: else -> break
    od;
    expected_used = current_size - n;

    if
    :: n == current_size ->
        read_pos = 0;
        write_pos = 0
    :: else ->
        read_pos = read_pos + n
    fi;
    consumed_bytes = consumed_bytes + n;
    invalidate_view()
}

inline expose_view() {
    view_generation = generation;
    view_size = unread;
    view_live = 1
}

inline check_invariants() {
    assert(read_pos <= write_pos);
    assert(write_pos <= capacity);
    assert(capacity <= MAX_BYTES);
    assert(unread <= MAX_BYTES);
    assert(expected_used == unread);
    assert(consumed_bytes <= appended_bytes);
    assert(view_live == 0 || view_generation == generation);
    assert(view_live == 0 || view_size == unread)
}

proctype Owner() {
    do
    :: phase < FINAL_PHASE ->
        check_invariants();
        if
        :: phase == 0 ->
            atomic { append_fresh(3) };
            phase = 1
        :: phase == 1 ->
            atomic { expose_view() };
            phase = 2
        :: phase == 2 ->
            save_snapshot();
            phase = 3
        :: phase == 3 ->
            assert_snapshot_unchanged();
            append_failed = 1;
            phase = 4
        :: phase == 4 ->
            append_failed = 0;
            atomic { consume_bytes(1) };
            phase = 5
        :: phase == 5 ->
            atomic { append_fresh(2) };
            phase = 6
        :: phase == 6 ->
            atomic { expose_view() };
            phase = 7
        :: phase == 7 ->
            atomic { consume_bytes(1) };
            phase = 8
        :: phase == 8 ->
            atomic { append_fresh(3) };
            phase = 9
        :: phase == 9 ->
            atomic { expose_view() };
            phase = 10
        :: phase == 10 ->
            atomic { append_alias_two() };
            phase = 11
        :: phase == 11 ->
            atomic { expose_view() };
            phase = 12
        :: phase == 12 ->
            save_snapshot();
            phase = 13
        :: phase == 13 ->
            assert_snapshot_unchanged();
            consume_failed = 1;
            phase = 14
        :: phase == 14 ->
            consume_failed = 0;
            drain_started = 1;
            atomic { consume_bytes(8) };
            phase = 15
        :: phase == 15 ->
            atomic { expose_view() };
            phase = 16
        :: phase == 16 ->
            atomic {
                assert(view_live == 1);
                assert(view_size == 0);
                read_pos = 0;
                write_pos = 0;
                invalidate_view()
            };
            phase = 17
        :: phase == 17 ->
            atomic { expose_view() };
            phase = 18
        :: phase == 18 ->
            atomic {
                assert(view_live == 1);
                view_live = 0;
                destroyed = 1;
                generation++
            };
            phase = FINAL_PHASE
        fi
    :: phase == FINAL_PHASE ->
        check_invariants();
        assert(destroyed == 1);
        assert(view_live == 0);
        assert(unread == 0);
        assert(consumed_bytes == TOTAL_CONSUMED);
        assert(appended_bytes == TOTAL_APPENDED);
        break
    od
}

init {
    atomic {
        run Owner()
    }
}

ltl byte_buffer_capacity_is_bounded {
    [] (unread <= MAX_BYTES)
}

ltl byte_buffer_cursors_remain_valid {
    [] (read_pos <= write_pos && write_pos <= capacity && capacity <= MAX_BYTES)
}

ltl failed_append_is_atomic {
    [] (append_failed ->
        (unread == snapshot_used && read_pos == snapshot_read &&
         write_pos == snapshot_write && capacity == snapshot_capacity &&
         generation == snapshot_generation && expected_used == snapshot_expected_used))
}

ltl failed_consume_is_atomic {
    [] (consume_failed ->
        (unread == snapshot_used && read_pos == snapshot_read &&
         write_pos == snapshot_write && capacity == snapshot_capacity &&
         generation == snapshot_generation && expected_used == snapshot_expected_used))
}

ltl borrowed_view_matches_current_state {
    [] (view_live -> (view_generation == generation && view_size == unread))
}

ltl byte_buffer_eventually_destroyed {
    [] (drain_started -> <> destroyed)
}
