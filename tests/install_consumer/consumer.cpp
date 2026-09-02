#if defined(CONSUME_NETWORK_CPP)

  #include <cnet/cnet.h>
  #include <crpc/crpc.h>

int main() {
  crpc_client client{};
  crpc_async_client async_client{};
  crpc_request request{};
  chttp_server server{};
  chttp_session session{};
  cnet_tls_server tls_server{};
  cnet_tls_client_config tls_client_config{};
  return client.impl == nullptr && async_client.impl == nullptr && request.slot == 0u &&
                 request.generation == 0u && server.impl == nullptr && session.impl == nullptr &&
                 tls_server.impl == nullptr && tls_client_config.size == 0u
             ? 0
             : 1;
}

#else

  #include <rocida/stl/typed.h>
  #include <turbo_uuid.h>

static_assert(sizeof(Rocida::UUID) == TURBO_UUID_SIZE);

Struct(InstalledCppPayload, (TYPE(Vec, int), values));

int main() {
  const cmeta_field_desc *field = cmeta_struct_find_field(InstalledCppPayload_meta(), "values");
  InstalledCppPayload payload{};
  const int input = 9;
  int output = 0;
  int status = 0;

  if (field == nullptr || field->declared_type == nullptr) return 1;
  if (cmeta_container_bind_types(&payload.values, field->declared_type) != CMETA_OK) return 2;
  if (vec_init(&payload.values, 1u) != STL_OK) return 3;
  if (vec_push(&payload.values, &input) != STL_OK) {
    status = 4;
    goto cleanup;
  }
  if (vec_pop(&payload.values, &output) != STL_OK || output != input) status = 5;

cleanup:
  vec_destroy(&payload.values);
  return status;
}

#endif
