#include "cnet_resolver.h"

#include "cnet_module.h"

#include <ares.h>
#include <turbo/thread.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <winsock2.h>
typedef WSAPOLLFD cnet_resolver_pollfd;
#else
  #include <poll.h>
  #include <sys/socket.h>
typedef struct pollfd cnet_resolver_pollfd;
#endif

typedef enum cnet_resolver_slot_state {
  CNET_RESOLVER_SLOT_FREE = 0,
  CNET_RESOLVER_SLOT_ACTIVE,
  CNET_RESOLVER_SLOT_READY,
  CNET_RESOLVER_SLOT_RETIRED
} cnet_resolver_slot_state;

struct cnet_resolver_impl;

typedef struct cnet_resolver_slot {
  struct cnet_resolver_impl *owner;
  cnet_resolver_slot_state state;
  uint32_t generation;
  uintptr_t user_data;
  int socket_type;
  bool cancelled;
  char host[CNET_RESOLVER_HOST_CAPACITY];
  char service[6];
  cnet_resolver_result result;
} cnet_resolver_slot;

typedef struct cnet_resolver_socket {
  ares_socket_t descriptor;
  unsigned int events;
} cnet_resolver_socket;

typedef struct cnet_resolver_impl {
  ares_channel_t *channel;
  cnet_resolver_slot *slots;
  uint32_t *free_slots;
  uint32_t *ready_slots;
  cnet_resolver_socket *sockets;
  cnet_resolver_pollfd *poll_fds;
  ares_fd_events_t *ready_events;
  size_t capacity;
  size_t socket_capacity;
  size_t socket_count;
  size_t free_count;
  size_t ready_head;
  size_t ready_count;
  size_t active_count;
  bool admission_open;
  turbo_mutex_t control_lock;
  turbo_mutex_t state_lock;
  bool socket_overflow;
} cnet_resolver_impl;

static cnet_resolver_impl *cnet_resolver_get_impl(cnet_resolver *resolver) {
  return resolver != NULL ? (cnet_resolver_impl *)resolver->impl : NULL;
}

static int cnet_resolver_map_status(int status) {
  switch (status) {
  case ARES_SUCCESS:
    return TURBO_OK;
  case ARES_ENODATA:
    return TURBO_EAI_NODATA;
  case ARES_ENOTFOUND:
  case ARES_ENONAME:
    return TURBO_EAI_NONAME;
  case ARES_ETIMEOUT:
  case ARES_ESERVFAIL:
    return TURBO_EAI_AGAIN;
  case ARES_EBADFAMILY:
    return TURBO_EAI_FAMILY;
  case ARES_ESERVICE:
    return TURBO_EAI_SERVICE;
  case ARES_ENOMEM:
    return TURBO_EAI_MEMORY;
  case ARES_ECANCELLED:
  case ARES_EDESTRUCTION:
    return TURBO_EAI_CANCELED;
  default:
    return TURBO_EAI_FAIL;
  }
}

static cnet_resolver_slot *cnet_resolver_find_slot(cnet_resolver_impl *impl,
                                                   cnet_resolver_query query) {
  cnet_resolver_slot *slot;

  if (!cnet_resolver_query_valid(query) || (size_t)query.slot > impl->capacity) return NULL;
  slot = &impl->slots[query.slot - 1u];
  if (slot->state == CNET_RESOLVER_SLOT_FREE || slot->state == CNET_RESOLVER_SLOT_RETIRED ||
      slot->generation != query.generation)
    return NULL;
  return slot;
}

static void cnet_resolver_socket_state(void *context, ares_socket_t descriptor, int readable,
                                       int writable) {
  cnet_resolver_impl *impl = (cnet_resolver_impl *)context;
  const unsigned int events =
      (readable ? ARES_FD_EVENT_READ : 0u) | (writable ? ARES_FD_EVENT_WRITE : 0u);
  size_t index;
  for (index = 0u; index < impl->socket_count; ++index)
    if (impl->sockets[index].descriptor == descriptor) break;
  if (events == 0u) {
    if (index < impl->socket_count) {
      --impl->socket_count;
      if (index != impl->socket_count) impl->sockets[index] = impl->sockets[impl->socket_count];
    }
    return;
  }
  if (index < impl->socket_count) {
    impl->sockets[index].events = events;
    return;
  }
  if (impl->socket_count == impl->socket_capacity) {
    impl->socket_overflow = true;
    return;
  }
  impl->sockets[impl->socket_count++] = (cnet_resolver_socket){descriptor, events};
}

