#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "cnet_module.h"
#include "cnet_shards.h"
#include "cnet_transport.h"

#include "cnet_io_benchmark_config.h"

#include <salts/clock.h>
#include <salts/disruptor.h>
#include <salts/error_codes.h>
#include <salts/thread.h>

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum {
  CENTRAL_LANES = 2,
  CENTRAL_WARMUPS = 8,
  CENTRAL_SAMPLES = 64,
  CENTRAL_REPEATS = 7,
  CENTRAL_PAYLOAD_COUNT = 2,
  CENTRAL_TIMEOUT_MS = 5000,
  CENTRAL_COMMAND_CAPACITY = 64,
  CENTRAL_EVENT_CAPACITY = 64,
  CENTRAL_SHARD_COMMAND_CAPACITY = 32,
  CENTRAL_SHARD_EVENT_CAPACITY = 128,
  CENTRAL_REQUEST_CAPACITY = 32
};

static const size_t CENTRAL_PAYLOADS[] = {1024u, 65536u};

typedef enum central_command_kind {
  CENTRAL_COMMAND_NONE = 0,
  CENTRAL_COMMAND_RECEIVE,
  CENTRAL_COMMAND_SEND,
  CENTRAL_COMMAND_CLOSE,
  CENTRAL_COMMAND_RECYCLE
} central_command_kind;

typedef struct central_command {
  central_command_kind kind;
  cnet_shard_connection connection;
  mem_buffer_t *buffer;
  size_t demand;
} central_command;

typedef struct central_mailbox {
  disruptor_t *ring;
  atomic_uint_fast64_t published;
  atomic_uint_fast64_t rejected;
} central_mailbox;

typedef struct central_event_entry {
  uint32_t shard;
  cnet_event_kind kind;
  cnet_session_handle session;
  cnet_event_state state;
  int status;
  cnet_session_stage stage;
  size_t size;
  size_t argument;
  mem_buffer_t *backing;
} central_event_entry;

typedef struct central_event_mailbox {
  disruptor_t *ring;
  salts_mutex_t mutex;
  salts_cond_t available;
  atomic_size_t pending;
  atomic_uint_fast64_t published;
  atomic_uint_fast64_t rejected;
} central_event_mailbox;

typedef struct central_peer {
  int listener;
  int accepted;
  struct sockaddr_in address;
  size_t payload_size;
  size_t cycles;
  unsigned char *scratch;
  pthread_t thread;
  int status;
  bool thread_started;
} central_peer;

typedef struct central_lane {
  cnet_shards *shards;
  cnet_shard_connection connection;
  central_mailbox commands;
  mem_buffer_t *buffer;
  unsigned char *payload;
  size_t payload_size;
  size_t receive_offset;
  size_t current_sample;
  size_t target_samples;
  size_t completed_samples;
  size_t send_terminals;
  size_t receive_terminals;
  uint64_t started_ns;
  uint64_t *latencies;
  int status;
  bool connected;
  bool send_done;
  bool receive_done;
  bool terminal;
  atomic_bool recycled;
  atomic_int recycle_status;
} central_lane;

typedef struct central_owner_arg {
  central_lane *lane;
  atomic_bool *stop;
  atomic_bool *measure_cpu;
  int cpu;
  int observed_cpu;
  uint64_t cpu_ns;
  uint64_t command_hops;
  int status;
} central_owner_arg;

typedef struct central_sample {
  const char *backend;
  const char *topology;
  const char *mode;
  size_t payload_size;
  size_t repeat;
  size_t logical_operations;
  int cpu_a;
  int cpu_b;
  uint32_t shard_a;
  uint32_t shard_b;
  uint64_t wall_ns;
  uint64_t owner_cpu_ns;
  uint64_t central_cpu_ns;
  uint64_t p50_ns;
  uint64_t p95_ns;
  uint64_t p99_ns;
  double operations_per_second;
  double mib_per_second;
  double owner_cpu_us_per_op;
  double central_cpu_us_per_op;
  uint64_t command_hops;
  uint64_t event_hops;
  uint64_t command_rejects;
  uint64_t event_rejects;
  size_t send_terminals;
  size_t receive_terminals;
} central_sample;

static int central_socket_error(void) {
  return errno == 0 ? SALTS_EIO : -errno;
}

static int central_set_nodelay(int fd) {
  const int enabled = 1;
  return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled,
                    sizeof(enabled)) == 0
             ? SALTS_OK
             : central_socket_error();
}

static int central_read_full(int fd, unsigned char *buffer, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    const ssize_t count = read(fd, buffer + offset, size - offset);
    if (count > 0) {
      offset += (size_t)count;
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    return count == 0 ? SALTS_EOF : central_socket_error();
  }
  return SALTS_OK;
}

static int central_write_full(int fd, const unsigned char *buffer, size_t size) {
  size_t offset = 0u;
  while (offset < size) {
    const ssize_t count = write(fd, buffer + offset, size - offset);
    if (count > 0) {
      offset += (size_t)count;
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    return count == 0 ? SALTS_EIO : central_socket_error();
  }
  return SALTS_OK;
}

static void *central_peer_entry(void *user) {
  central_peer *peer = (central_peer *)user;
  int status = SALTS_OK;

  do {
    peer->accepted = accept(peer->listener, NULL, NULL);
  } while (peer->accepted < 0 && errno == EINTR);
  if (peer->accepted < 0) status = central_socket_error();
  if (status == SALTS_OK) status = central_set_nodelay(peer->accepted);

  for (size_t cycle = 0u;
       cycle < peer->cycles && status == SALTS_OK; ++cycle) {
    status = central_read_full(
        peer->accepted, peer->scratch, peer->payload_size);
    if (status == SALTS_OK)
      status = central_write_full(
          peer->accepted, peer->scratch, peer->payload_size);
  }

  peer->status = status;
  return NULL;
}

static void central_peer_reset(central_peer *peer) {
  memset(peer, 0, sizeof(*peer));
  peer->listener = -1;
  peer->accepted = -1;
  peer->status = SALTS_OK;
}

static int central_peer_init(
    central_peer *peer, size_t payload_size, size_t cycles) {
  socklen_t address_size = sizeof(peer->address);
  const int reuse = 1;
  int status;

  central_peer_reset(peer);
  peer->payload_size = payload_size;
  peer->cycles = cycles;
  peer->scratch = (unsigned char *)malloc(payload_size);
  if (peer->scratch == NULL) return SALTS_ENOMEM;

  peer->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (peer->listener < 0) return central_socket_error();
  (void)setsockopt(peer->listener, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse));

  memset(&peer->address, 0, sizeof(peer->address));
  peer->address.sin_family = AF_INET;
  peer->address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  peer->address.sin_port = 0u;
  if (bind(peer->listener, (const struct sockaddr *)&peer->address,
           sizeof(peer->address)) != 0)
    return central_socket_error();
  if (getsockname(peer->listener, (struct sockaddr *)&peer->address,
                  &address_size) != 0)
    return central_socket_error();
  if (listen(peer->listener, 1) != 0)
    return central_socket_error();

  status = pthread_create(&peer->thread, NULL, central_peer_entry, peer);
  if (status != 0) return -status;
  peer->thread_started = true;
  return SALTS_OK;
}

