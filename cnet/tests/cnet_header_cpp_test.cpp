#include <cnet/cnet.h>
#include <cnet/name_lookup.h>
#include <cnet/manager.h>
#include <cnet/handoff.h>
#include <cnet/owner_placement.h>
#include <cnet/destination_policy.h>
#include <cnet/client_pool.h>
#include <cnet/recovery_policy.h>
#include <cnet/managed_dial.h>
#include <cnet/sg_host.h>
static_assert(CNET_MANAGER_VERSION == 1u, "manager capability is versioned");
#include <cnet/ipc.h>
#include <cnet/websocket.h>
#include <cnet/websocket_transport.h>
#include <tinytest.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(std::is_standard_layout<cnet_sg_host_routes>::value,
              "SG host route table is a stable C descriptor");
static_assert(CNET_SG_HOST_ROUTING_VERSION == 1u,
              "SG host routing is a versioned C ABI");
static_assert(std::is_standard_layout<cnet_managed_dial_config>::value,
              "Managed dial config is C ABI");
static_assert(std::is_standard_layout<cnet_managed_dial_snapshot>::value,
              "Managed dial observation is C ABI");
static_assert(CNET_MANAGED_DIAL_VERSION == 1u, "Managed dial has versioned ABI");
static_assert(std::is_standard_layout<cnet_reconnect_state>::value,
              "Reconnect descriptor remains pure owner-owned C data");
static_assert(std::is_standard_layout<cnet_retry_input>::value,
              "Protocol replay authorization uses a C descriptor");
static_assert(std::is_same<decltype(&cnet_retry_evaluate),
                           int (*)(const cnet_retry_input *, cnet_retry_result *)>::value,
              "Retry gate preserves C ABI/linkage");
static_assert(CNET_RECOVERY_POLICY_VERSION == 1u, "Recovery contract is versioned");
static_assert(std::is_standard_layout<cnet_pool_key>::value,
              "Client pool key must have stable C layout");
static_assert(std::is_standard_layout<cnet_pool_connection>::value,
              "Client pool physical identity must be C ABI data");
static_assert(std::is_standard_layout<cnet_pool_lease>::value,
              "Protocol lease is a generation-safe C value");
static_assert(std::is_same<decltype(&cnet_pool_try_acquire),
                           int (*)(cnet_client_pool *, const cnet_pool_key *,
                                   const cnet_pool_protocol_ops *, cnet_pool_lease *,
                                   cnet_managed_connection *)>::value,
              "Client pool acquire uses C linkage");
static_assert(CNET_CLIENT_POOL_VERSION == 1u, "Client pool ABI is versioned");
static_assert(std::is_standard_layout<cnet_destination_selection>::value,
              "client destination selection is C ABI data");
static_assert(std::is_standard_layout<cnet_destination_result>::value,
              "selected remote destination has stable copied identity");
static_assert(CNET_DESTINATION_POLICY_VERSION == 1u, "destination ABI is versioned");
static_assert(std::is_same<decltype(&cnet_destination_choose),
                           int (*)(const cnet_destination_selection *,
                                   cnet_destination_result *)>::value,
              "client remote policy has C linkage");
static_assert(std::is_standard_layout<cnet_owner_placement_input>::value,
              "server placement input is C ABI data");
static_assert(CNET_OWNER_PLACEMENT_VERSION == 1u, "placement contract is versioned");
static_assert(std::is_same<decltype(&cnet_owner_placement_choose),
                           int (*)(const cnet_owner_placement_input *, size_t *)>::value,
              "server placement decision has C linkage");
static_assert(std::is_standard_layout<cnet_handoff_ticket>::value,
              "handoff ticket must remain C ABI data");
static_assert(CNET_HANDOFF_VERSION == 1u, "handoff capability is versioned");

static_assert(std::is_standard_layout<cnet_ipc_accepted>::value,
              "IPC handoff must remain typed C ABI data");
static_assert(CNET_IPC_VERSION == 1u && CNET_DATAGRAM_EXTERNAL_PROGRESS_VERSION == 1u &&
              CNET_WEBSOCKET_TAGGED_SEND_VERSION == 1u, "optional transport capabilities");

static_assert(std::is_standard_layout<cnet_client>::value, "client must be a C value wrapper");
static_assert(std::is_standard_layout<cnet_connection>::value,
              "connection must be a C value handle");
static_assert(std::is_standard_layout<cnet_listener>::value, "listener must be a C value wrapper");
static_assert(std::is_standard_layout<cnet_accepted_stream>::value,
              "accepted stream handoff must remain C ABI data");
static_assert(std::is_standard_layout<cnet_datagram>::value,
              "datagram must be a C value wrapper");