static void cnet_resolver_callback(void *argument, int status, int timeouts,
                                   struct ares_addrinfo *addresses) {
  cnet_resolver_slot *slot = (cnet_resolver_slot *)argument;
  cnet_resolver_impl *impl = slot->owner;
  const struct ares_addrinfo_node *node = NULL;

  if (status == ARES_SUCCESS) {
    for (node = addresses != NULL ? addresses->nodes : NULL; node != NULL; node = node->ai_next) {
      if ((node->ai_family == AF_INET || node->ai_family == AF_INET6) &&
          node->ai_socktype == slot->socket_type && node->ai_addr != NULL &&
          node->ai_addrlen <= CNET_RESOLVER_ADDRESS_CAPACITY)
        break;
    }
    if (node == NULL) status = ARES_ENODATA;
  }

  turbo_mutex_lock(&impl->state_lock);
  if (slot->state == CNET_RESOLVER_SLOT_ACTIVE) {
    memset(&slot->result, 0, sizeof(slot->result));
    slot->result.query.slot = (uint32_t)(slot - impl->slots) + 1u;
    slot->result.query.generation = slot->generation;
    slot->result.user_data = slot->user_data;
    slot->result.native_status = status;
    slot->result.timeouts = timeouts;
    slot->result.status = slot->cancelled ? TURBO_EAI_CANCELED : cnet_resolver_map_status(status);
    if (slot->result.status == TURBO_OK && node != NULL) {
      slot->result.address_length = (size_t)node->ai_addrlen;
      memcpy(slot->result.address, node->ai_addr, slot->result.address_length);
    }
    slot->state = CNET_RESOLVER_SLOT_READY;
    impl->ready_slots[(impl->ready_head + impl->ready_count) % impl->capacity] =
        slot->result.query.slot - 1u;
    ++impl->ready_count;
  }
  turbo_mutex_unlock(&impl->state_lock);

  if (addresses != NULL) ares_freeaddrinfo(addresses);
}

bool cnet_resolver_query_valid(cnet_resolver_query query) {
  return query.slot != 0u && query.generation != 0u;
}

