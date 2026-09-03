#include <cnet/cnet.h>

#include "cnet_module.h"
#include "cnet_transport.h"

#include <salts/clock.h>

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
// clang-format off
  #include <winsock2.h>
  #include <windows.h>
  #include <ws2tcpip.h>
// clang-format on
typedef SOCKET cnet_datagram_socket;
  #define CNET_DATAGRAM_INVALID_SOCKET INVALID_SOCKET
#else
  #include <errno.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
typedef int cnet_datagram_socket;
  #define CNET_DATAGRAM_INVALID_SOCKET (-1)
#endif

typedef struct cnet_datagram_send_slot {
  native_io_request request;
  struct sockaddr_storage native_peer;
  cnet_datagram_peer peer;
  unsigned char *data;
  size_t size;
  uint64_t tag;
  bool active;
} cnet_datagram_send_slot;

typedef struct cnet_datagram_impl {
  cnet_datagram *public_datagram;
  native_io_backend backend;
  native_io_endpoint endpoint;
  cnet_datagram_socket socket_value;
  cnet_datagram_observer observer;
  cnet_datagram_send_slot *send_slots;
  uint32_t *free_sends;
  unsigned char *send_storage;
  unsigned char *receive_buffer;
  native_io_completion *completions;
  struct sockaddr_storage receive_peer;
  native_io_request receive_request;
  size_t send_capacity;
  size_t free_send_count;
  size_t active_send_count;
  size_t request_capacity;
  size_t completion_batch_capacity;
  size_t max_datagram_bytes;
  size_t receive_buffer_bytes;
  size_t receive_demand;
  uint16_t port;
  bool receive_active;
  bool polling;
  bool callback_active;
  bool stopping;
  bool stopped;
} cnet_datagram_impl;

static cnet_datagram_impl *cnet_datagram_get(cnet_datagram *datagram) {
  return datagram != NULL ? (cnet_datagram_impl *)datagram->impl : NULL;
}

static const cnet_datagram_impl *cnet_datagram_const_get(const cnet_datagram *datagram) {
  return datagram != NULL ? (const cnet_datagram_impl *)datagram->impl : NULL;
}

static int cnet_datagram_native_error(void) {
#if defined(_WIN32)
  const int error = WSAGetLastError();
#else
  const int error = errno;
#endif
  return error > 0 ? -error : SALTS_EIO;
}

static void cnet_datagram_close_socket(cnet_datagram_impl *impl) {
  if (impl == NULL || impl->socket_value == CNET_DATAGRAM_INVALID_SOCKET) return;
#if defined(_WIN32)
  (void)closesocket(impl->socket_value);
#else
  (void)close(impl->socket_value);
#endif
  impl->socket_value = CNET_DATAGRAM_INVALID_SOCKET;
}

static int cnet_datagram_bound_port(cnet_datagram_socket socket_value, uint16_t *out_port) {
  struct sockaddr_storage address;
#if defined(_WIN32)
  int length = (int)sizeof(address);
#else
  socklen_t length = (socklen_t)sizeof(address);
#endif
  memset(&address, 0, sizeof(address));
  if (getsockname(socket_value, (struct sockaddr *)&address, &length) != 0)
    return cnet_datagram_native_error();
  if (address.ss_family == AF_INET)
    *out_port = ntohs(((const struct sockaddr_in *)&address)->sin_port);
  else if (address.ss_family == AF_INET6)
    *out_port = ntohs(((const struct sockaddr_in6 *)&address)->sin6_port);
  else return SALTS_EPROTO;
  return *out_port != 0u ? SALTS_OK : SALTS_EPROTO;
}

