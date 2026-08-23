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
}