int cnet_resolver_init(cnet_resolver *resolver, const cnet_resolver_config *config) {
  cnet_resolver_impl *impl;
  struct ares_options options;
  size_t socket_capacity;
  int status;
  size_t index;

  if (resolver == NULL || config == NULL || config->query_capacity == 0u) return TURBO_EINVAL;
  if (resolver->impl != NULL) return TURBO_EALREADY;
  if (config->query_capacity > UINT32_MAX ||
      config->query_capacity > SIZE_MAX / sizeof(cnet_resolver_slot) ||
      config->query_capacity > SIZE_MAX / sizeof(uint32_t))
    return TURBO_ERANGE;
  if (config->query_capacity > (SIZE_MAX - ARES_GETSOCK_MAXNUM) / 2u) return TURBO_ERANGE;
  socket_capacity = config->query_capacity * 2u + ARES_GETSOCK_MAXNUM;
  if (socket_capacity > SIZE_MAX / sizeof(cnet_resolver_socket) ||
      socket_capacity > SIZE_MAX / sizeof(cnet_resolver_pollfd) ||
      socket_capacity > SIZE_MAX / sizeof(ares_fd_events_t))
    return TURBO_ERANGE;

  status = cnet_module_acquire_resolver();
  if (status != TURBO_OK) return status;

  impl = (cnet_resolver_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) {
    cnet_module_release_resolver();
    return TURBO_ENOMEM;
  }
  impl->slots = (cnet_resolver_slot *)calloc(config->query_capacity, sizeof(*impl->slots));
  impl->free_slots = (uint32_t *)malloc(config->query_capacity * sizeof(*impl->free_slots));
  impl->ready_slots = (uint32_t *)malloc(config->query_capacity * sizeof(*impl->ready_slots));
  impl->sockets = (cnet_resolver_socket *)calloc(socket_capacity, sizeof(*impl->sockets));
  impl->poll_fds = (cnet_resolver_pollfd *)calloc(socket_capacity, sizeof(*impl->poll_fds));
  impl->ready_events = (ares_fd_events_t *)calloc(socket_capacity, sizeof(*impl->ready_events));
  if (impl->slots == NULL || impl->free_slots == NULL || impl->ready_slots == NULL ||
      impl->sockets == NULL || impl->poll_fds == NULL || impl->ready_events == NULL) {
    free(impl->ready_events);
    free(impl->poll_fds);
    free(impl->sockets);
    free(impl->ready_slots);
    free(impl->free_slots);
    free(impl->slots);
    free(impl);
    cnet_module_release_resolver();
    return TURBO_ENOMEM;
  }

  impl->capacity = config->query_capacity;
  impl->socket_capacity = socket_capacity;
  impl->free_count = config->query_capacity;
  impl->admission_open = true;
  for (index = 0u; index < impl->capacity; ++index) {
    impl->slots[index].owner = impl;
    impl->free_slots[index] = (uint32_t)(impl->capacity - index - 1u);
  }
  turbo_mutex_init(&impl->control_lock);
  turbo_mutex_init(&impl->state_lock);

  memset(&options, 0, sizeof(options));
  options.sock_state_cb = cnet_resolver_socket_state;
  options.sock_state_cb_data = impl;
  status = ares_init_options(&impl->channel, &options, ARES_OPT_SOCK_STATE_CB);
  if (status != ARES_SUCCESS) {
    turbo_mutex_destroy(&impl->state_lock);
    turbo_mutex_destroy(&impl->control_lock);
    free(impl->ready_events);
    free(impl->poll_fds);
    free(impl->sockets);
    free(impl->ready_slots);
    free(impl->free_slots);
    free(impl->slots);
    free(impl);
    cnet_module_release_resolver();
    return status == ARES_ENOMEM ? TURBO_ENOMEM : TURBO_EAI_FAIL;
  }

  resolver->impl = impl;
  return TURBO_OK;
}

