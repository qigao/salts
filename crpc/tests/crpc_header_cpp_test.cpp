#include <crpc/crpc.h>

#include <type_traits>

static_assert(std::is_standard_layout<crpc_client>::value,
              "request/reply client must be C ABI data");
static_assert(std::is_standard_layout<crpc_async_client>::value, "async client must be C ABI data");
static_assert(std::is_standard_layout<crpc_response>::value, "owning response must be C ABI data");
static_assert(std::is_standard_layout<crpc_response_view>::value,
              "response view must be C ABI data");

int main() {
  crpc_client client = {};
  crpc_async_client async_client = {};
  crpc_request request = {};
  return client.impl == nullptr && async_client.impl == nullptr && request.slot == 0u ? 0 : 1;
}
