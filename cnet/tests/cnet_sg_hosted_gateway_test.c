#include <cnet/cnet.h>
#include <cnet/sg_host.h>
#include <salts/native_io_sharded.h>
#include <salts/clock.h>
#include <salts/thread.h>
#include <tinytest.h>

#include <stdint.h>
#include <string.h>
#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

/* Real CNet inbound/outbound sharing a NativeIO SG backend, with one short
 * owner-progress task per cycle. SG-owned pipe terminal uses the same observe
 * authority; no Actor, duplicate NativeIO observe, or blocking forever task. */
enum { HOST_BATCH = 16u, HOST_TIMEOUT_MS = 6000u };
typedef struct hosted_state hosted_state;
typedef struct hosted_probe {
  hosted_state *host;
  cnet_client *client;
  const char *expected;
  size_t bytes, sent, connected, terminal;
} hosted_probe;
struct hosted_state {
  native_io_sharded *sg;
  native_io_sharded_host_lease lease;
  native_io_backend *backend;
  cnet_client inbound, outbound;
  cnet_listener listener;
  cnet_stream_endpoint local;
  cnet_connection accepted, outgoing;
  hosted_probe in_probe, out_probe;
  const void *owner_thread;
  int status;
  bool accepting, data_started, closing, done, clients_destroyed;
#if defined(__linux__)
  int pipe_read, pipe_write;
  native_io_sharded_endpoint pipe_endpoint;
  native_io_sharded_request pipe_request;
  char pipe_byte;
  size_t sg_terminal, sg_finalize;
#endif
};

static native_io_backend_kind host_kind(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
  return NATIVE_IO_BACKEND_KQUEUE;
#else
  return NATIVE_IO_BACKEND_EPOLL;
#endif
}
static cnet_client_config host_config(void) {
  cnet_client_config c = {0};
  c.backend = host_kind();
  c.connection_capacity = 1u;
  c.command_capacity = 8u;
  c.request_capacity = 8u;
  c.completion_batch_capacity = HOST_BATCH;
  c.event_capacity = 8u;
  c.max_send_bytes = 1024u;
  c.receive_buffer_bytes = 1024u;
  c.connect_timeout_ms = HOST_TIMEOUT_MS;
  c.read_timeout_ms = HOST_TIMEOUT_MS;
  c.write_timeout_ms = HOST_TIMEOUT_MS;
  return c;
}
static void host_record_error(hosted_state *f, int status) {
  if (f->status == SALTS_OK) f->status = status;
}
static void host_on_state(void *arg, cnet_connection handle,
                          cnet_connection_state state, const cnet_error *error) {
  hosted_probe *p = (hosted_probe *)arg;
  (void)handle; (void)error;
  if (cmeta_thread_current_token() != p->host->owner_thread) {
    host_record_error(p->host, SALTS_EPERM);
    return;
  }
  if (state == CNET_CONNECTION_CONNECTED) ++p->connected;
  if (state == CNET_CONNECTION_FAILED) host_record_error(p->host, SALTS_ECONNRESET);
  if (state == CNET_CONNECTION_FAILED || state == CNET_CONNECTION_CLOSED) ++p->terminal;
}
static void host_on_receive(void *arg, cnet_connection handle, const cnet_receive_view *view) {
  hosted_probe *p = (hosted_probe *)arg;
  size_t size = strlen(p->expected);
  if (cmeta_thread_current_token() != p->host->owner_thread || view == NULL ||
      p->bytes > size || view->size > size - p->bytes ||
      memcmp(view->data, p->expected + p->bytes, view->size) != 0) {
    host_record_error(p->host, SALTS_EPROTO);
    return;
  }
  p->bytes += view->size;
  if (p->bytes != size) {
    int status = cnet_receive(p->client, handle, 1u);
    if (status != SALTS_OK) host_record_error(p->host, status);
  }
}
static void host_on_send(void *arg, cnet_connection handle, size_t size) {
  hosted_probe *p = (hosted_probe *)arg;
  (void)handle;
  if (cmeta_thread_current_token() != p->host->owner_thread)
    host_record_error(p->host, SALTS_EPERM);
  p->sent += size;
}
static cnet_observer host_observer(hosted_probe *p) {
  cnet_observer o = {0};
  o.on_state = host_on_state;
  o.on_receive = host_on_receive;
  o.on_send = host_on_send;
  o.user = p;
  return o;
}
static bool host_quiescent(void *arg) {
  hosted_state *f = (hosted_state *)arg;
  return f->clients_destroyed && !f->listener.impl &&
         !f->inbound.impl && !f->outbound.impl;
}
#if defined(__linux__)
static void host_pipe_terminal(native_io_sharded_context *context,
                               const native_io_sharded_completion *event, void *arg) {
  hosted_state *f = (hosted_state *)arg;
  if (native_io_sharded_context_shard(context) != 0u ||
      event->status != SALTS_OK || event->bytes != 1u || f->pipe_byte != 'X')
    host_record_error(f, SALTS_EPROTO);
  ++f->sg_terminal;
}
static void host_pipe_finalize(void *arg) {
  ++((hosted_state *)arg)->sg_finalize;
}
#endif
#define HOST_TASK_OK(f, expr) do { int host_rc = (expr); \
  if (host_rc != SALTS_OK) { host_record_error((f), host_rc); return; } \
} while (0)

