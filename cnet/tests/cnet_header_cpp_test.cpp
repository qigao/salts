#include <cnet/cnet.h>
#include <cnet/websocket.h>

#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout<cnet_client>::value, "client must be a C value wrapper");
static_assert(std::is_standard_layout<cnet_connection>::value,
              "connection must be a C value handle");
static_assert(std::is_standard_layout<cnet_listener>::value, "listener must be a C value wrapper");
static_assert(std::is_standard_layout<cnet_tls_server>::value,
              "TLS server must be a C value wrapper");
static_assert(std::is_standard_layout<cnet_tls_client>::value,
              "TLS client must be a C value wrapper");
static_assert(std::is_standard_layout<cnet_websocket>::value,
              "WebSocket must be a C value wrapper");
static_assert(CNET_CONNECTION_CONNECTED != CNET_CONNECTION_FAILED,
              "connection states must remain distinct");
static_assert(CNET_MESSAGE_BYTES != CNET_MESSAGE_DATAGRAM,
              "stream and datagram receive values must remain distinct");
static_assert(offsetof(cnet_observer, on_send) > offsetof(cnet_observer, user),
              "send completion must remain appended after legacy observer fields");
using cnet_client_wake_function = int (*)(cnet_client *);
static_assert(std::is_same<decltype(&cnet_client_wake), cnet_client_wake_function>::value,
              "client wake must keep its C linkage signature");

int main() {
  cnet_client client{};
  cnet_listener listener{};
  cnet_tls_server tls_server{};
  cnet_tls_client tls_client{};
  cnet_connection connection{};
  cnet_websocket websocket{};
  cnet_client_config config{};
  cnet_listener_config listener_config{};
  cnet_tls_client_config tls_client_config{};
  cnet_tls_server_config tls_server_config{};
  cnet_connect_options options{};
  cnet_receive_view view{};
  cnet_error error{};
  cnet_websocket_config websocket_config{};
  (void)client;
  (void)listener;
  (void)tls_server;
  (void)tls_client;
  (void)connection;
  (void)websocket;
  (void)config;
  (void)listener_config;
  (void)tls_client_config;
  (void)tls_server_config;
  (void)options;
  (void)view;
  (void)error;
  (void)websocket_config;
  return 0;
}