static int cnet_datagram_peer_from_native(const struct sockaddr_storage *native_peer,
                                          size_t native_size, cnet_datagram_peer *peer) {
  memset(peer, 0, sizeof(*peer));
  if (native_peer->ss_family == AF_INET && native_size >= sizeof(struct sockaddr_in)) {
    const struct sockaddr_in *address = (const struct sockaddr_in *)native_peer;
    peer->family = CNET_DATAGRAM_ADDRESS_IPV4;
    peer->port = ntohs(address->sin_port);
    memcpy(peer->address, &address->sin_addr, sizeof(address->sin_addr));
    return peer->port != 0u ? SALTS_OK : SALTS_EPROTO;
  }
  if (native_peer->ss_family == AF_INET6 && native_size >= sizeof(struct sockaddr_in6)) {
    const struct sockaddr_in6 *address = (const struct sockaddr_in6 *)native_peer;
    peer->family = CNET_DATAGRAM_ADDRESS_IPV6;
    peer->port = ntohs(address->sin6_port);
    peer->scope_id = address->sin6_scope_id;
    memcpy(peer->address, &address->sin6_addr, sizeof(address->sin6_addr));
    return peer->port != 0u ? SALTS_OK : SALTS_EPROTO;
  }
  return SALTS_EPROTO;
}

static int cnet_datagram_peer_to_native(const cnet_datagram_peer *peer,
                                        struct sockaddr_storage *native_peer,
                                        size_t *native_size) {
  if (peer == NULL || native_peer == NULL || native_size == NULL || peer->port == 0u)
    return SALTS_EINVAL;
  memset(native_peer, 0, sizeof(*native_peer));
  if (peer->family == CNET_DATAGRAM_ADDRESS_IPV4) {
    struct sockaddr_in *address = (struct sockaddr_in *)native_peer;
    address->sin_family = AF_INET;
    address->sin_port = htons(peer->port);
    memcpy(&address->sin_addr, peer->address, sizeof(address->sin_addr));
    *native_size = sizeof(*address);
    return SALTS_OK;
  }
  if (peer->family == CNET_DATAGRAM_ADDRESS_IPV6) {
    struct sockaddr_in6 *address = (struct sockaddr_in6 *)native_peer;
    address->sin6_family = AF_INET6;
    address->sin6_port = htons(peer->port);
    address->sin6_scope_id = peer->scope_id;
    memcpy(&address->sin6_addr, peer->address, sizeof(address->sin6_addr));
    *native_size = sizeof(*address);
    return SALTS_OK;
  }
  return SALTS_EINVAL;
}

static int cnet_datagram_arm_receive(cnet_datagram_impl *impl) {
  native_io_operation operation;
  int status;
  if (impl->receive_active || impl->receive_demand == 0u || impl->stopping) return SALTS_OK;
  memset(&impl->receive_peer, 0, sizeof(impl->receive_peer));
  operation = (native_io_operation){.kind = NATIVE_IO_OPERATION_UDP_RECV_FROM,
                                    .endpoint = impl->endpoint,
                                    .buffer = impl->receive_buffer,
                                    .length = impl->receive_buffer_bytes,
                                    .user_data = 0u,
                                    .address = &impl->receive_peer,
                                    .address_capacity = sizeof(impl->receive_peer)};
  status = native_io_backend_submit(&impl->backend, &operation, &impl->receive_request);
  if (status == SALTS_OK) impl->receive_active = true;
  return status;
}

static void cnet_datagram_send_slot_release(cnet_datagram_impl *impl, size_t index) {
  cnet_datagram_send_slot *slot = &impl->send_slots[index];
  memset(&slot->request, 0, sizeof(slot->request));
  memset(&slot->native_peer, 0, sizeof(slot->native_peer));
  memset(&slot->peer, 0, sizeof(slot->peer));
  slot->size = 0u;
  slot->tag = 0u;
  slot->active = false;
  impl->free_sends[impl->free_send_count++] = (uint32_t)index;
  --impl->active_send_count;
}

static int cnet_datagram_completion_status(const native_io_completion *completion) {
  if (completion->kind == NATIVE_IO_COMPLETION_OK) return SALTS_OK;
  if (completion->kind == NATIVE_IO_COMPLETION_CANCELLED) return SALTS_ECANCELED;
  return completion->status < SALTS_OK ? completion->status : SALTS_EIO;
}