static int central_peer_destroy(central_peer *peer, bool abort_peer) {
  int status = SALTS_OK;

  if (abort_peer && peer->listener >= 0)
    (void)shutdown(peer->listener, SHUT_RDWR);
  if (abort_peer && peer->accepted >= 0)
    (void)shutdown(peer->accepted, SHUT_RDWR);

  if (peer->thread_started) {
    const int join_status = pthread_join(peer->thread, NULL);
    if (join_status != 0) status = -join_status;
    peer->thread_started = false;
    if (status == SALTS_OK && !abort_peer &&
        peer->status != SALTS_OK)
      status = peer->status;
  }

  if (peer->accepted >= 0) {
    (void)close(peer->accepted);
    peer->accepted = -1;
  }
  if (peer->listener >= 0) {
    (void)close(peer->listener);
    peer->listener = -1;
  }
  free(peer->scratch);
  peer->scratch = NULL;
  return status;
}

static int central_set_affinity(int cpu) {
  cpu_set_t set;
  int status;

  if (cpu < 0 || cpu >= CPU_SETSIZE) return SALTS_EINVAL;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  status = pthread_setaffinity_np(
      pthread_self(), sizeof(set), &set);
  return status == 0 ? SALTS_OK : -status;
}

static uint64_t central_thread_cpu_ns(void) {
  struct timespec value;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0)
    return 0u;
  return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
         (uint64_t)value.tv_nsec;
}

static int central_mailbox_init(central_mailbox *mailbox) {
  const disruptor_config_t config = {
      sizeof(central_command),
      CENTRAL_COMMAND_CAPACITY,
      1u,
      DISRUPTOR_MODE_WORKER_POOL};

  if (mailbox == NULL) return SALTS_EINVAL;
  memset(mailbox, 0, sizeof(*mailbox));
  mailbox->ring = disruptor_create(&config);
  if (mailbox->ring == NULL) return SALTS_ENOMEM;
  atomic_init(&mailbox->published, 0u);
  atomic_init(&mailbox->rejected, 0u);
  return SALTS_OK;
}

static void central_mailbox_destroy(central_mailbox *mailbox) {
  if (mailbox != NULL && mailbox->ring != NULL) {
    disruptor_destroy(mailbox->ring);
    mailbox->ring = NULL;
  }
}

static int central_mailbox_publish(
    central_mailbox *mailbox, const central_command *command) {
  disruptor_cursor_t cursor = {0};
  central_command *entry;
  mem_buffer_t *retained = NULL;

  if (mailbox == NULL || mailbox->ring == NULL ||
      command == NULL || command->kind == CENTRAL_COMMAND_NONE)
    return SALTS_EINVAL;
  if (command->kind == CENTRAL_COMMAND_SEND) {
    if (command->buffer == NULL) return SALTS_EINVAL;
    retained = mem_buffer_retain(command->buffer);
    if (retained == NULL) return SALTS_ENOMEM;
  }

  if (!disruptor_publisher_try_claim(mailbox->ring, &cursor)) {
    if (retained != NULL) mem_buffer_release(retained);
    atomic_fetch_add_explicit(
        &mailbox->rejected, 1u, memory_order_relaxed);
    return SALTS_ENOBUFS;
  }

  entry = (central_command *)disruptor_acquire_entry(
      mailbox->ring, &cursor);
  *entry = *command;
  if (command->kind == CENTRAL_COMMAND_SEND)
    entry->buffer = retained;

  (void)disruptor_publisher_publish(mailbox->ring, &cursor);
  atomic_fetch_add_explicit(
      &mailbox->published, 1u, memory_order_relaxed);
  return SALTS_OK;
}

static int central_event_mailbox_init(central_event_mailbox *mailbox) {
  const disruptor_config_t config = {
      sizeof(central_event_entry),
      CENTRAL_EVENT_CAPACITY,
      1u,
      DISRUPTOR_MODE_WORKER_POOL};

  if (mailbox == NULL) return SALTS_EINVAL;
  memset(mailbox, 0, sizeof(*mailbox));
  mailbox->ring = disruptor_create(&config);
  if (mailbox->ring == NULL) return SALTS_ENOMEM;
  salts_mutex_init(&mailbox->mutex);
  salts_cond_init(&mailbox->available);
  if (mailbox->mutex == NULL || mailbox->available == NULL) {
    if (mailbox->available != NULL)
      salts_cond_destroy(&mailbox->available);
    if (mailbox->mutex != NULL)
      salts_mutex_destroy(&mailbox->mutex);
    disruptor_destroy(mailbox->ring);
    mailbox->ring = NULL;
    return SALTS_ENOMEM;
  }
  atomic_init(&mailbox->pending, 0u);
  atomic_init(&mailbox->published, 0u);
  atomic_init(&mailbox->rejected, 0u);
  return SALTS_OK;
}

static void central_event_mailbox_destroy(
    central_event_mailbox *mailbox) {
  if (mailbox == NULL) return;
  if (mailbox->available != NULL)
    salts_cond_destroy(&mailbox->available);
  if (mailbox->mutex != NULL)
    salts_mutex_destroy(&mailbox->mutex);
  if (mailbox->ring != NULL)
    disruptor_destroy(mailbox->ring);
  memset(mailbox, 0, sizeof(*mailbox));
}

