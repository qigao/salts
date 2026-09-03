/*
 * Salts buffer/slice ownership model.
 *
 * Scope:
 *   - pool-managed and externally wrapped buffers
 *   - retain/release balance for two consumers
 *   - mem_slice() retaining its source buffer
 *   - initial owner release after all handoffs are retained
 *   - exactly-once external free callback
 *   - pool destruction only after all buffers are quiescent
 *
 * Payload bytes are abstracted.  Slice bounds are still checked against the
 * used length so the model keeps the ownership and range invariants visible.
 */

#define BUFFER_COUNT 2
#define INTERNAL_BUFFER 0
#define EXTERNAL_BUFFER 1
#define CONSUMER_COUNT 2

byte pool_alive = 1;
byte pool_destroyed = 0;
byte buffer_alive[BUFFER_COUNT];
byte buffer_freed[BUFFER_COUNT];
byte buffer_external[BUFFER_COUNT];
byte buffer_ref_count[BUFFER_COUNT];
byte buffer_used[BUFFER_COUNT];

byte slice_live[BUFFER_COUNT];
byte slice_offset[BUFFER_COUNT];
byte slice_length[BUFFER_COUNT];
byte handoff_count[BUFFER_COUNT];
byte initial_released[BUFFER_COUNT];
byte retained_internal[CONSUMER_COUNT];
byte retained_external[CONSUMER_COUNT];
byte released_internal[CONSUMER_COUNT];
byte released_external[CONSUMER_COUNT];
byte external_free_calls[BUFFER_COUNT];

#define all_handoffs_ready \
    (handoff_count[0] == 3 && handoff_count[1] == 3)
#define all_consumers_released \
    (released_internal[0] && released_internal[1] && \
     released_external[0] && released_external[1])
#define all_buffers_freed (buffer_freed[0] && buffer_freed[1])

proctype BufferConsumer(byte owner) {
    byte buffer_id;
    byte consumer_id;

    atomic {
        buffer_id = owner / CONSUMER_COUNT;
        consumer_id = owner % CONSUMER_COUNT;
        assert(buffer_alive[buffer_id] == 1);
        if
        :: buffer_id == INTERNAL_BUFFER ->
            assert(retained_internal[consumer_id] == 0);
            retained_internal[consumer_id] = 1
        :: buffer_id == EXTERNAL_BUFFER ->
            assert(retained_external[consumer_id] == 0);
            retained_external[consumer_id] = 1
        fi;
        buffer_ref_count[buffer_id]++;
        handoff_count[buffer_id]++;
    }

    do
    :: initial_released[buffer_id] == 1 -> break
    od;

    atomic {
        assert(buffer_alive[buffer_id] == 1);
        assert(buffer_ref_count[buffer_id] > 0);
        if
        :: buffer_id == INTERNAL_BUFFER ->
            assert(released_internal[consumer_id] == 0);
            released_internal[consumer_id] = 1
        :: buffer_id == EXTERNAL_BUFFER ->
            assert(released_external[consumer_id] == 0);
            released_external[consumer_id] = 1
        fi;
        buffer_ref_count[buffer_id]--;
    }
}

proctype SliceOwner(byte buffer_id) {
    atomic {
        assert(buffer_alive[buffer_id] == 1);
        assert(slice_live[buffer_id] == 0);
        assert(slice_offset[buffer_id] + slice_length[buffer_id] <=
               buffer_used[buffer_id]);
        buffer_ref_count[buffer_id]++;
        slice_live[buffer_id] = 1;
        handoff_count[buffer_id]++;
    }

    do
    :: initial_released[buffer_id] == 1 -> break
    od;

    atomic {
        assert(buffer_alive[buffer_id] == 1);
        assert(slice_live[buffer_id] == 1);
        assert(buffer_ref_count[buffer_id] > 0);
        slice_live[buffer_id] = 0;
        buffer_ref_count[buffer_id]--;
    }
}