static int cnet_datagram_complete(cnet_datagram_impl *impl,
                                  const native_io_completion *completion,
                                  size_t *callback_count) {
  if (completion->user_data == 0u) {
    cnet_datagram_peer peer;
    cnet_receive_view view;
    int status;
    if (!impl->receive_active || completion->request.slot != impl->receive_request.slot ||
        completion->request.generation != impl->receive_request.generation)
      return SALTS_EPROTO;
    impl->receive_active = false;
    memset(&impl->receive_request, 0, sizeof(impl->receive_request));
    if (completion->kind == NATIVE_IO_COMPLETION_CANCELLED && impl->stopping) return SALTS_OK;
    status = cnet_datagram_completion_status(completion);
    if (status != SALTS_OK) return status;
    if (impl->stopping) return SALTS_OK;
    if (completion->bytes > impl->max_datagram_bytes || impl->receive_demand == 0u)
      return SALTS_EPROTO;
    status = cnet_datagram_peer_from_native(&impl->receive_peer, completion->address_length, &peer);
    if (status != SALTS_OK) return status;
    --impl->receive_demand;
    view = (cnet_receive_view){impl->receive_buffer, completion->bytes, CNET_MESSAGE_DATAGRAM};
    impl->callback_active = true;
    impl->observer.on_receive(impl->observer.user, impl->public_datagram, &peer, &view);
    impl->callback_active = false;
    ++*callback_count;
    return cnet_datagram_arm_receive(impl);
  }
  {
    const size_t index = (size_t)completion->user_data - 1u;
    cnet_datagram_peer peer;
    size_t size;
    uint64_t tag;
    int status;
    if (index >= impl->send_capacity || !impl->send_slots[index].active ||
        completion->request.slot != impl->send_slots[index].request.slot ||
        completion->request.generation != impl->send_slots[index].request.generation)
      return SALTS_EPROTO;
    peer = impl->send_slots[index].peer;
    size = impl->send_slots[index].size;
    tag = impl->send_slots[index].tag;
    status = cnet_datagram_completion_status(completion);
    if (status == SALTS_OK && completion->bytes != size) status = SALTS_EIO;
    cnet_datagram_send_slot_release(impl, index);
    impl->callback_active = true;
    impl->observer.on_send(impl->observer.user, impl->public_datagram, &peer, size, status, tag);
    impl->callback_active = false;
    ++*callback_count;
    return SALTS_OK;
  }
}

static int cnet_datagram_drive(cnet_datagram_impl *impl, uint32_t timeout_ms,
                               size_t *out_callbacks) {
  size_t completion_count = 0u;
  size_t callback_count = 0u;
  int status = native_io_backend_observe(&impl->backend, impl->completions,
                                         impl->completion_batch_capacity, timeout_ms,
                                         &completion_count);
  if (status == SALTS_ETIMEDOUT) status = SALTS_OK;
  if (status != SALTS_OK) return status;
  for (size_t index = 0u; index < completion_count; ++index) {
    status = cnet_datagram_complete(impl, &impl->completions[index], &callback_count);
    if (status != SALTS_OK) return status;
  }
  *out_callbacks = callback_count;
  return SALTS_OK;
}