static int central_event_sink(
    void *context, uint32_t shard, const cnet_event *event) {
  central_event_mailbox *mailbox =
      (central_event_mailbox *)context;
  disruptor_cursor_t cursor = {0};
  central_event_entry *entry;
  mem_buffer_t *retained = NULL;

  if (mailbox == NULL || mailbox->ring == NULL || event == NULL)
    return SALTS_EINVAL;
  if (event->size != 0u) {
    if (event->kind != CNET_EVENT_RECEIVE ||
        event->backing == NULL)
      return SALTS_EPROTO;
    retained = mem_buffer_retain(event->backing);
    if (retained == NULL) return SALTS_ENOMEM;
  }

  if (!disruptor_publisher_try_claim(mailbox->ring, &cursor)) {
    if (retained != NULL) mem_buffer_release(retained);
    atomic_fetch_add_explicit(
        &mailbox->rejected, 1u, memory_order_relaxed);
    return SALTS_ENOBUFS;
  }

  entry = (central_event_entry *)disruptor_acquire_entry(
      mailbox->ring, &cursor);
  *entry = (central_event_entry){
      shard,
      event->kind,
      event->session,
      event->state,
      event->status,
      event->stage,
      event->size,
      event->argument,
      retained};

  /*
   * Reserve the pending count before publishing. The consumer may observe the
   * reservation before the ring cursor becomes visible and retry briefly, but
   * it can never claim a published entry and decrement pending from zero.
   */
  atomic_fetch_add_explicit(
      &mailbox->pending, 1u, memory_order_release);
  (void)disruptor_publisher_publish(mailbox->ring, &cursor);
  atomic_fetch_add_explicit(
      &mailbox->published, 1u, memory_order_relaxed);

  salts_mutex_lock(&mailbox->mutex);
  salts_cond_signal(&mailbox->available);
  salts_mutex_unlock(&mailbox->mutex);
  return SALTS_OK;
}

static int central_event_take_wait(
    central_event_mailbox *mailbox,
    central_event_entry *out_event,
    uint64_t deadline_ms) {
  for (;;) {
    disruptor_cursor_t cursor = {0};
    if (disruptor_worker_try_claim(mailbox->ring, &cursor)) {
      const central_event_entry *entry =
          (const central_event_entry *)disruptor_show_entry(
              mailbox->ring, &cursor);
      *out_event = *entry;
      disruptor_worker_release_entry(mailbox->ring, &cursor);
      atomic_fetch_sub_explicit(
          &mailbox->pending, 1u, memory_order_release);
      return SALTS_OK;
    }

    if (salts_monotonic_ms() >= deadline_ms)
      return SALTS_ETIMEDOUT;

    salts_mutex_lock(&mailbox->mutex);
    if (atomic_load_explicit(
            &mailbox->pending, memory_order_acquire) == 0u) {
      const uint64_t now = salts_monotonic_ms();
      const uint64_t remaining_ms =
          deadline_ms > now ? deadline_ms - now : 0u;
      const uint64_t timeout_ns =
          remaining_ms > UINT64_MAX / UINT64_C(1000000)
              ? UINT64_MAX
              : remaining_ms * UINT64_C(1000000);
      if (timeout_ns == 0u) {
        salts_mutex_unlock(&mailbox->mutex);
        return SALTS_ETIMEDOUT;
      }
      (void)salts_cond_timedwait(
          &mailbox->available, &mailbox->mutex, timeout_ns);
    }
    salts_mutex_unlock(&mailbox->mutex);
  }
}

static int central_owner_drain_commands(
    central_owner_arg *arg) {
  central_mailbox *mailbox = &arg->lane->commands;

  for (;;) {
    disruptor_cursor_t cursor = {0};
    const central_command *entry;
    int status = SALTS_OK;

    if (!disruptor_worker_try_claim(mailbox->ring, &cursor))
      return SALTS_OK;

    entry = (const central_command *)disruptor_show_entry(
        mailbox->ring, &cursor);

    if (entry->connection.shard !=
        arg->lane->connection.shard) {
      status = SALTS_EPROTO;
    } else if (entry->kind == CENTRAL_COMMAND_RECEIVE) {
      status = cnet_shards_receive_direct(
          arg->lane->shards, entry->connection, entry->demand);
    } else if (entry->kind == CENTRAL_COMMAND_SEND) {
      status = cnet_shards_send_buffer_direct(
          arg->lane->shards, entry->connection, entry->buffer);
      mem_buffer_release(entry->buffer);
    } else if (entry->kind == CENTRAL_COMMAND_CLOSE) {
      status = cnet_shards_close_direct(
          arg->lane->shards, entry->connection);
      if (status == SALTS_EBUSY)
        status = cnet_shards_close(
            arg->lane->shards, entry->connection);
    } else if (entry->kind == CENTRAL_COMMAND_RECYCLE) {
      cnet_session_terminal terminal = {0};
      status = cnet_shards_recycle(
          arg->lane->shards, entry->connection, &terminal);
      if (status == SALTS_OK && terminal.status != SALTS_OK)
        status = terminal.status;
      atomic_store_explicit(
          &arg->lane->recycle_status, status, memory_order_release);
      atomic_store_explicit(
          &arg->lane->recycled, true, memory_order_release);
    } else {
      status = SALTS_EINVAL;
    }

    ++arg->command_hops;
    disruptor_worker_release_entry(mailbox->ring, &cursor);
    if (status != SALTS_OK && status != SALTS_EALREADY)
      return status;
  }
}