#if !defined(CNET_STOP_DRAIN_CONTRACT_VERSION) || CNET_STOP_DRAIN_CONTRACT_VERSION < 1u
  #error "CNet C++ consumers require stop-drain contract v1"
#endif
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
static_assert(CNET_RETAINED_VECTOR_MAX >= 32u,
              "retained logical vector capacity must cover 32 ranges");
static_assert(CNET_RETAINED_VECTOR_MAX >= NATIVE_IO_VECTOR_MAX,
              "CNet logical vector capacity must cover one NativeIO window");
static_assert(std::is_standard_layout<cnet_stream_socket_options>::value,
              "stream socket policy must remain C ABI data");
static_assert(std::is_same<decltype(cnet_stream_socket_options::nodelay), int>::value,
              "TCP_NODELAY policy must remain a C int flag");
static_assert(std::is_standard_layout<cnet_listener_options>::value,
              "listener socket policy must remain C ABI data");
static_assert(std::is_standard_layout<cnet_vsock_peer>::value,
              "VSOCK peer must remain portable C ABI data");
static_assert(std::is_standard_layout<cnet_vsock_listener_config>::value,
              "VSOCK listener configuration must remain versioned C ABI data");
static_assert(std::is_standard_layout<cnet_start_tls_options>::value,
              "TLS upgrade policy must remain C ABI data");
static_assert(std::is_standard_layout<cnet_websocket>::value,
              "WebSocket must be a C value wrapper");
static_assert(std::is_standard_layout<cnet_websocket_config>::value,
              "WebSocket config must remain C ABI data");
static_assert(CNET_CONNECTION_CONNECTED != CNET_CONNECTION_FAILED,
              "connection states must remain distinct");
static_assert(CNET_MESSAGE_BYTES != CNET_MESSAGE_DATAGRAM,
              "stream and datagram receive values must remain distinct");
static_assert(CNET_VSOCK_CID_LOCAL == std::uint32_t{1}, "local VSOCK CID must be portable");
static_assert(CNET_VSOCK_CID_HOST == std::uint32_t{2}, "host VSOCK CID must be portable");
static_assert(CNET_VSOCK_CID_ANY == UINT32_MAX, "VSOCK any CID must retain full width");
static_assert(CNET_VSOCK_PORT_ANY == UINT32_MAX, "VSOCK any port must retain full width");
static_assert(std::is_same<decltype(&cnet_client_adopt_vsock),
                           int (*)(cnet_client *, uintptr_t, const cnet_observer *,
                                   cnet_connection *)>::value,
              "VSOCK adoption must expose the C client ownership contract");
static_assert(
    std::is_same<decltype(&cnet_listener_accept_detached),
                 int (*)(cnet_listener *, cnet_accepted_stream *)>::value,
    "detached listener admission must keep C linkage");
static_assert(
    std::is_same<decltype(&cnet_client_adopt_accepted),
                 int (*)(cnet_client *, cnet_accepted_stream *,
                         const cnet_observer *, cnet_connection *)>::value,
    "accepted TCP adoption must keep C linkage");
static_assert(
    std::is_same<decltype(&cnet_client_adopt_accepted_tls),
                 int (*)(cnet_client *, cnet_accepted_stream *,
                         const cnet_tls_server *, const cnet_observer *,
                         cnet_connection *)>::value,
    "accepted TLS adoption must keep C linkage");
static_assert(std::is_same<decltype(&cnet_listener_init_vsock),
                           int (*)(cnet_listener *, const cnet_vsock_listener_config *)>::value,
              "VSOCK listener initialization must keep C linkage");
static_assert(std::is_same<decltype(&cnet_listener_vsock_local),
                           int (*)(const cnet_listener *, cnet_vsock_peer *)>::value,
              "VSOCK local query must return full-width copied address data");
static_assert(std::is_same<decltype(&cnet_listener_accept_vsock_peer),
                           int (*)(cnet_listener *, cnet_client *, const cnet_observer *,
                                   cnet_connection *, cnet_vsock_peer *)>::value,
              "VSOCK accept must expose peer metadata without widening TCP types");
static_assert(offsetof(cnet_observer, on_send) > offsetof(cnet_observer, user),
              "send completion must remain appended after legacy observer fields");
using cnet_client_wake_function = int (*)(cnet_client *);
static_assert(std::is_same<decltype(&cnet_client_wake), cnet_client_wake_function>::value,
              "client wake must keep its C linkage signature");
using cnet_start_tls_function = int (*)(cnet_client *, cnet_connection,
                                        const cnet_start_tls_options *);
static_assert(std::is_same<decltype(&cnet_start_tls), cnet_start_tls_function>::value,
              "TLS upgrade must keep its C linkage signature");