int cnet_datagram_init(cnet_datagram *datagram, const cnet_datagram_config *config) {
  cnet_datagram_impl *impl = NULL;
  unsigned char native_address[sizeof(struct sockaddr_storage)];
  size_t native_address_size = 0u;
  int family;
  int status;
  if (datagram == NULL || config == NULL || config->size != sizeof(*config)) return SALTS_EINVAL;
  if (datagram->impl != NULL) return SALTS_EALREADY;
  if (config->host == NULL || config->host[0] == '\0' || config->send_capacity == 0u ||
      config->request_capacity <= config->send_capacity ||
      config->completion_batch_capacity == 0u ||
      config->completion_batch_capacity > config->request_capacity ||
      config->max_datagram_bytes == 0u ||
      config->max_datagram_bytes > CNET_DATAGRAM_MAX_PAYLOAD_BYTES ||
      config->receive_buffer_bytes < config->max_datagram_bytes ||
      config->receive_buffer_bytes > CNET_DATAGRAM_MAX_PAYLOAD_BYTES ||
      config->observer.on_receive == NULL || config->observer.on_send == NULL ||
      (config->reuse_port != 0 && config->reuse_port != 1) ||
      !native_io_backend_kind_supported(config->backend) ||
      config->send_capacity > SIZE_MAX / config->max_datagram_bytes)
    return SALTS_EINVAL;
#if !defined(SO_REUSEPORT)
  if (config->reuse_port) return SALTS_ENOTSUP;
#endif
  status = cnet_transport_parse_bind_address(config->host, config->port, native_address,
                                             sizeof(native_address), &native_address_size);
  if (status != SALTS_OK) return status;
  family = ((const struct sockaddr *)native_address)->sa_family;
  status = cnet_module_init();
  if (status != SALTS_OK) return status;
  impl = (cnet_datagram_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) {
    (void)cnet_module_shutdown();
    return SALTS_ENOMEM;
  }
  impl->socket_value = CNET_DATAGRAM_INVALID_SOCKET;
  impl->public_datagram = datagram;
  impl->observer = config->observer;
  impl->send_capacity = config->send_capacity;
  impl->free_send_count = config->send_capacity;
  impl->request_capacity = config->request_capacity;
  impl->completion_batch_capacity = config->completion_batch_capacity;
  impl->max_datagram_bytes = config->max_datagram_bytes;
  impl->receive_buffer_bytes = config->receive_buffer_bytes;
  impl->send_slots = (cnet_datagram_send_slot *)calloc(config->send_capacity,
                                                       sizeof(*impl->send_slots));
  impl->free_sends = (uint32_t *)calloc(config->send_capacity, sizeof(*impl->free_sends));
  impl->send_storage = (unsigned char *)calloc(config->send_capacity, config->max_datagram_bytes);
  impl->receive_buffer = (unsigned char *)malloc(config->receive_buffer_bytes);
  impl->completions = (native_io_completion *)calloc(config->completion_batch_capacity,
                                                      sizeof(*impl->completions));
  if (impl->send_slots == NULL || impl->free_sends == NULL || impl->send_storage == NULL ||
      impl->receive_buffer == NULL || impl->completions == NULL) {
    status = SALTS_ENOMEM;
    goto fail;
  }
  for (size_t index = 0u; index < config->send_capacity; ++index) {
    impl->send_slots[index].data = impl->send_storage + index * config->max_datagram_bytes;
    impl->free_sends[index] = (uint32_t)(config->send_capacity - index - 1u);
  }
  status = native_io_backend_init(
      &impl->backend,
      &(native_io_backend_config){config->backend, 1u, config->request_capacity,
                                  config->completion_batch_capacity});
  if (status != SALTS_OK) goto fail;
#if defined(_WIN32)
  if (config->backend != NATIVE_IO_BACKEND_IOCP) {
    status = SALTS_ENOTSUP;
    goto fail;
  }
  impl->socket_value = WSASocketW(family, SOCK_DGRAM, IPPROTO_UDP, NULL, 0u, WSA_FLAG_OVERLAPPED);
#else
  impl->socket_value = socket(family, SOCK_DGRAM, IPPROTO_UDP);
#endif
  if (impl->socket_value == CNET_DATAGRAM_INVALID_SOCKET) {
    status = cnet_datagram_native_error();
    goto fail;
  }
#if defined(SO_REUSEPORT)
  if (config->reuse_port) {
    const int reuse_port = 1;
#if defined(_WIN32)
    if (setsockopt(impl->socket_value, SOL_SOCKET, SO_REUSEPORT, (const char *)&reuse_port,
                   (int)sizeof(reuse_port)) != 0) {
#else
    if (setsockopt(impl->socket_value, SOL_SOCKET, SO_REUSEPORT, &reuse_port,
                   (socklen_t)sizeof(reuse_port)) != 0) {
#endif
      status = cnet_datagram_native_error();
      goto fail;
    }
  }
#endif
  if (bind(impl->socket_value, (const struct sockaddr *)native_address,
           (int)native_address_size) != 0) {
    status = cnet_datagram_native_error();
    goto fail;
  }
  status = cnet_datagram_bound_port(impl->socket_value, &impl->port);
  if (status != SALTS_OK) goto fail;
  status = native_io_backend_attach_socket(&impl->backend, (uintptr_t)impl->socket_value,
                                           &impl->endpoint);
  if (status != SALTS_OK) goto fail;
  datagram->impl = impl;
  return SALTS_OK;

fail:
  cnet_datagram_close_socket(impl);
  if (native_io_endpoint_valid(impl->endpoint))
    (void)native_io_backend_release_socket(&impl->backend, impl->endpoint);
  (void)native_io_backend_close(&impl->backend);
  (void)native_io_backend_destroy(&impl->backend);
  free(impl->completions);
  free(impl->receive_buffer);
  free(impl->send_storage);
  free(impl->free_sends);
  free(impl->send_slots);
  free(impl);
  (void)cnet_module_shutdown();
  return status;
}

int cnet_datagram_port(const cnet_datagram *datagram, uint16_t *out_port) {
  const cnet_datagram_impl *impl = cnet_datagram_const_get(datagram);
  if (out_port == NULL) return SALTS_EINVAL;
  *out_port = 0u;
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->stopping) return SALTS_ESHUTDOWN;
  *out_port = impl->port;
  return SALTS_OK;
}

