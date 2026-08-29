#include "io_driver_internal.h"

#include "tinytest.h"

typedef struct driver_probe {
    cflow_io_actor *actor;
    cflow_io_request_id request_id;
    cflow_io_lease_id lease_id;
    void *operation_user;
    cflow_io_request_id cancelled;
    int submit_status;
    int cancel_status;
} driver_probe;

static int driver_probe_submit(void *user, cflow_io_actor *actor,
                               cflow_io_request_id request_id,
                               cflow_io_lease_id lease_id,
                               void *operation_user) {
    driver_probe *probe = (driver_probe *)user;
    probe->actor = actor;
    probe->request_id = request_id;
    probe->lease_id = lease_id;
    probe->operation_user = operation_user;
    return probe->submit_status;
}

static int driver_probe_cancel(void *user,
                               cflow_io_request_id request_id) {
    driver_probe *probe = (driver_probe *)user;
    probe->cancelled = request_id;
    return probe->cancel_status;
}

spec("CFlow IO driver bridge") {
    it("reports an uninitialized driver as invalid") {
        const cflow_io_driver driver = {0};

        check_equal(cflow_io_driver_kind_of(&driver),
                    CFLOW_IO_DRIVER_INVALID);
        check_equal(cflow_io_driver_kind_of(NULL),
                    CFLOW_IO_DRIVER_INVALID);
    }

    it("keeps readiness completion and blocking adapters distinct") {
        static const cflow_io_driver_ops probe_ops = {
            driver_probe_submit,
            driver_probe_cancel
        };
        const cflow_io_driver_kind kinds[] = {
            CFLOW_IO_DRIVER_READINESS,
            CFLOW_IO_DRIVER_COMPLETION,
            CFLOW_IO_DRIVER_BLOCKING
        };
        size_t index;

        for (index = 0u; index < sizeof(kinds) / sizeof(kinds[0]); ++index) {
            driver_probe probe = {0};
            cflow_io_driver driver = {0};

            check_true(cflow_io_driver_init_adapter(
                &driver, kinds[index], &probe_ops, &probe));
            check_equal(cflow_io_driver_kind_of(&driver), kinds[index]);
        }
    }

    it("adapts public backend callbacks as a completion driver") {
        driver_probe probe = {0};
        cflow_io_actor actor = {0};
        cflow_io_driver driver = {0};
        int operation = 29;

        probe.submit_status = -41;
        check_true(cflow_io_driver_init_completion_callbacks(
            &driver,
            (cflow_io_backend_ops){driver_probe_submit,
                                   driver_probe_cancel},
            &probe));
        check_equal(cflow_io_driver_kind_of(&driver),
                    CFLOW_IO_DRIVER_COMPLETION);
        check_equal(cflow_io_driver_submit(
                        &driver, &actor, 17u, 13u, &operation),
                    -41);
        check_equal((const void *)probe.actor, (const void *)&actor);
        check_equal(probe.request_id, (cflow_io_request_id)17u);
        check_equal(probe.lease_id, (cflow_io_lease_id)13u);
        check_equal((const void *)probe.operation_user,
                    (const void *)&operation);
    }

    it("preserves completion callback cancellation status") {
        driver_probe probe = {0};
        cflow_io_driver driver = {0};

        probe.cancel_status = -73;
        check_true(cflow_io_driver_init_completion_callbacks(
            &driver,
            (cflow_io_backend_ops){driver_probe_submit,
                                   driver_probe_cancel},
            &probe));
        check_equal(cflow_io_driver_cancel(&driver, 31u), -73);
        check_equal(probe.cancelled, (cflow_io_request_id)31u);
    }

    it("treats an absent completion cancellation callback as best effort") {
        driver_probe probe = {0};
        cflow_io_driver driver = {0};

        check_true(cflow_io_driver_init_completion_callbacks(
            &driver,
            (cflow_io_backend_ops){driver_probe_submit, NULL},
            &probe));
        check_equal(cflow_io_driver_cancel(&driver, 37u), 0);
        check_equal(probe.cancelled, (cflow_io_request_id)0u);
    }
}
