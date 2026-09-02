/*
 * Rocida byte-buffer invalid-input protocol model.
 *
 * Scope:
 *   - invalid initialization does not mutate the caller object
 *   - invalid append/view calls preserve state and caller output
 *   - a source range in the consumed region is rejected
 *   - append(NULL, 0) is a successful no-op
 *   - destroy clears the owner state
 */

#define MAX_BYTES 4
#define FINAL_PHASE 9

byte storage[MAX_BYTES];
byte snapshot[MAX_BYTES];
byte read_pos = 0;
byte write_pos = 0;
byte max_bytes = 0;
byte initialized = 0;
byte output_data = 77;
byte output_size = 9;
byte snapshot_read = 0;
byte snapshot_write = 0;
byte snapshot_max = 0;
byte snapshot_initialized = 0;
byte phase = 0;
byte i = 0;
byte consumed_alias_rejected = 0;
byte invalid_append_rejected = 0;
byte invalid_view_rejected = 0;
byte null_zero_append_ok = 0;
byte destroyed = 0;

#define unread (write_pos - read_pos)

inline save_snapshot() {
    snapshot_read = read_pos;
    snapshot_write = write_pos;
    snapshot_max = max_bytes;
    snapshot_initialized = initialized;
    i = 0;
    do
    :: i < MAX_BYTES ->
        snapshot[i] = storage[i];
        i++
    :: else -> break
    od
}

inline assert_snapshot_unchanged() {
    assert(read_pos == snapshot_read);
    assert(write_pos == snapshot_write);
    assert(max_bytes == snapshot_max);
    assert(initialized == snapshot_initialized);
    i = 0;
    do
    :: i < MAX_BYTES ->
        assert(storage[i] == snapshot[i]);
        i++
    :: else -> break
    od
}

proctype Owner() {
    do
    :: phase < FINAL_PHASE ->
        assert(read_pos <= write_pos);
        assert(write_pos <= max_bytes);
        if
        :: phase == 0 ->
            save_snapshot();
            /* init(&buffer, 0) returns EINVAL before touching the object. */
            assert_snapshot_unchanged();
            phase = 1
        :: phase == 1 ->
            save_snapshot();
            /* append on an uninitialized object returns EINVAL. */
            assert(initialized == 0);
            assert_snapshot_unchanged();
            invalid_append_rejected = 1;
            phase = 2
        :: phase == 2 ->
            invalid_append_rejected = 0;
            initialized = 1;
            max_bytes = MAX_BYTES;
            phase = 3
        :: phase == 3 ->
            storage[0] = 1;
            storage[1] = 2;
            storage[2] = 3;
            write_pos = 3;
            phase = 4
        :: phase == 4 ->
            read_pos = 2;
            phase = 5
        :: phase == 5 ->
            save_snapshot();
            /* Source offset 0 is outside the current unread range [2, 3). */
            assert(unread == 1);
            assert_snapshot_unchanged();
            consumed_alias_rejected = 1;
            phase = 6
        :: phase == 6 ->
            consumed_alias_rejected = 0;
            save_snapshot();
            /* view(buffer, NULL) returns EINVAL and leaves out untouched. */
            assert(output_data == 77);
            assert(output_size == 9);
            assert_snapshot_unchanged();
            invalid_view_rejected = 1;
            phase = 7
        :: phase == 7 ->
            invalid_view_rejected = 0;
            save_snapshot();
            /* append(NULL, 0) is TURBO_OK and does not mutate the buffer. */
            assert_snapshot_unchanged();
            null_zero_append_ok = 1;
            phase = 8
        :: phase == 8 ->
            null_zero_append_ok = 0;
            initialized = 0;
            max_bytes = 0;
            read_pos = 0;
            write_pos = 0;
            destroyed = 1;
            phase = FINAL_PHASE
        fi
    :: phase == FINAL_PHASE ->
        assert(destroyed == 1);
        assert(initialized == 0);
        assert(max_bytes == 0);
        assert(read_pos == 0 && write_pos == 0);
        break
    od
}

init {
    atomic {
        run Owner()
    }
}

ltl invalid_append_preserves_state {
    [] (invalid_append_rejected -> (initialized == 0 && unread == 0))
}

ltl consumed_alias_is_rejected {
    [] (consumed_alias_rejected -> (unread == 1 && read_pos == 2))
}

ltl invalid_view_preserves_output {
    [] (invalid_view_rejected -> (output_data == 77 && output_size == 9))
}

ltl null_zero_append_is_noop {
    [] (null_zero_append_ok -> (unread == 1 && read_pos == 2))
}

ltl invalid_paths_eventually_destroy {
    [] (null_zero_append_ok -> <> destroyed)
}