int cnet_datagram_receive(cnet_datagram *datagram, size_t demand) {
  cnet_datagram_impl *impl = cnet_datagram_get(datagram);
  if (impl == NULL || demand == 0u) return SALTS_EINVAL;
  if (impl->stopping) return SALTS_ESHUTDOWN;
  if (demand > SIZE_MAX - impl->receive_demand) return SALTS_ERANGE;
  impl->receive_demand += demand;
  {
    const int status = cnet_datagram_arm_receive(impl);
    if (status != SALTS_OK) impl->receive_demand -= demand;
    return status;
  }
}

int cnet_datagram_send(cnet_datagram *datagram, const cnet_datagram_peer *peer,
                       const void *data, size_t size, uint64_t tag) {
  cnet_datagram_impl *impl = cnet_datagram_get(datagram);
  cnet_datagram_send_slot *slot;
  native_io_operation operation;
  size_t native_peer_size = 0u;
  size_t index;
  int status;
  if (impl == NULL || peer == NULL || data == NULL || size == 0u) return SALTS_EINVAL;
  if (impl->stopping) return SALTS_ESHUTDOWN;
  if (size > impl->max_datagram_bytes) return SALTS_EMSGSIZE;
  if (impl->free_send_count == 0u) return SALTS_ENOBUFS;
  index = impl->free_sends[--impl->free_send_count];
  slot = &impl->send_slots[index];
  status = cnet_datagram_peer_to_native(peer, &slot->native_peer, &native_peer_size);
  if (status != SALTS_OK) {
    impl->free_sends[impl->free_send_count++] = (uint32_t)index;
    return status;
  }
  memcpy(slot->data, data, size);
  slot->peer = *peer;
  slot->size = size;
  slot->tag = tag;
  slot->active = true;
  operation = (native_io_operation){.kind = NATIVE_IO_OPERATION_UDP_SEND_TO,
                                    .endpoint = impl->endpoint,
                                    .buffer = slot->data,
                                    .length = size,
                                    .user_data = (uintptr_t)(index + 1u),
                                    .address = &slot->native_peer,
                                    .address_capacity = sizeof(slot->native_peer),
                                    .address_length = native_peer_size};
  status = native_io_backend_submit(&impl->backend, &operation, &slot->request);
  if (status != SALTS_OK) {
    slot->active = false;
    slot->size = 0u;
    slot->tag = 0u;
    impl->free_sends[impl->free_send_count++] = (uint32_t)index;
    return status;
  }
  ++impl->active_send_count;
  return SALTS_OK;
}

