#include <cflow/event.h>

#include "tinytest.h"

static bool owned_payload_copy(void *destination, const void *source) {
    if (destination == NULL || source == NULL) return false;
    *(int *)destination = *(const int *)source;
    return true;
}

static void owned_payload_destroy(void *value) {
    (void)value;
}

static const cmeta_type_traits owned_payload_traits = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY,
    NULL,
    NULL,
    NULL,
    owned_payload_copy,
    NULL,
    owned_payload_destroy
};

static const cmeta_type_desc owned_payload_type = {
    "owned_payload",
    sizeof(int),
    CMETA_ALIGNOF(int),
    CMETA_T_OBJECT,
    NULL,
    &owned_payload_traits,
    NULL
};

suite("CFlow typed event mailbox") {
    it("initializes a finite heterogeneous schema") {
        const cflow_event_type schema[] = {
            {1u, &cmeta_type_int},
            {2u, &cmeta_type_double}
        };
        cflow_mailbox mailbox = {0};

        check_equal(cflow_mailbox_init(
                        &mailbox, schema,
                        sizeof(schema) / sizeof(schema[0]), 4u),
                    CFLOW_MAILBOX_OK);
        check_equal(cflow_mailbox_payload_capacity(&mailbox), sizeof(double));

        cflow_mailbox_destroy(&mailbox);
        check_null(mailbox.impl);
    }

    it("rejects invalid schema and capacity without publishing state") {
        const cflow_event_type zero_id[] = {{0u, &cmeta_type_int}};
        const cflow_event_type duplicate_id[] = {
            {1u, &cmeta_type_int},
            {1u, &cmeta_type_double}
        };
        const cflow_event_type nontrivial[] = {{1u, &owned_payload_type}};
        cflow_mailbox mailbox = {0};

        check_equal(cflow_mailbox_init(&mailbox, zero_id, 1u, 1u),
                    CFLOW_MAILBOX_INVALID_ARGUMENT);
        check_null(mailbox.impl);
        check_equal(cflow_mailbox_init(&mailbox, duplicate_id, 2u, 1u),
                    CFLOW_MAILBOX_INVALID_ARGUMENT);
        check_null(mailbox.impl);
        check_equal(cflow_mailbox_init(&mailbox, nontrivial, 1u, 1u),
                    CFLOW_MAILBOX_INVALID_ARGUMENT);
        check_null(mailbox.impl);
        check_equal(cflow_mailbox_init(&mailbox, zero_id, 1u, 0u),
                    CFLOW_MAILBOX_INVALID_ARGUMENT);
        check_null(mailbox.impl);
    }

    it("admits and observes heterogeneous events in FIFO order") {
        const cflow_event_type schema[] = {
            {1u, &cmeta_type_int},
            {2u, &cmeta_type_double}
        };
        const int integer_payload = 17;
        const double double_payload = 3.5;
        const cflow_event_view integer_event = {
            1u, &cmeta_type_int, &integer_payload
        };
        const cflow_event_view double_event = {
            2u, &cmeta_type_double, &double_payload
        };
        const cflow_event_view mismatched_event = {
            1u, &cmeta_type_double, &double_payload
        };
        cflow_mailbox mailbox = {0};
        cflow_mailbox_stats stats = {0};
        cflow_event_id observed_id = 99u;
        const cmeta_type_desc *observed_type = &cmeta_type_double;
        int integer_output = -1;
        double double_output = -1.0;

        check_equal(cflow_mailbox_init(&mailbox, schema, 2u, 2u),
                    CFLOW_MAILBOX_OK);
        check_equal(cflow_mailbox_try_send(&mailbox, &mismatched_event),
                    CFLOW_MAILBOX_TYPE_MISMATCH);
        check_equal(cflow_mailbox_try_send(&mailbox, &integer_event),
                    CFLOW_MAILBOX_OK);
        check_equal(cflow_mailbox_try_send(&mailbox, &double_event),
                    CFLOW_MAILBOX_OK);
        check_equal(cflow_mailbox_try_send(&mailbox, &integer_event),
                    CFLOW_MAILBOX_FULL);

        check_equal(cflow_mailbox_try_receive(
                        &mailbox, &observed_id, &observed_type,
                        &integer_output, sizeof(integer_output) - 1u),
                    CFLOW_MAILBOX_BUFFER_TOO_SMALL);
        check_equal(observed_id, (cflow_event_id)0u);
        check_null(observed_type);
        check_equal(integer_output, -1);

        check_equal(cflow_mailbox_try_receive(
                        &mailbox, &observed_id, &observed_type,
                        &integer_output, sizeof(integer_output)),
                    CFLOW_MAILBOX_OK);
        check_equal(observed_id, (cflow_event_id)1u);
        check_true(observed_type == &cmeta_type_int);
        check_equal(integer_output, integer_payload);

        check_equal(cflow_mailbox_try_receive(
                        &mailbox, &observed_id, &observed_type,
                        &double_output, sizeof(double_output)),
                    CFLOW_MAILBOX_OK);
        check_equal(observed_id, (cflow_event_id)2u);
        check_true(observed_type == &cmeta_type_double);
        check_true(double_output == double_payload);

        check_equal(cflow_mailbox_try_receive(
                        &mailbox, &observed_id, &observed_type,
                        &integer_output, sizeof(integer_output)),
                    CFLOW_MAILBOX_EMPTY);
        check_equal(observed_id, (cflow_event_id)0u);
        check_null(observed_type);

        check_true(cflow_mailbox_get_stats(&mailbox, &stats));
        check_equal(stats.schema_count, (size_t)2u);
        check_equal(stats.capacity, (size_t)2u);
        check_equal(stats.pending, (size_t)0u);
        check_equal(stats.peak_pending, (size_t)2u);
        check_equal(stats.accepted, (uint64_t)2u);
        check_equal(stats.received, (uint64_t)2u);
        check_equal(stats.rejected_full, (uint64_t)1u);

        cflow_mailbox_destroy(&mailbox);
    }
}
