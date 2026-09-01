#include <cnet/cnet.h>

#include <type_traits>

static_assert(std::is_standard_layout<cnet_client>::value, "client must be a C value wrapper");
static_assert(std::is_standard_layout<cnet_connection>::value,
              "connection must be a C value handle");
static_assert(CNET_CONNECTION_CONNECTED != CNET_CONNECTION_FAILED,
              "connection states must remain distinct");
static_assert(CNET_MESSAGE_BYTES != CNET_MESSAGE_DATAGRAM,
              "stream and datagram receive values must remain distinct");

int main() {
  cnet_client client{};
  cnet_connection connection{};
  cnet_client_config config{};
  cnet_connect_options options{};
  cnet_receive_view view{};
  cnet_error error{};
  (void)client;
  (void)connection;
  (void)config;
  (void)options;
  (void)view;
  (void)error;
  return 0;
}
