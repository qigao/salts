#include "cnet_module.h"
#include "cnet_shards.h"
#include "tinytest.h"

spec("CNet private multi-owner shards") {
  it("keeps experimental multi-owner progress behind the diagnostic CNet target") {
    cnet_shards shards = {0};
    const cnet_shards_config config = {.backend_kind =
#if defined(_WIN32)
                                           NATIVE_IO_BACKEND_IOCP,
#elif defined(__linux__)
                                           NATIVE_IO_BACKEND_EPOLL,
#else
                                           NATIVE_IO_BACKEND_KQUEUE,
#endif
                                       .shard_count = 2u,
                                       .connection_capacity_per_shard = 1u,
                                       .command_capacity_per_shard = 8u,
                                       .request_capacity_per_shard = 4u,
                                       .completion_batch_capacity = 4u,
                                       .event_capacity_per_shard = 8u,
                                       .receive_buffer_bytes = 64u,
                                       .max_command_payload_bytes =
                                           sizeof(cnet_owner_connect_payload)};
    cnet_shards_layout layout = {0};

#if !defined(CNET_INTERNAL_MULTI_OWNER_POC)
    check_true(false);
#else
    check_equal(cnet_module_init(), SALTS_OK);
    check_equal(cnet_shards_init(&shards, &config), SALTS_EINVAL);
    check_null(shards.impl);

    check_equal(cnet_shards_init_multi_owner_experimental(&shards, &config),
                SALTS_OK);
    check_true(cnet_shards_get_layout(&shards, &layout));
    check_equal(layout.shard_count, (size_t)2u);
    check_equal(layout.connection_capacity_per_shard, (size_t)1u);

    check_equal(cnet_shards_poll(&shards, 0u), SALTS_EINVAL);
    check_equal(cnet_shards_wake(&shards), SALTS_EINVAL);
    check_equal(cnet_shards_poll_owner(&shards, 2u, 0u), SALTS_EINVAL);
    check_equal(cnet_shards_wake_owner(&shards, 2u), SALTS_EINVAL);
    check_equal(cnet_shards_poll_owner(&shards, 0u, 0u), SALTS_EBUSY);
    check_equal(cnet_shards_poll_owner(&shards, 1u, 0u), SALTS_EBUSY);

    check_equal(cnet_shards_init_owner_experimental(&shards, 0u), SALTS_OK);
    check_equal(cnet_shards_init_owner_experimental(&shards, 1u), SALTS_OK);
    check_equal(cnet_shards_init_owner_experimental(&shards, 1u), SALTS_EALREADY);
    check_equal(cnet_shards_poll_owner(&shards, 0u, 0u), SALTS_OK);
    check_equal(cnet_shards_poll_owner(&shards, 1u, 0u), SALTS_OK);
    check_equal(cnet_shards_wake_owner(&shards, 0u), SALTS_OK);
    check_equal(cnet_shards_wake_owner(&shards, 1u), SALTS_OK);

    check_equal(cnet_shards_stop(&shards, 5000u), SALTS_OK);
    check_equal(cnet_shards_destroy(&shards), SALTS_OK);
    check_equal(cnet_module_shutdown(), SALTS_OK);
#endif
  }
}