static void host_initialize(native_io_sharded_context *context, void *arg) {
  hosted_state *f = (hosted_state *)arg;
  cnet_client_config network = host_config();
  cnet_observer out = host_observer(&f->out_probe);
  cnet_stream_endpoint bind = CNET_STREAM_ENDPOINT_INIT;
  native_io_request accepted = {0};
  f->owner_thread = cmeta_thread_current_token();
  f->in_probe.host = f; f->in_probe.client = &f->inbound;
  f->in_probe.expected = "client";
  f->out_probe.host = f; f->out_probe.client = &f->outbound;
  f->out_probe.expected = "server";

  HOST_TASK_OK(f, native_io_sharded_context_acquire_host(
      context, host_quiescent, f, &f->lease, &f->backend));
  HOST_TASK_OK(f, cnet_client_init_external(&f->inbound, &network, f->backend));
  HOST_TASK_OK(f, cnet_client_init_external(&f->outbound, &network, f->backend));
  bind.family = CNET_DATAGRAM_ADDRESS_IPV4;
  bind.address[0] = 127u; bind.address[3] = 1u;
  HOST_TASK_OK(f, cnet_listener_open(&f->listener, host_kind(),
                                     CNET_DATAGRAM_ADDRESS_IPV4));
  HOST_TASK_OK(f, cnet_listener_bind_open_endpoint(&f->listener, &bind));
  HOST_TASK_OK(f, cnet_listener_local_endpoint(&f->listener, &f->local));
  HOST_TASK_OK(f, cnet_listener_listen(&f->listener, 8u));
  HOST_TASK_OK(f, cnet_listener_attach_external(&f->listener, f->backend));
  HOST_TASK_OK(f, cnet_listener_submit_external_accept(&f->listener, &accepted));
  f->accepting = true;
#if defined(__linux__)
  {
    int handles[2] = {-1, -1};
    native_io_sharded_operation op = {0};
    native_io_sharded_ownership ownership = {
        host_pipe_terminal, host_pipe_finalize, f};
    if (pipe(handles) != 0) { host_record_error(f, SALTS_EIO); return; }
    f->pipe_read = handles[0]; f->pipe_write = handles[1];
    {
      int flags = fcntl(f->pipe_read, F_GETFL, 0);
      if (flags < 0 || fcntl(f->pipe_read, F_SETFL, flags | O_NONBLOCK) != 0) {
        host_record_error(f, SALTS_EIO); return;
      }
    }
    HOST_TASK_OK(f, native_io_sharded_context_attach_pipe(
        context, (uintptr_t)f->pipe_read, NATIVE_IO_PIPE_ENDPOINT_ASYNC_CAPABLE,
        &f->pipe_endpoint));
    op.kind = NATIVE_IO_OPERATION_PIPE_READ;
    op.endpoint = f->pipe_endpoint;
    op.buffer = &f->pipe_byte;
    op.length = 1u;
    HOST_TASK_OK(f, native_io_sharded_context_submit_owned(
        context, &op, &ownership, &f->pipe_request));
    if (write(f->pipe_write, "X", 1u) != 1) {
      host_record_error(f, SALTS_EIO); return;
    }
  }
#endif
  HOST_TASK_OK(f, cnet_connect_endpoint(&f->outbound, &f->local, NULL,
                                         &out, &f->outgoing));
}