int cnet_datagram_poll(cnet_datagram *datagram, uint32_t timeout_ms, size_t *out_events) {
  cnet_datagram_impl *impl = cnet_datagram_get(datagram);
  int status;
  if (out_events == NULL) return SALTS_EINVAL;
  *out_events = 0u;
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->stopping) return SALTS_ESHUTDOWN;
  if (impl->polling || impl->callback_active) return SALTS_EBUSY;
  impl->polling = true;
  status = cnet_datagram_drive(impl, timeout_ms, out_events);
  impl->polling = false;
  return status;
}

int cnet_datagram_wake(cnet_datagram *datagram) {
  cnet_datagram_impl *impl = cnet_datagram_get(datagram);
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->stopping) return SALTS_ESHUTDOWN;
  return native_io_backend_wake(&impl->backend);
}

int cnet_datagram_stop(cnet_datagram *datagram, uint32_t timeout_ms) {
  cnet_datagram_impl *impl = cnet_datagram_get(datagram);
  const uint64_t started_ms = salts_monotonic_ms();
  int first_status = SALTS_OK;
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->callback_active || impl->polling) return SALTS_EBUSY;
  if (impl->stopped) return SALTS_OK;
  impl->stopping = true;
  impl->receive_demand = 0u;
  if (impl->receive_active) {
    const int status = native_io_backend_cancel(&impl->backend, impl->receive_request);
    if (status != SALTS_OK && status != SALTS_EALREADY) first_status = status;
  }
  for (size_t index = 0u; index < impl->send_capacity; ++index) {
    if (impl->send_slots[index].active) {
      const int status = native_io_backend_cancel(&impl->backend, impl->send_slots[index].request);
      if (first_status == SALTS_OK && status != SALTS_OK && status != SALTS_EALREADY)
        first_status = status;
    }
  }
  while (impl->receive_active || impl->active_send_count != 0u) {
    const uint64_t elapsed_ms = salts_monotonic_ms() - started_ms;
    uint32_t remaining_ms;
    size_t callbacks = 0u;
    int status;
    if (elapsed_ms >= timeout_ms) return SALTS_ETIMEDOUT;
    remaining_ms = (uint32_t)((uint64_t)timeout_ms - elapsed_ms);
    status = cnet_datagram_drive(impl, remaining_ms, &callbacks);
    if (status != SALTS_OK && first_status == SALTS_OK) first_status = status;
    if (status != SALTS_OK) return status;
  }
  cnet_datagram_close_socket(impl);
  {
    const int status = native_io_backend_release_socket(&impl->backend, impl->endpoint);
    if (first_status == SALTS_OK && status != SALTS_OK) first_status = status;
  }
  {
    const int status = native_io_backend_close(&impl->backend);
    if (first_status == SALTS_OK && status != SALTS_OK) first_status = status;
  }
  impl->stopped = true;
  return first_status;
}

int cnet_datagram_destroy(cnet_datagram *datagram) {
  cnet_datagram_impl *impl = cnet_datagram_get(datagram);
  int status;
  if (datagram == NULL) return SALTS_EINVAL;
  if (impl == NULL) return SALTS_OK;
  if (!impl->stopped) return SALTS_EBUSY;
  status = native_io_backend_destroy(&impl->backend);
  if (status != SALTS_OK) return status;
  free(impl->completions);
  free(impl->receive_buffer);
  free(impl->send_storage);
  free(impl->free_sends);
  free(impl->send_slots);
  free(impl);
  datagram->impl = NULL;
  return cnet_module_shutdown();
}