static void *central_owner_entry(void *user) {
  central_owner_arg *arg = (central_owner_arg *)user;
  int status = central_set_affinity(arg->cpu);

  arg->status = status;
  arg->observed_cpu = -1;
  if (status != SALTS_OK) return NULL;

  arg->observed_cpu = sched_getcpu();
  if (arg->observed_cpu != arg->cpu) {
    arg->status = SALTS_EPROTO;
    return NULL;
  }

  status = cnet_shards_init_owner_experimental(
      arg->lane->shards, arg->lane->connection.shard);
  if (status != SALTS_OK) {
    arg->status = status;
    return NULL;
  }

  while (!atomic_load_explicit(arg->stop, memory_order_acquire)) {
    const bool measure =
        atomic_load_explicit(arg->measure_cpu, memory_order_acquire);
    const uint64_t cpu_started =
        measure ? central_thread_cpu_ns() : 0u;

    status = central_owner_drain_commands(arg);
    if (status == SALTS_OK)
      status = cnet_shards_poll_owner(
          arg->lane->shards, arg->lane->connection.shard, 10u);

    if (measure)
      arg->cpu_ns += central_thread_cpu_ns() - cpu_started;
    if (status != SALTS_OK && status != SALTS_ETIMEDOUT)
      break;
    status = SALTS_OK;
  }
  if (status == SALTS_OK) {
    const int drain_status = central_owner_drain_commands(arg);
    if (drain_status != SALTS_OK) status = drain_status;
  }

  arg->status = status;
  return NULL;
}

static int central_publish_send(central_lane *lane) {
  const central_command send = {
      CENTRAL_COMMAND_SEND,
      lane->connection,
      lane->buffer,
      0u};
  int status = central_mailbox_publish(&lane->commands, &send);
  if (status == SALTS_OK)
    status = cnet_shards_wake_owner(
        lane->shards, lane->connection.shard);
  return status;
}

static int central_publish_receive_setup(
    central_lane *lane, size_t demand) {
  const central_command receive = {
      CENTRAL_COMMAND_RECEIVE,
      lane->connection,
      NULL,
      demand};
  int status = central_mailbox_publish(&lane->commands, &receive);
  if (status == SALTS_OK)
    status = cnet_shards_wake_owner(
        lane->shards, lane->connection.shard);
  return status;
}

static int central_start_lane_sample(
    central_lane *lane, size_t sample,
    uint64_t *latencies) {
  lane->send_done = false;
  lane->receive_done = false;
  lane->receive_offset = 0u;
  lane->current_sample = sample;
  lane->started_ns = salts_hrtime();
  lane->latencies = latencies;
  if (latencies != NULL) latencies[sample] = 0u;
  return central_publish_send(lane);
}

static int central_handle_event(
    central_lane lanes[CENTRAL_LANES],
    central_event_entry *event,
    bool measuring,
    uint64_t *event_hops) {
  central_lane *lane;

  if (event->shard >= CENTRAL_LANES) return SALTS_EPROTO;
  lane = &lanes[event->shard];

  if (event->session.slot != lane->connection.session.slot ||
      event->session.generation !=
          lane->connection.session.generation)
    return SALTS_EPROTO;

  if (event->kind == CNET_EVENT_STATE) {
    if (event->state == CNET_EVENT_STATE_CONNECTED) {
      lane->connected = true;
    } else if (event->state == CNET_EVENT_STATE_CLOSED) {
      lane->terminal = true;
    } else if (event->state == CNET_EVENT_STATE_FAILED) {
      lane->status =
          event->status != SALTS_OK ? event->status : SALTS_EIO;
      lane->terminal = true;
    }
  } else if (event->kind == CNET_EVENT_SEND) {
    if (event->argument != lane->payload_size)
      return SALTS_EPROTO;
    lane->send_done = true;
    if (measuring) {
      ++lane->send_terminals;
      ++*event_hops;
    }
  } else if (event->kind == CNET_EVENT_RECEIVE) {
    const unsigned char *data;
    if (event->backing == NULL ||
        event->size == 0u ||
        event->size > lane->payload_size - lane->receive_offset)
      return SALTS_EPROTO;

    data = (const unsigned char *)mem_buffer_const_data(
        event->backing);
    if (data == NULL ||
        memcmp(lane->payload + lane->receive_offset,
               data, event->size) != 0)
      return SALTS_EIO;

    lane->receive_offset += event->size;
    if (lane->receive_offset == lane->payload_size) {
      if (lane->latencies != NULL)
        lane->latencies[lane->current_sample] =
            salts_hrtime() - lane->started_ns;
      lane->receive_done = true;
      lane->receive_offset = 0u;
      if (measuring) {
        ++lane->receive_terminals;
        ++*event_hops;
      }
    }
  } else {
    return SALTS_EPROTO;
  }

  if (event->backing != NULL) {
    mem_buffer_release(event->backing);
    event->backing = NULL;
  }
  return lane->status;
}

static int central_run_phase(
    central_lane lanes[CENTRAL_LANES],
    central_event_mailbox *events,
    size_t samples,
    bool measuring,
    uint64_t latencies[CENTRAL_LANES][CENTRAL_SAMPLES],
    uint64_t *event_hops) {
  size_t total_completed = 0u;
  const size_t target = CENTRAL_LANES * samples;
  const uint64_t deadline =
      salts_monotonic_ms() + CENTRAL_TIMEOUT_MS * 4u;
  int status;

  for (size_t lane = 0u; lane < CENTRAL_LANES; ++lane) {
    lanes[lane].target_samples = samples;
    lanes[lane].completed_samples = 0u;
    status = central_start_lane_sample(
        &lanes[lane], 0u,
        measuring ? latencies[lane] : NULL);
    if (status != SALTS_OK) return status;
  }

  while (total_completed < target) {
    central_event_entry event = {0};
    status = central_event_take_wait(events, &event, deadline);
    if (status != SALTS_OK) return status;

    status = central_handle_event(
        lanes, &event, measuring, event_hops);
    if (status != SALTS_OK) {
      if (event.backing != NULL)
        mem_buffer_release(event.backing);
      return status;
    }

    {
      central_lane *lane = &lanes[event.shard];
      if (lane->send_done && lane->receive_done) {
        ++lane->completed_samples;
        ++total_completed;
        if (lane->completed_samples < samples) {
          status = central_start_lane_sample(
              lane, lane->completed_samples,
              measuring ? latencies[event.shard] : NULL);
          if (status != SALTS_OK) return status;
        }
      }
    }
  }

  return SALTS_OK;
}

