/*
 * Salts SPSC byte-ring protocol model.
 *
 * Scope:
 *   - one producer and one consumer
 *   - bounded byte occupancy
 *   - write/read acquire and release
 *   - partial reads
 *   - wrap marker and wrap-around writes
 *   - FIFO payload preservation
 *
 * The ring uses monotonically increasing logical cursors.  A wrapped write
 * reserves the tail gap and publishes a wrap marker; the consumer skips that
 * gap before acquiring the next contiguous data region.
 */

#define CAPACITY 4
#define TOTAL_BYTES 8
#define MAX_READ 2

byte storage[CAPACITY];
byte write_committed = 0;
byte read_committed = 0;
byte next_payload = 1;
byte expected_payload = 1;
byte produced_bytes = 0;
byte consumed_bytes = 0;

byte write_claimed = 0;
byte write_claim_start = 0;
byte write_claim_len = 0;
byte write_claim_gap = 0;

byte wrap_pending = 0;
byte wrap_pos = 0;
byte wrap_len = 0;
byte wrap_events = 0;

byte read_claimed = 0;
byte read_claim_start = 0;
byte read_claim_len = 0;

byte producer_done = 0;
byte consumer_done = 0;

#define free_bytes (CAPACITY - (write_committed - read_committed))
#define write_contiguous (CAPACITY - (write_committed % CAPACITY))
#define read_contiguous (CAPACITY - (read_committed % CAPACITY))

proctype Producer() {
    do
    :: write_claimed == 0 && produced_bytes == 0 &&
       free_bytes >= 3 ->
        atomic {
            write_claim_len = 3;
            if
            :: write_claim_len <= write_contiguous ->
                write_claim_gap = 0;
                write_claim_start = write_committed % CAPACITY
            :: wrap_pending == 0 &&
               free_bytes >= write_contiguous + write_claim_len ->
                write_claim_gap = write_contiguous;
                write_claim_start = 0
            fi;
            write_claimed = 1;
        }
    :: write_claimed == 0 && produced_bytes == 3 &&
       free_bytes >= 2 &&
       (2 <= write_contiguous ||
        (wrap_pending == 0 && free_bytes >= write_contiguous + 2)) ->
        atomic {
            write_claim_len = 2;
            if
            :: write_claim_len <= write_contiguous ->
                write_claim_gap = 0;
                write_claim_start = write_committed % CAPACITY
            :: wrap_pending == 0 &&
               free_bytes >= write_contiguous + write_claim_len ->
                write_claim_gap = write_contiguous;
                write_claim_start = 0
            fi;
            write_claimed = 1;
        }
    :: write_claimed == 0 && produced_bytes == 5 && free_bytes >= 1 ->
        atomic {
            write_claim_len = 1;
            if
            :: write_claim_len <= write_contiguous ->
                write_claim_gap = 0;
                write_claim_start = write_committed % CAPACITY
            :: wrap_pending == 0 &&
               free_bytes >= write_contiguous + write_claim_len ->
                write_claim_gap = write_contiguous;
                write_claim_start = 0
            fi;
            write_claimed = 1;
        }
    :: write_claimed == 0 && produced_bytes == 6 &&
       free_bytes >= 2 &&
       (2 <= write_contiguous ||
        (wrap_pending == 0 && free_bytes >= write_contiguous + 2)) ->
        atomic {
            write_claim_len = 2;
            if
            :: write_claim_len <= write_contiguous ->
                write_claim_gap = 0;
                write_claim_start = write_committed % CAPACITY
            :: wrap_pending == 0 &&
               free_bytes >= write_contiguous + write_claim_len ->
                write_claim_gap = write_contiguous;
                write_claim_start = 0
            fi;
            write_claimed = 1;
        }
    :: write_claimed == 1 ->
        atomic {
            if
            :: write_claim_len == 1 ->
                storage[write_claim_start] = next_payload
            :: write_claim_len == 2 ->
                storage[write_claim_start] = next_payload;
                storage[(write_claim_start + 1) % CAPACITY] = next_payload + 1
            :: write_claim_len == 3 ->
                storage[write_claim_start] = next_payload;
                storage[(write_claim_start + 1) % CAPACITY] = next_payload + 1;
                storage[(write_claim_start + 2) % CAPACITY] = next_payload + 2
            fi;

            if
            :: write_claim_gap > 0 ->
                wrap_pending = 1;
                wrap_pos = write_committed;
                wrap_len = write_claim_gap;
                wrap_events++
            :: else -> skip
            fi;

            write_committed = write_committed + write_claim_gap + write_claim_len;
            next_payload = next_payload + write_claim_len;
            produced_bytes = produced_bytes + write_claim_len;
            write_claimed = 0;
        }
        progress_write_release: skip;
    :: write_claimed == 0 && produced_bytes == TOTAL_BYTES ->
        producer_done = 1;
        break
    od
}

proctype Consumer() {
    byte available;

    do
    :: read_claimed == 0 && wrap_pending == 1 &&
       read_committed == wrap_pos ->
        atomic {
            read_committed = read_committed + wrap_len;
            wrap_pending = 0;
            wrap_len = 0;
        }
        progress_wrap_skip: skip;
    :: read_claimed == 0 && read_committed < write_committed &&
       !(wrap_pending == 1 && read_committed == wrap_pos) ->
        atomic {
            read_claim_start = read_committed % CAPACITY;
            if
            :: wrap_pending == 1 && read_committed < wrap_pos ->
                available = wrap_pos - read_committed
            :: else ->
                available = write_committed - read_committed
            fi;
            if
            :: available >= 2 &&
               read_contiguous >= 2 ->
                read_claim_len = MAX_READ
            :: else ->
                read_claim_len = 1
            fi;
            read_claimed = 1;
        }
    :: read_claimed == 1 ->
        atomic {
            assert(storage[read_claim_start] == expected_payload);
            expected_payload++;
            consumed_bytes++;
            read_committed++;

            if
            :: read_claim_len == 2 ->
                assert(storage[(read_claim_start + 1) % CAPACITY] == expected_payload);
                expected_payload++;
                consumed_bytes++;
                read_committed++
            :: else -> skip
            fi;
            read_claimed = 0;
        }
        progress_read_release: skip;
    :: producer_done == 1 && consumed_bytes == TOTAL_BYTES &&
       read_claimed == 0 && wrap_pending == 0 &&
       read_committed == write_committed ->
        consumer_done = 1;
        break
    od
}

proctype Owner() {
    do
    :: consumer_done == 1 ->
        assert(produced_bytes == TOTAL_BYTES);
        assert(consumed_bytes == TOTAL_BYTES);
        assert(expected_payload == next_payload);
        assert(write_committed == read_committed);
        assert(wrap_events > 0);
        break
    od
}

init {
    atomic {
        run Producer();
        run Consumer();
        run Owner();
    }
}

ltl ring_never_overruns {
    [] (write_committed - read_committed <= CAPACITY)
}

ltl ring_never_reads_unpublished_bytes {
    [] (read_committed <= write_committed)
}

ltl every_write_claim_is_released {
    [] (write_claimed -> <> (!write_claimed))
}

ltl fifo_payload_is_preserved {
    [] (consumed_bytes <= produced_bytes)
}

ltl ring_eventually_drains {
    [] (producer_done -> <> consumer_done)
}
