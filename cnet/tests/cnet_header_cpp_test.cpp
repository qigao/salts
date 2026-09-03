#include <cnet/cnet.h>
#include <cnet/websocket.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(std::is_standard_layout<cnet_client>::value, "client must be a C value wrapper");
static_assert(std::is_standard_layout<cnet_connection>::value,
              "connection must be a C value handle");
static_assert(std::is_standard_layout<cnet_listener>::value, "listener must be a C value wrapper");
static_assert(std::is_standard_layout<cnet_datagram>::value,
              "datagram must be a C value wrapper");
static_assert(std::is_standard_layout<cnet_kcp>::value, "KCP must be a C value wrapper");
static_assert(std::is_standard_layout<cnet_secure_kcp>::value,
              "secure KCP must be a C value wrapper");
static_assert(std::is_standard_layout<cnet_packet_endpoint>::value,
              "packet endpoint must be a C value wrapper");
static_assert(std::is_standard_layout<cnet_packet_session>::value,
              "packet session must be a C value handle");
static_assert(std::is_standard_layout<cnet_tls_server>::value,
              "TLS server must be a C value wrapper");
static_assert(std::is_standard_layout<cnet_tls_client>::value,
              "TLS client must be a C value wrapper");
static_assert(std::is_standard_layout<cnet_const_buffer>::value,
              "send segments must remain C value descriptors");
static_assert(std::is_standard_layout<cnet_stream_socket_options>::value,
              "stream socket policy must remain C ABI data");
static_assert(std::is_standard_layout<cnet_listener_options>::value,
              "listener socket policy must remain C ABI data");
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
using cnet_packet_poll_function = int (*)(cnet_packet_endpoint *, std::uint32_t, std::size_t *);
static_assert(std::is_same<decltype(&cnet_packet_poll), cnet_packet_poll_function>::value,
              "packet poll must keep its C linkage signature");

int main() {
  cnet_client client{};
  cnet_listener listener{};
  cnet_datagram datagram{};
  cnet_kcp kcp{};
  cnet_secure_kcp secure_kcp{};
  cnet_packet_endpoint packet_endpoint{};
  cnet_packet_session packet_session{};
  cnet_packet_session_info packet_session_info{};
  cnet_tls_server tls_server{};
  cnet_tls_client tls_client{};
  cnet_connection connection{};
  cnet_websocket websocket{};
  cnet_client_config config{};
  cnet_listener_config listener_config{};
  cnet_stream_socket_options stream_socket_options = CNET_STREAM_SOCKET_OPTIONS_INIT;
  cnet_listener_options listener_options = CNET_LISTENER_OPTIONS_INIT;
  cnet_datagram_config datagram_config = CNET_DATAGRAM_CONFIG_INIT;
  cnet_kcp_config kcp_config = CNET_KCP_CONFIG_INIT;
  cnet_secure_kcp_config secure_kcp_config = CNET_SECURE_KCP_CONFIG_INIT;
  cnet_packet_endpoint_config packet_config = CNET_PACKET_ENDPOINT_CONFIG_INIT;
  cnet_tls_client_config tls_client_config{};
  cnet_tls_server_config tls_server_config{};
  cnet_connect_options options{};
  cnet_receive_view view{};
  cnet_const_buffer buffer{};
  cnet_error error{};
  cnet_websocket_config websocket_config{};
  (void)client;
  (void)listener;
  (void)datagram;
  (void)kcp;
  (void)secure_kcp;
  (void)packet_endpoint;
  (void)packet_session;
  (void)packet_session_info;
  (void)tls_server;
  (void)tls_client;
  (void)connection;
  (void)websocket;
  (void)config;
  (void)listener_config;
  (void)stream_socket_options;
  (void)listener_options;
  (void)datagram_config;
  (void)kcp_config;
  (void)secure_kcp_config;
  (void)packet_config;
  (void)tls_client_config;
  (void)tls_server_config;
  (void)options;
  (void)view;
  (void)buffer;
  (void)error;
  (void)websocket_config;
  return 0;
}