static int central_wait_connected(
    central_lane lanes[CENTRAL_LANES],
    central_event_mailbox *events) {
  size_t connected = 0u;
  const uint64_t deadline =
      salts_monotonic_ms() + CENTRAL_TIMEOUT_MS * 2u;

  while (connected < CENTRAL_LANES) {
    central_event_entry event = {0};
    const int status =
        central_event_take_wait(events, &event, deadline);
    if (status != SALTS_OK) return status;

    if (event.kind != CNET_EVENT_STATE ||
        event.state != CNET_EVENT_STATE_CONNECTED ||
        event.shard >= CENTRAL_LANES) {
      if (event.backing != NULL)
        mem_buffer_release(event.backing);
      return SALTS_EPROTO;
    }

    if (!lanes[event.shard].connected) {
      lanes[event.shard].connected = true;
      ++connected;
    }

    if (event.backing != NULL)
      mem_buffer_release(event.backing);
  }

  return SALTS_OK;
}

static int central_close_connections(
    central_lane lanes[CENTRAL_LANES],
    central_event_mailbox *events) {
  size_t terminals = 0u;
  const uint64_t deadline =
      salts_monotonic_ms() + CENTRAL_TIMEOUT_MS * 2u;

  for (size_t lane = 0u; lane < CENTRAL_LANES; ++lane) {
    const central_command close = {
        CENTRAL_COMMAND_CLOSE,
        lanes[lane].connection,
        NULL,
        0u};
    int status = central_mailbox_publish(
        &lanes[lane].commands, &close);
    if (status != SALTS_OK) return status;
    status = cnet_shards_wake_owner(
        lanes[lane].shards, lanes[lane].connection.shard);
    if (status != SALTS_OK) return status;
  }

  while (terminals < CENTRAL_LANES) {
    central_event_entry event = {0};
    int status = central_event_take_wait(events, &event, deadline);
    if (status != SALTS_OK) return status;

    if (event.shard >= CENTRAL_LANES ||
        event.kind != CNET_EVENT_STATE ||
        (event.state != CNET_EVENT_STATE_CLOSED &&
         event.state != CNET_EVENT_STATE_FAILED)) {
      if (event.backing != NULL)
        mem_buffer_release(event.backing);
      return SALTS_EPROTO;
    }

    if (!lanes[event.shard].terminal) {
      lanes[event.shard].terminal = true;
      ++terminals;
    }
    if (event.state == CNET_EVENT_STATE_FAILED)
      lanes[event.shard].status =
          event.status != SALTS_OK ? event.status : SALTS_EIO;

    if (event.backing != NULL)
      mem_buffer_release(event.backing);
  }

  for (size_t lane = 0u; lane < CENTRAL_LANES; ++lane) {
    const central_command recycle = {
        CENTRAL_COMMAND_RECYCLE,
        lanes[lane].connection,
        NULL,
        0u};
    int status;
    if (lanes[lane].status != SALTS_OK)
      return lanes[lane].status;
    atomic_store_explicit(&lanes[lane].recycled, false, memory_order_release);
    atomic_store_explicit(
        &lanes[lane].recycle_status, SALTS_OK, memory_order_release);
    status = central_mailbox_publish(&lanes[lane].commands, &recycle);
    if (status != SALTS_OK) return status;
    status = cnet_shards_wake_owner(
        lanes[lane].shards, lanes[lane].connection.shard);
    if (status != SALTS_OK) return status;
  }

  for (size_t lane = 0u; lane < CENTRAL_LANES; ++lane) {
    while (!atomic_load_explicit(
        &lanes[lane].recycled, memory_order_acquire)) {
      if (salts_monotonic_ms() >= deadline)
        return SALTS_ETIMEDOUT;
      salts_sleep_ms(1u);
    }
    {
      const int status = atomic_load_explicit(
          &lanes[lane].recycle_status, memory_order_acquire);
      if (status != SALTS_OK) return status;
    }
  }

  return SALTS_OK;
}