int cnet_resolver_poll(cnet_resolver *resolver) {
  cnet_resolver_impl *impl = cnet_resolver_get_impl(resolver);
  size_t ready_count = 0u;
  size_t offset = 0u;
  int polled;
  ares_status_t status;
  if (impl == NULL) return TURBO_EINVAL;

  turbo_mutex_lock(&impl->control_lock);
  if (impl->socket_overflow) {
    impl->socket_overflow = false;
    ares_cancel(impl->channel);
    turbo_mutex_unlock(&impl->control_lock);
    return TURBO_ENOBUFS;
  }
  while (offset < impl->socket_count) {
    const size_t batch = impl->socket_count - offset > ARES_GETSOCK_MAXNUM
                             ? ARES_GETSOCK_MAXNUM
                             : impl->socket_count - offset;
    size_t index;
    for (index = 0u; index < batch; ++index) {
      const cnet_resolver_socket *socket = &impl->sockets[offset + index];
      cnet_resolver_pollfd *poll_fd = &impl->poll_fds[index];
      poll_fd->fd = socket->descriptor;
      poll_fd->events = 0;
      poll_fd->revents = 0;
      if ((socket->events & ARES_FD_EVENT_READ) != 0u) poll_fd->events |= POLLIN;
      if ((socket->events & ARES_FD_EVENT_WRITE) != 0u) poll_fd->events |= POLLOUT;
    }
#if defined(_WIN32)
    polled = WSAPoll(impl->poll_fds, (ULONG)batch, 0);
#else
    polled = poll(impl->poll_fds, (nfds_t)batch, 0);
#endif
    if (polled < 0) {
      turbo_mutex_unlock(&impl->control_lock);
      return TURBO_EAI_FAIL;
    }
    for (index = 0u; polled > 0 && index < batch; ++index) {
      const short revents = impl->poll_fds[index].revents;
      unsigned int events = 0u;
      if ((revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0) events |= ARES_FD_EVENT_READ;
      if ((revents & POLLOUT) != 0) events |= ARES_FD_EVENT_WRITE;
      if (events != 0u) {
        impl->ready_events[ready_count++] = (ares_fd_events_t){impl->poll_fds[index].fd, events};
      }
    }
    offset += batch;
  }
  status = ares_process_fds(impl->channel, ready_count != 0u ? impl->ready_events : NULL,
                            ready_count, ARES_PROCESS_FLAG_NONE);
  turbo_mutex_unlock(&impl->control_lock);
  if (status == ARES_SUCCESS) return TURBO_OK;
  return status == ARES_ENOMEM ? TURBO_ENOMEM : TURBO_EAI_FAIL;
}

bool cnet_resolver_has_pending(cnet_resolver *resolver) {
  cnet_resolver_impl *impl = cnet_resolver_get_impl(resolver);
  bool pending;
  if (impl == NULL) return false;
  turbo_mutex_lock(&impl->state_lock);
  pending = impl->active_count != 0u;
  turbo_mutex_unlock(&impl->state_lock);
  return pending;
}

int cnet_resolver_submit(cnet_resolver *resolver, const char *host, uint16_t port, int socket_type,
                         uintptr_t user_data, cnet_resolver_query *out_query) {
  cnet_resolver_impl *impl = cnet_resolver_get_impl(resolver);
  cnet_resolver_slot *slot;
  struct ares_addrinfo_hints hints;
  size_t host_length;
  uint32_t slot_index;

  if (out_query == NULL) return TURBO_EINVAL;
  memset(out_query, 0, sizeof(*out_query));
  if (impl == NULL || host == NULL || port == 0u) return TURBO_EINVAL;
  if (socket_type != SOCK_STREAM && socket_type != SOCK_DGRAM) return TURBO_EAI_SOCKTYPE;
  host_length = strnlen(host, CNET_RESOLVER_HOST_CAPACITY);
  if (host_length == 0u || host_length == CNET_RESOLVER_HOST_CAPACITY) return TURBO_ENAMETOOLONG;

  turbo_mutex_lock(&impl->control_lock);
  turbo_mutex_lock(&impl->state_lock);
  if (!impl->admission_open) {
    turbo_mutex_unlock(&impl->state_lock);
    turbo_mutex_unlock(&impl->control_lock);
    return TURBO_ESHUTDOWN;
  }
  if (impl->free_count == 0u) {
    turbo_mutex_unlock(&impl->state_lock);
    turbo_mutex_unlock(&impl->control_lock);
    return TURBO_ENOBUFS;
  }

  slot_index = impl->free_slots[--impl->free_count];
  slot = &impl->slots[slot_index];
  ++slot->generation;
  slot->state = CNET_RESOLVER_SLOT_ACTIVE;
  slot->user_data = user_data;
  slot->socket_type = socket_type;
  slot->cancelled = false;
  memcpy(slot->host, host, host_length + 1u);
  (void)snprintf(slot->service, sizeof(slot->service), "%u", (unsigned int)port);
  ++impl->active_count;
  out_query->slot = slot_index + 1u;
  out_query->generation = slot->generation;
  turbo_mutex_unlock(&impl->state_lock);

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = socket_type;
  hints.ai_protocol = socket_type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
  ares_getaddrinfo(impl->channel, slot->host, slot->service, &hints, cnet_resolver_callback, slot);
  turbo_mutex_unlock(&impl->control_lock);
  return TURBO_OK;
}

int cnet_resolver_cancel(cnet_resolver *resolver, cnet_resolver_query query) {
  cnet_resolver_impl *impl = cnet_resolver_get_impl(resolver);
  cnet_resolver_slot *slot;

  if (impl == NULL) return TURBO_EINVAL;
  turbo_mutex_lock(&impl->state_lock);
  slot = cnet_resolver_find_slot(impl, query);
  if (slot == NULL) {
    turbo_mutex_unlock(&impl->state_lock);
    return TURBO_ENOENT;
  }
  slot->cancelled = true;
  if (slot->state == CNET_RESOLVER_SLOT_READY) {
    slot->result.status = TURBO_EAI_CANCELED;
    slot->result.address_length = 0u;
  }
  turbo_mutex_unlock(&impl->state_lock);
  return TURBO_OK;
}

int cnet_resolver_take(cnet_resolver *resolver, cnet_resolver_result *out_result) {
  cnet_resolver_impl *impl = cnet_resolver_get_impl(resolver);
  cnet_resolver_slot *slot;
  uint32_t slot_index;
  bool drained;

  if (out_result == NULL) return TURBO_EINVAL;
  memset(out_result, 0, sizeof(*out_result));
  if (impl == NULL) return TURBO_EINVAL;

  turbo_mutex_lock(&impl->state_lock);
  if (impl->ready_count == 0u) {
    drained = !impl->admission_open && impl->active_count == 0u;
    turbo_mutex_unlock(&impl->state_lock);
    return drained ? TURBO_EOF : TURBO_ETIMEDOUT;
  }

  slot_index = impl->ready_slots[impl->ready_head];
  impl->ready_head = (impl->ready_head + 1u) % impl->capacity;
  --impl->ready_count;
  slot = &impl->slots[slot_index];
  *out_result = slot->result;
  memset(&slot->result, 0, sizeof(slot->result));
  slot->state =
      slot->generation == UINT32_MAX ? CNET_RESOLVER_SLOT_RETIRED : CNET_RESOLVER_SLOT_FREE;
  slot->cancelled = false;
  --impl->active_count;
  if (slot->state == CNET_RESOLVER_SLOT_FREE) impl->free_slots[impl->free_count++] = slot_index;
  turbo_mutex_unlock(&impl->state_lock);
  return TURBO_OK;
}

int cnet_resolver_close(cnet_resolver *resolver, uint32_t timeout_ms) {
  cnet_resolver_impl *impl = cnet_resolver_get_impl(resolver);

  if (impl == NULL) return TURBO_EINVAL;
  (void)timeout_ms;

  turbo_mutex_lock(&impl->control_lock);
  turbo_mutex_lock(&impl->state_lock);
  if (!impl->admission_open) {
    turbo_mutex_unlock(&impl->state_lock);
    turbo_mutex_unlock(&impl->control_lock);
    return TURBO_EALREADY;
  }
  impl->admission_open = false;
  turbo_mutex_unlock(&impl->state_lock);
  ares_cancel(impl->channel);
  turbo_mutex_unlock(&impl->control_lock);
  return TURBO_OK;
}

int cnet_resolver_destroy(cnet_resolver *resolver) {
  cnet_resolver_impl *impl = cnet_resolver_get_impl(resolver);

  if (resolver == NULL) return TURBO_EINVAL;
  if (impl == NULL) return TURBO_OK;

  turbo_mutex_lock(&impl->control_lock);
  turbo_mutex_lock(&impl->state_lock);
  if (impl->admission_open || impl->active_count != 0u) {
    turbo_mutex_unlock(&impl->state_lock);
    turbo_mutex_unlock(&impl->control_lock);
    return TURBO_EBUSY;
  }
  turbo_mutex_unlock(&impl->state_lock);
  ares_destroy(impl->channel);
  turbo_mutex_unlock(&impl->control_lock);

  turbo_mutex_destroy(&impl->state_lock);
  turbo_mutex_destroy(&impl->control_lock);
  free(impl->ready_events);
  free(impl->poll_fds);
  free(impl->sockets);
  free(impl->ready_slots);
  free(impl->free_slots);
  free(impl->slots);
  free(impl);
  resolver->impl = NULL;
  cnet_module_release_resolver();
  return TURBO_OK;
}