static void host_send(hosted_state *f, cnet_client *client,
                      cnet_connection connection, const char *payload, size_t size) {
  mem_buffer_t *buffer = mem_get_buffer(mem_global(), size);
  int status;
  if (buffer == NULL) { host_record_error(f, SALTS_ENOMEM); return; }
  memcpy(mem_buffer_data(buffer), payload, size);
  mem_set_used(buffer, size);
  status = cnet_send_buffer(client, connection, buffer);
  mem_buffer_release(buffer);
  if (status != SALTS_OK) host_record_error(f, status);
}
static void host_progress(native_io_sharded_context *context, void *arg) {
  hosted_state *f = (hosted_state *)arg;
  native_io_sharded_completion observed[HOST_BATCH];
  size_t count = 0u, events = 0u;
  int status;
  if (f->status != SALTS_OK || f->done) return;
  HOST_TASK_OK(f, cnet_client_advance_external(&f->inbound, &events));
  HOST_TASK_OK(f, cnet_client_advance_external(&f->outbound, &events));
  status = native_io_sharded_context_observe_host(
      context, f->lease, observed, HOST_BATCH, 0u, &count);
  if (status != SALTS_OK && status != SALTS_ETIMEDOUT) {
    host_record_error(f, status); return;
  }
  {
    cnet_client *clients[2] = {&f->inbound, &f->outbound};
    cnet_sg_host_routes routes = {
        sizeof(routes), CNET_SG_HOST_ROUTING_VERSION, &f->listener, clients, 2u};
    size_t accepts = 0u, sg_completed = 0u;
    status = cnet_sg_host_route_batch(
        observed, count, &routes, &accepts, &sg_completed);
    if (status != SALTS_OK) { host_record_error(f, status); return; }
    if (accepts != 0u && f->accepting) {
      cnet_observer in = host_observer(&f->in_probe);
      HOST_TASK_OK(f, cnet_listener_accept(
          &f->listener, &f->inbound, &in, &f->accepted));
      f->accepting = false;
    }
  }
  HOST_TASK_OK(f, cnet_client_advance_external(&f->inbound, &events));
  HOST_TASK_OK(f, cnet_client_advance_external(&f->outbound, &events));
  if (!f->data_started && f->in_probe.connected && f->out_probe.connected) {
    f->data_started = true;
    HOST_TASK_OK(f, cnet_receive(&f->inbound, f->accepted, 1u));
    HOST_TASK_OK(f, cnet_receive(&f->outbound, f->outgoing, 1u));
    host_send(f, &f->outbound, f->outgoing, "client", 6u);
    host_send(f, &f->inbound, f->accepted, "server", 6u);
    if (f->status != SALTS_OK) return;
  }
  if (!f->closing && f->data_started &&
      f->in_probe.bytes == 6u && f->out_probe.bytes == 6u &&
      f->in_probe.sent == 6u && f->out_probe.sent == 6u
#if defined(__linux__)
      && f->sg_terminal == 1u && f->sg_finalize == 1u
#endif
      ) {
    f->closing = true;
    HOST_TASK_OK(f, cnet_listener_close(&f->listener));
    HOST_TASK_OK(f, cnet_listener_destroy(&f->listener));
    HOST_TASK_OK(f, cnet_close(&f->inbound, f->accepted));
    HOST_TASK_OK(f, cnet_close(&f->outbound, f->outgoing));
  }
  if (f->closing && f->in_probe.terminal && f->out_probe.terminal) {
    HOST_TASK_OK(f, cnet_client_stop_external(&f->inbound));
    HOST_TASK_OK(f, cnet_client_stop_external(&f->outbound));
    HOST_TASK_OK(f, cnet_client_destroy(&f->inbound));
    HOST_TASK_OK(f, cnet_client_destroy(&f->outbound));
#if defined(__linux__)
    HOST_TASK_OK(f, native_io_sharded_context_release_pipe(
        context, f->pipe_endpoint));
    (void)close(f->pipe_read); (void)close(f->pipe_write);
    f->pipe_read = -1; f->pipe_write = -1;
#endif
    f->clients_destroyed = true;
    HOST_TASK_OK(f, native_io_sharded_context_release_host(context, f->lease));
    f->done = true;
  }
}

spec("CNet native SG leased host with real TCP + SG-owned completions") {
  it("co-drives inbound and outbound without stealing SG completions or blocking the Owner") {
    hosted_state f = {0};
    const native_io_sharded_config config = {
        1u, 8u, {host_kind(), 24u, 48u, HOST_BATCH}};
    native_io_sharded_task initialize = {host_initialize, NULL, NULL, &f};
    native_io_sharded_task progress = {host_progress, NULL, NULL, &f};
    uint64_t deadline = cmeta_monotonic_ms() + HOST_TIMEOUT_MS;
    check_equal(native_io_sharded_create(&config, &f.sg), SALTS_OK);
    check_equal(native_io_sharded_submit_to(f.sg, 0u, &initialize), SALTS_OK);
    check_equal(native_io_sharded_wait(f.sg), SALTS_OK);
    check_equal(f.status, SALTS_OK);
    while (!f.done && f.status == SALTS_OK && cmeta_monotonic_ms() < deadline) {
      check_equal(native_io_sharded_submit_to(f.sg, 0u, &progress), SALTS_OK);
      check_equal(native_io_sharded_wait(f.sg), SALTS_OK);
      if (!f.done) cmeta_sleep_ms(1u);
    }
    check_equal(f.status, SALTS_OK);
    check(f.done);
    check_equal(f.in_probe.bytes, (size_t)6u);
    check_equal(f.out_probe.bytes, (size_t)6u);
    check_equal(f.in_probe.connected, (size_t)1u);
    check_equal(f.out_probe.connected, (size_t)1u);
    check_equal(f.in_probe.terminal, (size_t)1u);
    check_equal(f.out_probe.terminal, (size_t)1u);
#if defined(__linux__)
    check_equal(f.sg_terminal, (size_t)1u);
    check_equal(f.sg_finalize, (size_t)1u);
#endif
    if (f.done) {
      check_equal(native_io_sharded_shutdown(f.sg), SALTS_OK);
      check_equal(native_io_sharded_destroy(f.sg), SALTS_OK);
    }
  }
}