static int central_u64_compare(const void *left, const void *right) {
  const uint64_t a = *(const uint64_t *)left;
  const uint64_t b = *(const uint64_t *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static uint64_t central_percentile(
    uint64_t *values, size_t count, unsigned percentile) {
  size_t index;
  qsort(values, count, sizeof(*values), central_u64_compare);
  index = ((count - 1u) * (size_t)percentile + 50u) / 100u;
  return values[index];
}

static int central_parse_cpu(const char *name, int *out_cpu) {
  const char *value = getenv(name);
  char *end = NULL;
  long parsed;

  if (out_cpu == NULL || value == NULL || *value == '\0')
    return SALTS_EINVAL;
  errno = 0;
  parsed = strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' ||
      parsed < 0 || parsed >= CPU_SETSIZE)
    return SALTS_EINVAL;
  *out_cpu = (int)parsed;
  return SALTS_OK;
}

static int central_double_compare(const void *left, const void *right) {
  const double a = *(const double *)left;
  const double b = *(const double *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}

static double central_double_median(double *values, size_t count) {
  qsort(values, count, sizeof(*values), central_double_compare);
  return values[count / 2u];
}

static int central_run_repeat(
    native_io_backend_kind backend_kind,
    const char *backend_name,
    const char *topology,
    size_t payload_size,
    size_t repeat,
    int cpu_a,
    int cpu_b,
    central_sample *out) {
  cnet_shards shards = {0};
  central_peer peers[CENTRAL_LANES];
  central_lane lanes[CENTRAL_LANES];
  central_owner_arg owner_args[CENTRAL_LANES];
  pthread_t owner_threads[CENTRAL_LANES];
  bool owner_started[CENTRAL_LANES] = {false, false};
  central_event_mailbox events;
  atomic_bool stop;
  atomic_bool measure_owner_cpu;
  uint64_t latencies[CENTRAL_LANES][CENTRAL_SAMPLES];
  uint64_t combined[CENTRAL_LANES * CENTRAL_SAMPLES];
  const size_t cycles = CENTRAL_WARMUPS + CENTRAL_SAMPLES;
  const cnet_shards_config config = {
      .backend_kind = backend_kind,
      .shard_count = CENTRAL_LANES,
      .connection_capacity_per_shard = 1u,
      .command_capacity_per_shard = CENTRAL_SHARD_COMMAND_CAPACITY,
      .request_capacity_per_shard = CENTRAL_REQUEST_CAPACITY,
      .completion_batch_capacity = CENTRAL_REQUEST_CAPACITY,
      .event_capacity_per_shard = CENTRAL_SHARD_EVENT_CAPACITY,
      .receive_buffer_bytes = payload_size,
      .max_command_payload_bytes = sizeof(cnet_owner_connect_payload),
      .write_capacity_per_shard = 8u,
      .max_write_payload_bytes = payload_size};
  uint64_t wall_started = 0u;
  uint64_t wall_ns = 0u;
  uint64_t central_cpu_started = 0u;
  uint64_t central_cpu_ns = 0u;
  uint64_t owner_cpu_ns = 0u;
  uint64_t event_hops = 0u;
  uint64_t command_hops = 0u;
  uint64_t command_rejects = 0u;
  size_t combined_count = 0u;
  bool module_initialized = false;
  bool events_initialized = false;
  int status = SALTS_OK;

  if (out == NULL) return SALTS_EINVAL;
  memset(out, 0, sizeof(*out));
  memset(lanes, 0, sizeof(lanes));
  memset(owner_args, 0, sizeof(owner_args));
  memset(&events, 0, sizeof(events));
  atomic_init(&stop, false);
  atomic_init(&measure_owner_cpu, false);
  for (size_t lane = 0u; lane < CENTRAL_LANES; ++lane)
    central_peer_reset(&peers[lane]);

  status = cnet_module_init();
  if (status != SALTS_OK) goto cleanup;
  module_initialized = true;

  status = central_event_mailbox_init(&events);
  if (status != SALTS_OK) goto cleanup;
  events_initialized = true;

  status = cnet_shards_init_multi_owner_experimental(&shards, &config);
  if (status != SALTS_OK) goto cleanup;
  status = cnet_shards_bind_event_sink(&shards, central_event_sink, &events);
  if (status != SALTS_OK) goto cleanup;

  for (size_t lane = 0u;
       lane < CENTRAL_LANES && status == SALTS_OK; ++lane) {
    cnet_owner_connect_payload payload = {0};

    status = central_peer_init(
        &peers[lane], payload_size, cycles);
    if (status != SALTS_OK) break;

    payload.scheme = CNET_URI_TCP;
    payload.socket_options =
        (cnet_stream_socket_options)CNET_STREAM_SOCKET_OPTIONS_INIT;
    payload.socket_options.nodelay = 1;
    status = cnet_transport_parse_numeric_address(
        "127.0.0.1",
        ntohs(peers[lane].address.sin_port),
        payload.address, sizeof(payload.address),
        &payload.address_length);
    if (status != SALTS_OK) break;

    status = cnet_shards_connect(
        &shards, &payload, &lanes[lane].connection);
    if (status != SALTS_OK) break;
    if (lanes[lane].connection.shard != (uint32_t)lane) {
      status = SALTS_EPROTO;
      break;
    }

    lanes[lane].shards = &shards;
    lanes[lane].payload_size = payload_size;
    lanes[lane].status = SALTS_OK;
    atomic_init(&lanes[lane].recycled, false);
    atomic_init(&lanes[lane].recycle_status, SALTS_OK);
    lanes[lane].payload = (unsigned char *)malloc(payload_size);
    if (lanes[lane].payload == NULL) {
      status = SALTS_ENOMEM;
      break;
    }
    memset(lanes[lane].payload, 0x5a, payload_size);
    lanes[lane].buffer = mem_wrap_external(
        lanes[lane].payload, payload_size, NULL, NULL);
    if (lanes[lane].buffer == NULL) {
      status = SALTS_ENOMEM;
      break;
    }

    status = central_mailbox_init(&lanes[lane].commands);
  }
  if (status != SALTS_OK) goto cleanup;

  for (size_t lane = 0u; lane < CENTRAL_LANES; ++lane) {
    owner_args[lane].lane = &lanes[lane];
    owner_args[lane].stop = &stop;
    owner_args[lane].measure_cpu = &measure_owner_cpu;
    owner_args[lane].cpu = lane == 0u ? cpu_a : cpu_b;
    {
      const int create_status = pthread_create(
          &owner_threads[lane], NULL,
          central_owner_entry, &owner_args[lane]);
      if (create_status != 0) {
        status = -create_status;
        break;
      }
    }
    owner_started[lane] = true;
  }
  if (status != SALTS_OK) goto cleanup;

  status = central_wait_connected(lanes, &events);
  if (status != SALTS_OK) goto cleanup;

  for (size_t lane = 0u; lane < CENTRAL_LANES; ++lane) {
    const size_t demand =
        payload_size * (CENTRAL_WARMUPS + CENTRAL_SAMPLES);
    status = central_publish_receive_setup(&lanes[lane], demand);
    if (status != SALTS_OK) goto cleanup;
  }

  status = central_run_phase(
      lanes, &events, CENTRAL_WARMUPS, false, latencies, &event_hops);
  if (status != SALTS_OK) goto cleanup;

  for (size_t lane = 0u; lane < CENTRAL_LANES; ++lane) {
    lanes[lane].send_terminals = 0u;
    lanes[lane].receive_terminals = 0u;
    atomic_store_explicit(
        &lanes[lane].commands.published, 0u, memory_order_relaxed);
    atomic_store_explicit(
        &lanes[lane].commands.rejected, 0u, memory_order_relaxed);
  }
  atomic_store_explicit(&events.published, 0u, memory_order_relaxed);
  atomic_store_explicit(&events.rejected, 0u, memory_order_relaxed);
  event_hops = 0u;

  atomic_store_explicit(
      &measure_owner_cpu, true, memory_order_release);
  wall_started = salts_hrtime();
  central_cpu_started = central_thread_cpu_ns();
  status = central_run_phase(
      lanes, &events, CENTRAL_SAMPLES, true, latencies, &event_hops);
  central_cpu_ns = central_thread_cpu_ns() - central_cpu_started;
  wall_ns = salts_hrtime() - wall_started;
  atomic_store_explicit(
      &measure_owner_cpu, false, memory_order_release);
  if (status != SALTS_OK) goto cleanup;

  for (size_t lane = 0u; lane < CENTRAL_LANES; ++lane) {
    command_hops += atomic_load_explicit(
        &lanes[lane].commands.published, memory_order_acquire);
    command_rejects += atomic_load_explicit(
        &lanes[lane].commands.rejected, memory_order_acquire);
    for (size_t sample = 0u; sample < CENTRAL_SAMPLES; ++sample)
      combined[combined_count++] = latencies[lane][sample];
  }

  if (combined_count != CENTRAL_LANES * CENTRAL_SAMPLES ||
      command_hops != combined_count ||
      event_hops != UINT64_C(2) * combined_count ||
      command_rejects != 0u ||
      atomic_load_explicit(&events.rejected, memory_order_acquire) != 0u ||
      wall_ns == 0u || central_cpu_ns == 0u) {
    status = SALTS_EPROTO;
    goto cleanup;
  }

  status = central_close_connections(lanes, &events);
  if (status != SALTS_OK) goto cleanup;

  atomic_store_explicit(&stop, true, memory_order_release);
  for (size_t lane = 0u; lane < CENTRAL_LANES; ++lane)
    (void)cnet_shards_wake_owner(&shards, (uint32_t)lane);

  for (size_t lane = 0u; lane < CENTRAL_LANES; ++lane) {
    if (owner_started[lane]) {
      const int join_status =
          pthread_join(owner_threads[lane], NULL);
      owner_started[lane] = false;
      if (status == SALTS_OK && join_status != 0)
        status = -join_status;
    }
    if (status == SALTS_OK &&
        owner_args[lane].status != SALTS_OK)
      status = owner_args[lane].status;
    if (owner_args[lane].observed_cpu != owner_args[lane].cpu) {
      status = SALTS_EPROTO;
    }
    owner_cpu_ns += owner_args[lane].cpu_ns;
  }
  if (status != SALTS_OK) goto cleanup;

  out->backend = backend_name;
  out->topology = topology;
  out->mode = "central_callback_compat";
  out->payload_size = payload_size;
  out->repeat = repeat;
  out->logical_operations = combined_count;
  out->cpu_a = cpu_a;
  out->cpu_b = cpu_b;
  out->shard_a = lanes[0].connection.shard;
  out->shard_b = lanes[1].connection.shard;
  out->wall_ns = wall_ns;
  out->owner_cpu_ns = owner_cpu_ns;
  out->central_cpu_ns = central_cpu_ns;
  out->p50_ns = central_percentile(combined, combined_count, 50u);
  out->p95_ns = central_percentile(combined, combined_count, 95u);
  out->p99_ns = central_percentile(combined, combined_count, 99u);
  out->operations_per_second =
      (double)combined_count * 1.0e9 / (double)wall_ns;
  out->mib_per_second =
      ((double)combined_count * (double)payload_size /
       (1024.0 * 1024.0)) * 1.0e9 / (double)wall_ns;
  out->owner_cpu_us_per_op =
      (double)owner_cpu_ns / 1000.0 / (double)combined_count;
  out->central_cpu_us_per_op =
      (double)central_cpu_ns / 1000.0 / (double)combined_count;
  out->command_hops = command_hops;
  out->event_hops = event_hops;
  out->command_rejects = command_rejects;
  out->event_rejects = atomic_load_explicit(
      &events.rejected, memory_order_acquire);
  out->send_terminals =
      lanes[0].send_terminals + lanes[1].send_terminals;
  out->receive_terminals =
      lanes[0].receive_terminals + lanes[1].receive_terminals;

cleanup:
  atomic_store_explicit(&stop, true, memory_order_release);
  if (shards.impl != NULL) {
    for (size_t lane = 0u; lane < CENTRAL_LANES; ++lane)
      (void)cnet_shards_wake_owner(&shards, (uint32_t)lane);
  }
  for (size_t lane = 0u; lane < CENTRAL_LANES; ++lane) {
    if (owner_started[lane]) {
      (void)pthread_join(owner_threads[lane], NULL);
      owner_started[lane] = false;
    }
  }

  if (shards.impl != NULL) {
    const int stop_status =
        cnet_shards_stop(&shards, CENTRAL_TIMEOUT_MS);
    if (status == SALTS_OK &&
        stop_status != SALTS_OK &&
        stop_status != SALTS_EALREADY)
      status = stop_status;
    if (cnet_shards_stopped(&shards)) {
      const int destroy_status = cnet_shards_destroy(&shards);
      if (status == SALTS_OK && destroy_status != SALTS_OK)
        status = destroy_status;
    }
  }

  for (size_t lane = 0u; lane < CENTRAL_LANES; ++lane) {
    central_mailbox_destroy(&lanes[lane].commands);
    if (lanes[lane].buffer != NULL) {
      if (status == SALTS_OK &&
          mem_buffer_ref_count(lanes[lane].buffer) != 1u)
        status = SALTS_EPROTO;
      mem_buffer_release(lanes[lane].buffer);
      lanes[lane].buffer = NULL;
    }
    free(lanes[lane].payload);
    lanes[lane].payload = NULL;
    {
      const int peer_status =
          central_peer_destroy(&peers[lane], status != SALTS_OK);
      if (status == SALTS_OK && peer_status != SALTS_OK)
        status = peer_status;
    }
  }

  if (events_initialized)
    central_event_mailbox_destroy(&events);
  if (module_initialized) {
    const int module_status = cnet_module_shutdown();
    if (status == SALTS_OK && module_status != SALTS_OK)
      status = module_status;
  }
  return status;
}

static FILE *central_open_csv(void) {
  const char *prefix = getenv("CNET_CENTRAL_CALLBACK_OUTPUT");
  char path[1024];

  if (prefix == NULL || *prefix == '\0') return NULL;
  if (snprintf(path, sizeof(path), "%s.csv", prefix) < 0)
    return NULL;
  return fopen(path, "w");
}

static int central_write_csv(FILE *csv, const central_sample *sample) {
  if (csv == NULL || sample == NULL) return SALTS_OK;
  return fprintf(
             csv,
             "%s,%s,%s,%zu,%zu,%zu,%d,%d,%u,%u,%" PRIu64
             ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
             ",%" PRIu64 ",%.6f,%.6f,%.6f,%.6f,%" PRIu64
             ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%zu,%zu\n",
             sample->backend, sample->topology, sample->mode,
             sample->payload_size, sample->repeat,
             sample->logical_operations, sample->cpu_a, sample->cpu_b,
             sample->shard_a, sample->shard_b,
             sample->wall_ns, sample->owner_cpu_ns,
             sample->central_cpu_ns,
             sample->p50_ns, sample->p95_ns, sample->p99_ns,
             sample->operations_per_second,
             sample->mib_per_second,
             sample->owner_cpu_us_per_op,
             sample->central_cpu_us_per_op,
             sample->command_hops, sample->event_hops,
             sample->command_rejects, sample->event_rejects,
             sample->send_terminals, sample->receive_terminals) < 0
             ? SALTS_EIO
             : SALTS_OK;
}

int main(void) {
  cnet_io_benchmark_backend backend = {0};
  const char *requested_backend = getenv("CNET_IO_BENCHMARK_BACKEND");
  const char *topology = getenv("CNET_OWNER_PARALLEL_TOPOLOGY");
  central_sample results[CENTRAL_PAYLOAD_COUNT][CENTRAL_REPEATS];
  FILE *csv = NULL;
  int cpu_a = -1;
  int cpu_b = -1;
  int status;

  if (topology == NULL || *topology == '\0')
    topology = "unspecified";

  status = cnet_io_benchmark_select_backend(
      requested_backend, &backend);
  if (status != SALTS_OK) {
    fprintf(stderr, "backend selection failed: status=%d\n", status);
    return 2;
  }
  if (backend.kind != NATIVE_IO_BACKEND_EPOLL &&
      backend.kind != NATIVE_IO_BACKEND_IO_URING) {
    fprintf(stderr,
            "CNet central-callback benchmark supports Linux epoll/io_uring only\n");
    return 2;
  }

  status = central_parse_cpu("CNET_OWNER_PARALLEL_CPU_A", &cpu_a);
  if (status == SALTS_OK)
    status = central_parse_cpu("CNET_OWNER_PARALLEL_CPU_B", &cpu_b);
  if (status != SALTS_OK) {
    fprintf(stderr, "invalid owner CPU affinity environment\n");
    return 2;
  }

  memset(results, 0, sizeof(results));
  for (size_t payload = 0u; payload < CENTRAL_PAYLOAD_COUNT; ++payload) {
    for (size_t repeat = 0u; repeat < CENTRAL_REPEATS; ++repeat) {
      status = central_run_repeat(
          backend.kind, backend.name, topology,
          CENTRAL_PAYLOADS[payload], repeat + 1u,
          cpu_a, cpu_b, &results[payload][repeat]);
      if (status != SALTS_OK) {
        fprintf(stderr,
                "CNet central-callback benchmark failed: backend=%s topology=%s payload=%zu repeat=%zu status=%d\n",
                backend.name, topology,
                CENTRAL_PAYLOADS[payload], repeat + 1u, status);
        return 1;
      }
    }
  }

  csv = central_open_csv();
  if (csv != NULL) {
    fprintf(
        csv,
        "backend,topology,mode,payload_bytes,repeat,logical_operations,"
        "cpu_a,cpu_b,shard_a,shard_b,wall_ns,owner_cpu_ns,central_cpu_ns,"
        "p50_ns,p95_ns,p99_ns,operations_per_second,mib_per_second,"
        "owner_cpu_us_per_op,central_cpu_us_per_op,command_hops,event_hops,"
        "command_rejects,event_rejects,send_terminals,receive_terminals\n");
  }

  printf("# CNet central-callback compatibility POC\n\n");
  printf("Backend: %s\n\n", backend.name);
  printf("Topology: %s; fixed I/O owners %d,%d.\n\n",
         topology, cpu_a, cpu_b);
  printf("The central application thread never drives NativeIO. Each logical "
         "RTT publishes one bounded retained SEND descriptor to the fixed "
         "owner (pointer retain only, no payload copy); receive demand is "
         "pre-admitted before timing to match the owner-affine baseline. "
         "The central thread then drains a "
         "bounded MPSC event aggregation where SEND+RECEIVE are callback-"
         "equivalent events. This measures the compatibility tax of keeping "
         "one central callback thread over multiple owners.\n\n");
  printf("| payload | ops/s median | MiB/s median | p50 us | p99 us | "
         "owner CPU us/op | central CPU us/op | cmd hops/op | event hops/op |\n");
  printf("| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");

  for (size_t payload = 0u; payload < CENTRAL_PAYLOAD_COUNT; ++payload) {
    double rate[CENTRAL_REPEATS];
    double mib[CENTRAL_REPEATS];
    double p50[CENTRAL_REPEATS];
    double p99[CENTRAL_REPEATS];
    double owner_cpu[CENTRAL_REPEATS];
    double central_cpu[CENTRAL_REPEATS];
    double cmd_hops[CENTRAL_REPEATS];
    double evt_hops[CENTRAL_REPEATS];

    for (size_t repeat = 0u; repeat < CENTRAL_REPEATS; ++repeat) {
      const central_sample *sample = &results[payload][repeat];
      rate[repeat] = sample->operations_per_second;
      mib[repeat] = sample->mib_per_second;
      p50[repeat] = (double)sample->p50_ns / 1000.0;
      p99[repeat] = (double)sample->p99_ns / 1000.0;
      owner_cpu[repeat] = sample->owner_cpu_us_per_op;
      central_cpu[repeat] = sample->central_cpu_us_per_op;
      cmd_hops[repeat] =
          (double)sample->command_hops / (double)sample->logical_operations;
      evt_hops[repeat] =
          (double)sample->event_hops / (double)sample->logical_operations;
      status = central_write_csv(csv, sample);
      if (status != SALTS_OK) goto cleanup;
    }

    printf("| %zu | %.0f | %.3f | %.3f | %.3f | %.3f | %.3f | %.3f | %.3f |\n",
           CENTRAL_PAYLOADS[payload],
           central_double_median(rate, CENTRAL_REPEATS),
           central_double_median(mib, CENTRAL_REPEATS),
           central_double_median(p50, CENTRAL_REPEATS),
           central_double_median(p99, CENTRAL_REPEATS),
           central_double_median(owner_cpu, CENTRAL_REPEATS),
           central_double_median(central_cpu, CENTRAL_REPEATS),
           central_double_median(cmd_hops, CENTRAL_REPEATS),
           central_double_median(evt_hops, CENTRAL_REPEATS));
  }

cleanup:
  if (csv != NULL && fclose(csv) != 0 && status == SALTS_OK)
    status = SALTS_EIO;
  return status == SALTS_OK ? 0 : 1;
}
