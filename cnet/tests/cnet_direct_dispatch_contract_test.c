#include "cnet_client_internal.h"
#include "tinytest.h"

#include <cnet/cnet.h>
#include <salts/error_codes.h>

spec("CNet diagnostic direct dispatch") {
  it("keeps direct dispatch private, explicit, and reversible") {
    cnet_client client = {0};
    cnet_client_config config = {
        .backend =
#if defined(_WIN32)
            NATIVE_IO_BACKEND_IOCP,
#elif defined(__linux__)
            NATIVE_IO_BACKEND_EPOLL,
#else
            NATIVE_IO_BACKEND_KQUEUE,
#endif
        .connection_capacity = 2u,
        .command_capacity = 8u,
        .request_capacity = 4u,
        .completion_batch_capacity = 4u,
        .event_capacity = 8u,
        .max_send_bytes = 256u,
        .receive_buffer_bytes = 256u,
    };

    check_equal(cnet_client_set_diagnostic_direct_dispatch(NULL, true), SALTS_EINVAL);
    check_equal(cnet_client_init(&client, &config), SALTS_OK);
    check_equal(cnet_client_set_diagnostic_direct_dispatch(&client, true), SALTS_OK);
    check_equal(cnet_client_set_diagnostic_direct_dispatch(&client, false), SALTS_OK);
    check_equal(cnet_client_stop(&client, 5000u), SALTS_OK);
    check_equal(cnet_client_destroy(&client), SALTS_OK);
  }
}