proctype InitialOwner(byte buffer_id) {
    do
    :: handoff_count[buffer_id] == 3 -> break
    od;

    atomic {
        assert(buffer_alive[buffer_id] == 1);
        assert(buffer_ref_count[buffer_id] > 0);
        buffer_ref_count[buffer_id]--;
        initial_released[buffer_id] = 1;
    }
}

proctype Cleanup() {
    do
    :: buffer_alive[INTERNAL_BUFFER] == 1 &&
       buffer_ref_count[INTERNAL_BUFFER] == 0 ->
        atomic {
            assert(slice_live[INTERNAL_BUFFER] == 0);
            assert(released_internal[0] == 1);
            assert(released_internal[1] == 1);
            buffer_alive[INTERNAL_BUFFER] = 0;
            buffer_freed[INTERNAL_BUFFER] = 1;
        }
    :: buffer_alive[EXTERNAL_BUFFER] == 1 &&
       buffer_ref_count[EXTERNAL_BUFFER] == 0 ->
        atomic {
            assert(slice_live[EXTERNAL_BUFFER] == 0);
            assert(released_external[0] == 1);
            assert(released_external[1] == 1);
            buffer_alive[EXTERNAL_BUFFER] = 0;
            buffer_freed[EXTERNAL_BUFFER] = 1;
            assert(external_free_calls[EXTERNAL_BUFFER] == 0);
            external_free_calls[EXTERNAL_BUFFER]++;
        }
    :: all_buffers_freed ->
        break
    od
}

proctype PoolOwner() {
    do
    :: all_buffers_freed ->
        atomic {
            assert(pool_alive == 1);
            assert(buffer_ref_count[INTERNAL_BUFFER] == 0);
            assert(buffer_ref_count[EXTERNAL_BUFFER] == 0);
            assert(slice_live[INTERNAL_BUFFER] == 0);
            assert(slice_live[EXTERNAL_BUFFER] == 0);
            pool_alive = 0;
            pool_destroyed = 1;
        }
        break
    od
}

init {
    atomic {
        buffer_alive[INTERNAL_BUFFER] = 1;
        buffer_alive[EXTERNAL_BUFFER] = 1;
        buffer_external[INTERNAL_BUFFER] = 0;
        buffer_external[EXTERNAL_BUFFER] = 1;
        buffer_ref_count[INTERNAL_BUFFER] = 1;
        buffer_ref_count[EXTERNAL_BUFFER] = 1;
        buffer_used[INTERNAL_BUFFER] = 8;
        buffer_used[EXTERNAL_BUFFER] = 8;
        slice_offset[INTERNAL_BUFFER] = 2;
        slice_offset[EXTERNAL_BUFFER] = 2;
        slice_length[INTERNAL_BUFFER] = 3;
        slice_length[EXTERNAL_BUFFER] = 3;

        run BufferConsumer(0);
        run BufferConsumer(1);
        run SliceOwner(INTERNAL_BUFFER);
        run InitialOwner(INTERNAL_BUFFER);

        run BufferConsumer(2);
        run BufferConsumer(3);
        run SliceOwner(EXTERNAL_BUFFER);
        run InitialOwner(EXTERNAL_BUFFER);

        run Cleanup();
        run PoolOwner();
    }
}

ltl no_free_while_referenced {
    [] ((buffer_ref_count[0] > 0 -> !buffer_freed[0]) &&
        (buffer_ref_count[1] > 0 -> !buffer_freed[1]))
}

ltl slice_keeps_source_alive {
    [] ((slice_live[0] -> buffer_alive[0]) &&
        (slice_live[1] -> buffer_alive[1]))
}

ltl external_callback_once {
    [] (external_free_calls[EXTERNAL_BUFFER] <= 1)
}

ltl handoffs_eventually_release {
    [] (all_handoffs_ready -> <> all_consumers_released)
}

ltl pool_destroy_is_quiescent {
    [] (pool_destroyed ->
        (all_buffers_freed &&
         buffer_ref_count[0] == 0 && buffer_ref_count[1] == 0 &&
         slice_live[0] == 0 && slice_live[1] == 0))
}
