#include <chttp/chttp.h>

#include <type_traits>

static_assert(std::is_standard_layout<chttp_client>::value, "client handle must be C ABI data");
static_assert(std::is_standard_layout<chttp_async_client>::value,
              "async client handle must be C ABI data");
static_assert(std::is_standard_layout<chttp_request>::value, "request handle must be C ABI data");
static_assert(std::is_standard_layout<chttp_response>::value, "owning response must be C ABI data");
static_assert(std::is_standard_layout<chttp_response_view>::value,
              "response view must be C ABI data");

int main() {
  chttp_client client{};
  chttp_async_client async_client{};
  chttp_request request{};
  return client.impl == nullptr && async_client.impl == nullptr && request.slot == 0u &&
                 request.generation == 0u
             ? 0
             : 1;
}