using cnet_receive_slice_handler_function =
    int (*)(cnet_client *, cnet_connection, cnet_receive_slice_fn, void *);
static_assert(
    std::is_same<decltype(&cnet_set_receive_slice_handler),
                 cnet_receive_slice_handler_function>::value,
    "owned receive handler setter must keep its additive C linkage signature");
using cnet_owned_receive_callback =
    void (*)(void *, cnet_connection, mem_slice_t, cnet_message_kind);
static_assert(std::is_same<cnet_receive_slice_fn, cnet_owned_receive_callback>::value,
              "owned receive callback must transfer a C value slice descriptor");
using cnet_send_buffer_function = int (*)(cnet_client *, cnet_connection, mem_buffer_t *);
static_assert(std::is_same<decltype(&cnet_send_buffer), cnet_send_buffer_function>::value,
              "retained-buffer send must keep its C linkage signature");
using cnet_send_buffer_and_close_function =
    int (*)(cnet_client *, cnet_connection, mem_buffer_t *);
static_assert(
    std::is_same<decltype(&cnet_send_buffer_and_close),
                 cnet_send_buffer_and_close_function>::value,
    "retained final-send must keep its C linkage signature");
using cnet_send_slice_function =
    int (*)(cnet_client *, cnet_connection, const mem_slice_t *);
static_assert(std::is_same<decltype(&cnet_send_slice), cnet_send_slice_function>::value,
              "retained-slice send must keep its C linkage signature");
using cnet_send_slicev_function =
    int (*)(cnet_client *, cnet_connection, const mem_slice_t *, std::size_t);
static_assert(std::is_same<decltype(&cnet_send_slicev), cnet_send_slicev_function>::value,
              "retained-vector send must keep its C linkage signature");
using cnet_send_slicev_and_close_function =
    int (*)(cnet_client *, cnet_connection, const mem_slice_t *, std::size_t);
static_assert(
    std::is_same<decltype(&cnet_send_slicev_and_close),
                 cnet_send_slicev_and_close_function>::value,
    "retained final-vector send must keep its C linkage signature");
using cnet_websocket_write_complete_function =
    int (*)(cnet_websocket *, std::size_t, int);
static_assert(
    std::is_same<decltype(&cnet_websocket_write_complete),
                 cnet_websocket_write_complete_function>::value,
    "retained WebSocket terminal acknowledgement must keep its C linkage signature");
using cnet_packet_poll_function = int (*)(cnet_packet_endpoint *, std::uint32_t, std::size_t *);
static_assert(std::is_same<decltype(&cnet_packet_poll), cnet_packet_poll_function>::value,
              "packet poll must keep its C linkage signature");

suite("CNet C++ headers") {
  group("aggregate initialization") {
    it("accepts public C values and default initializers") {
      cnet_client client{};
      cnet_listener listener{};
      cnet_accepted_stream accepted_stream = CNET_ACCEPTED_STREAM_INIT;
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
      cnet_vsock_listener_config vsock_listener_config = CNET_VSOCK_LISTENER_CONFIG_INIT;
      cnet_vsock_peer vsock_peer{};
      cnet_datagram_config datagram_config = CNET_DATAGRAM_CONFIG_INIT;
      cnet_kcp_config kcp_config = CNET_KCP_CONFIG_INIT;
      cnet_secure_kcp_config secure_kcp_config = CNET_SECURE_KCP_CONFIG_INIT;
      cnet_packet_endpoint_config packet_config = CNET_PACKET_ENDPOINT_CONFIG_INIT;
      cnet_tls_client_config tls_client_config{};
      cnet_tls_server_config tls_server_config{};
      cnet_connect_options options{};
      cnet_start_tls_options start_tls_options = CNET_START_TLS_OPTIONS_INIT;
      cnet_receive_view view{};
      cnet_const_buffer buffer{};
      cnet_error error{};
      cnet_websocket_config websocket_config{};
      (void)client;
      (void)listener;
      (void)accepted_stream;
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
      (void)vsock_listener_config;
      (void)vsock_peer;
      (void)datagram_config;
      (void)kcp_config;
      (void)secure_kcp_config;
      (void)packet_config;
      (void)tls_client_config;
      (void)tls_server_config;
      (void)options;
      (void)start_tls_options;
      (void)view;
      (void)buffer;
      (void)error;
      (void)websocket_config;
      check_equal(client.impl, nullptr);
      check_equal(listener.impl, nullptr);
      check_equal(connection.slot, 0u);
      check_equal(connection.generation, 0u);
      check_equal(accepted_stream.internal_active, 0u);
      check_equal(stream_socket_options.size, sizeof(stream_socket_options));
      check_equal(listener_options.size, sizeof(listener_options));
    }
  }
}
