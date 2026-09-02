#include <chttp/chttp.h>

#include <type_traits>

static_assert(std::is_standard_layout<chttp_client>::value, "client handle must be C ABI data");
static_assert(std::is_standard_layout<chttp_async_client>::value,
              "async client handle must be C ABI data");
static_assert(std::is_standard_layout<chttp_request>::value, "request handle must be C ABI data");
static_assert(std::is_standard_layout<chttp_response>::value, "owning response must be C ABI data");
static_assert(std::is_standard_layout<chttp_response_view>::value,
              "response view must be C ABI data");
static_assert(std::is_standard_layout<chttp_server>::value, "server handle must be C ABI data");
static_assert(std::is_standard_layout<chttp_server_request_view>::value,
              "server request view must be C ABI data");
static_assert(std::is_standard_layout<chttp_server_response>::value,
              "server response builder must be C ABI data");
static_assert(std::is_standard_layout<chttp_server_next>::value,
              "middleware continuation must be C ABI data");
static_assert(std::is_standard_layout<chttp_session>::value, "session handle must be C ABI data");
static_assert(std::is_standard_layout<chttp_tls_profile>::value, "TLS profile must be C ABI data");

int main() {
  chttp_tls_profile tls_profile{};
  chttp_client client{};
  chttp_async_client async_client{};
  chttp_request request{};
  chttp_server server{};
  chttp_server_config server_config{};
  chttp_server_stats server_stats{};
  (void)tls_profile;
  (void)server_config;
  (void)server_stats;
  return client.impl == nullptr && async_client.impl == nullptr && request.slot == 0u &&
                 request.generation == 0u && server.impl == nullptr
             ? 0
             : 1;
}
