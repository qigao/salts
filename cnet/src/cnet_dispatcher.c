#include "cnet_dispatcher.h"

#include <turbo/clock.h>
#include <turbo/thread.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct cnet_dispatcher_impl cnet_dispatcher_impl;
typedef int (*cnet_dispatch_release_fn)(void *context, const cnet_dispatch_view *view,
                                        uint64_t token);

typedef struct cnet_dispatch_job {
  cnet_dispatch_fn invoke;
  void *context;
  cnet_event event;
  cnet_dispatch_release_fn release;
  void *release_context;
  uint64_t release_token;
} cnet_dispatch_job;

typedef struct cnet_dispatch_entry {
  cnet_dispatcher_impl *dispatcher;
  cnet_shard_connection connection;
  cnet_dispatch_fn observer;
  void *observer_context;
  bool active;
  bool close_requested;
} cnet_dispatch_entry;

typedef struct cnet_dispatch_lane {
  atomic_flag driving;
  atomic_bool pending;
  cnet_event_view event;
} cnet_dispatch_lane;

struct cnet_dispatcher_impl {
  cnet_shards *shards;
  cnet_dispatch_entry *entries;
  cnet_dispatch_lane *lanes;
  turbo_mutex_t lock;
  size_t shard_count;
  size_t connection_capacity_per_shard;
  size_t active_count;
  atomic_int first_error;
  bool admission_open;
  bool drained;
};

static cnet_dispatcher_impl *cnet_dispatcher_get(cnet_dispatcher *dispatcher) {
  return dispatcher != NULL ? (cnet_dispatcher_impl *)dispatcher->impl : NULL;
}

static cnet_dispatch_entry *cnet_dispatcher_entry(cnet_dispatcher_impl *impl,
                                                  cnet_shard_connection connection) {
  size_t index;
  if (impl == NULL || !cnet_shard_connection_valid(connection) ||
      (size_t)connection.shard >= impl->shard_count || connection.session.slot == 0u ||
      (size_t)connection.session.slot > impl->connection_capacity_per_shard)
    return NULL;
  index = (size_t)connection.shard * impl->connection_capacity_per_shard +
          (size_t)connection.session.slot - 1u;
  return &impl->entries[index];
}

static bool cnet_dispatcher_terminal_state(cnet_event_state state) {
  return state == CNET_EVENT_STATE_CLOSED || state == CNET_EVENT_STATE_FAILED;
}

static void cnet_dispatcher_record_error(cnet_dispatcher_impl *impl, int status) {
  int expected = TURBO_OK;
  if (status == TURBO_OK) return;
  (void)atomic_compare_exchange_strong_explicit(&impl->first_error, &expected, status,
                                                memory_order_acq_rel, memory_order_acquire);
}

static int cnet_dispatcher_recycle(cnet_dispatch_entry *entry, const cnet_dispatch_view *view) {
  cnet_dispatcher_impl *impl = entry->dispatcher;
  cnet_session_terminal terminal = {0};
  bool recycled = false;
  int status;

  status = cnet_shards_recycle(impl->shards, entry->connection, &terminal);
  if (status == TURBO_OK) {
    recycled = true;
    const bool valid_closed = view->state == CNET_EVENT_STATE_CLOSED &&
                              terminal.kind == CNET_SESSION_TERMINAL_CLOSED &&
                              terminal.status == TURBO_OK;
    const bool valid_failed = view->state == CNET_EVENT_STATE_FAILED &&
                              terminal.kind == CNET_SESSION_TERMINAL_FAILED &&
                              terminal.status == view->status && terminal.stage == view->stage;
    if (!valid_closed && !valid_failed) status = TURBO_EPROTO;
  }

  if (recycled) {
    turbo_mutex_lock(&impl->lock);
    if (entry->active && entry->connection.session.slot == view->session.slot &&
        entry->connection.session.generation == view->session.generation) {
      entry->active = false;
      entry->close_requested = false;
      entry->observer = NULL;
      entry->observer_context = NULL;
      if (impl->active_count == 0u) status = TURBO_EPROTO;
      else --impl->active_count;
    } else if (status == TURBO_OK) {
      status = TURBO_EPROTO;
    }
    turbo_mutex_unlock(&impl->lock);
  }
  cnet_dispatcher_record_error(impl, status);
  return status;
}

static int cnet_dispatcher_release_lease(void *context, const cnet_dispatch_view *view,
                                         uint64_t token) {
  cnet_dispatch_entry *entry = (cnet_dispatch_entry *)context;
  cnet_dispatcher_impl *impl;
  cnet_event_view event;
  bool terminal_event;
  int status;

  if (entry == NULL || view == NULL || token == 0u) return TURBO_EINVAL;
  impl = entry->dispatcher;
  if (impl == NULL) return TURBO_EINVAL;
  event = (cnet_event_view){view->kind, view->session, view->state,    view->status, view->stage,
                            view->data, view->size,    view->argument, token};
  terminal_event = view->kind == CNET_EVENT_STATE && cnet_dispatcher_terminal_state(view->state);
  status = cnet_shards_release_event(impl->shards, entry->connection.shard, &event);
  if (status != TURBO_OK || !terminal_event) {
    cnet_dispatcher_record_error(impl, status);
    return status;
  }
  return cnet_dispatcher_recycle(entry, view);
}

static int cnet_dispatcher_release_direct(void *context, const cnet_dispatch_view *view,
                                          uint64_t token) {
  cnet_dispatch_entry *entry = (cnet_dispatch_entry *)context;
  (void)token;
  if (entry == NULL || view == NULL || entry->dispatcher == NULL) return TURBO_EINVAL;
  return cnet_dispatcher_recycle(entry, view);
}

static int cnet_dispatcher_prepare(cnet_dispatcher_impl *impl, uint32_t shard,
                                   const cnet_event *event, cnet_dispatch_release_fn release,
                                   uint64_t release_token, cnet_dispatch_job *out_job) {
  const cnet_shard_connection connection = {shard, event->session};
  cnet_dispatch_entry *entry;

  turbo_mutex_lock(&impl->lock);
  entry = cnet_dispatcher_entry(impl, connection);
  if (entry == NULL || !entry->active ||
      entry->connection.session.generation != connection.session.generation) {
    turbo_mutex_unlock(&impl->lock);
    return TURBO_EBUSY;
  }
  *out_job = (cnet_dispatch_job){.invoke = entry->observer,
                                 .context = entry->observer_context,
                                 .event = {event->kind, event->session, event->state, event->status,
                                           event->stage, event->data, event->size, event->argument},
                                 .release = release,
                                 .release_context = entry,
                                 .release_token = release_token};
  turbo_mutex_unlock(&impl->lock);
  return TURBO_OK;
}

static int cnet_dispatcher_invoke(const cnet_dispatch_job *job) {
  const cnet_dispatch_view view = {job->event.kind,   job->event.session, job->event.state,
                                   job->event.status, job->event.stage,   job->event.data,
                                   job->event.size,   job->event.argument};
  int status = TURBO_OK;

  job->invoke(job->context, &view);
  if (job->release != NULL) status = job->release(job->release_context, &view, job->release_token);
  return status;
}

int cnet_dispatcher_init(cnet_dispatcher *dispatcher, cnet_shards *shards) {
  cnet_dispatcher_impl *impl;
  cnet_shards_layout layout = {0};
  size_t entry_count;
  size_t index;

  if (dispatcher == NULL || shards == NULL) return TURBO_EINVAL;
  if (dispatcher->impl != NULL) return TURBO_EALREADY;
  if (!cnet_shards_get_layout(shards, &layout) || layout.shard_count == 0u ||
      layout.connection_capacity_per_shard == 0u)
    return TURBO_EINVAL;
  if (layout.shard_count > SIZE_MAX / layout.connection_capacity_per_shard) return TURBO_ERANGE;
  entry_count = layout.shard_count * layout.connection_capacity_per_shard;
  if (entry_count > SIZE_MAX / sizeof(cnet_dispatch_entry) ||
      layout.shard_count > SIZE_MAX / sizeof(cnet_dispatch_lane))
    return TURBO_ERANGE;

  impl = (cnet_dispatcher_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return TURBO_ENOMEM;
  impl->entries = (cnet_dispatch_entry *)calloc(entry_count, sizeof(*impl->entries));
  impl->lanes = (cnet_dispatch_lane *)calloc(layout.shard_count, sizeof(*impl->lanes));
  if (impl->entries == NULL || impl->lanes == NULL) {
    free(impl->lanes);
    free(impl->entries);
    free(impl);
    return TURBO_ENOMEM;
  }
  impl->shards = shards;
  impl->shard_count = layout.shard_count;
  impl->connection_capacity_per_shard = layout.connection_capacity_per_shard;
  impl->admission_open = true;
  atomic_init(&impl->first_error, TURBO_OK);
  turbo_mutex_init(&impl->lock);
  for (index = 0u; index < entry_count; ++index)
    impl->entries[index].dispatcher = impl;
  for (index = 0u; index < impl->shard_count; ++index)
    atomic_flag_clear_explicit(&impl->lanes[index].driving, memory_order_release);
  for (index = 0u; index < impl->shard_count; ++index)
    atomic_init(&impl->lanes[index].pending, false);
  dispatcher->impl = impl;
  return TURBO_OK;
}

int cnet_dispatcher_register(cnet_dispatcher *dispatcher, cnet_shard_connection connection,
                             cnet_dispatch_fn observer, void *observer_context) {
  cnet_dispatcher_impl *impl = cnet_dispatcher_get(dispatcher);
  cnet_dispatch_entry *entry;
  cnet_session_state state = CNET_SESSION_FREE;
  int status;

  if (impl == NULL || observer == NULL) return TURBO_EINVAL;
  turbo_mutex_lock(&impl->lock);
  if (!impl->admission_open) {
    turbo_mutex_unlock(&impl->lock);
    return TURBO_ESHUTDOWN;
  }
  turbo_mutex_unlock(&impl->lock);
  status = cnet_shards_state(impl->shards, connection, &state);
  if (status != TURBO_OK) return status;
  entry = cnet_dispatcher_entry(impl, connection);
  if (entry == NULL) return TURBO_ENOENT;

  turbo_mutex_lock(&impl->lock);
  if (!impl->admission_open) status = TURBO_ESHUTDOWN;
  else if (entry->active) status = TURBO_EALREADY;
  else {
    entry->connection = connection;
    entry->observer = observer;
    entry->observer_context = observer_context;
    entry->active = true;
    entry->close_requested = state == CNET_SESSION_DRAINING || state == CNET_SESSION_TERMINAL;
    ++impl->active_count;
    status = TURBO_OK;
  }
  turbo_mutex_unlock(&impl->lock);
  return status;
}

int cnet_dispatcher_publish(cnet_dispatcher *dispatcher, uint32_t shard, const cnet_event *event) {
  cnet_dispatcher_impl *impl = cnet_dispatcher_get(dispatcher);
  cnet_dispatch_job job = {0};
  cnet_dispatch_release_fn release = NULL;
  int status;

  if (impl == NULL || event == NULL || (size_t)shard >= impl->shard_count) return TURBO_EINVAL;
  if (event->kind == CNET_EVENT_STATE && cnet_dispatcher_terminal_state(event->state))
    release = cnet_dispatcher_release_direct;
  status = cnet_dispatcher_prepare(impl, shard, event, release, 0u, &job);
  if (status == TURBO_OK) status = cnet_dispatcher_invoke(&job);
  return status;
}

int cnet_dispatcher_drive(cnet_dispatcher *dispatcher, uint32_t shard) {
  cnet_dispatcher_impl *impl = cnet_dispatcher_get(dispatcher);
  cnet_dispatch_lane *lane;
  cnet_dispatch_job job = {0};
  int status = TURBO_OK;

  if (impl == NULL || (size_t)shard >= impl->shard_count) return TURBO_EINVAL;
  lane = &impl->lanes[shard];
  if (atomic_flag_test_and_set_explicit(&lane->driving, memory_order_acquire)) return TURBO_EBUSY;
  if (!atomic_load_explicit(&lane->pending, memory_order_acquire)) {
    status = cnet_shards_take_event(impl->shards, shard, &lane->event);
    if (status != TURBO_OK) {
      atomic_flag_clear_explicit(&lane->driving, memory_order_release);
      return status;
    }
    atomic_store_explicit(&lane->pending, true, memory_order_release);
  }

  if (status == TURBO_OK) {
    const cnet_event event = {lane->event.kind,   lane->event.session, lane->event.state,
                              lane->event.status, lane->event.stage,   lane->event.data,
                              lane->event.size,   lane->event.argument};
    status = cnet_dispatcher_prepare(impl, shard, &event, cnet_dispatcher_release_lease,
                                     lane->event._sequence, &job);
  }
  if (status == TURBO_OK) status = cnet_dispatcher_invoke(&job);
  if (status == TURBO_OK) {
    memset(&lane->event, 0, sizeof(lane->event));
    atomic_store_explicit(&lane->pending, false, memory_order_release);
  } else if (status != TURBO_ENOBUFS && status != TURBO_EBUSY) {
    cnet_dispatcher_record_error(impl, status);
  }
  atomic_flag_clear_explicit(&lane->driving, memory_order_release);
  return status;
}

static bool cnet_dispatcher_is_idle(cnet_dispatcher_impl *impl) {
  size_t index;
  bool idle;
  turbo_mutex_lock(&impl->lock);
  idle = impl->active_count == 0u;
  turbo_mutex_unlock(&impl->lock);
  if (!idle) return false;
  for (index = 0u; index < impl->shard_count; ++index)
    if (atomic_load_explicit(&impl->lanes[index].pending, memory_order_acquire)) return false;
  return true;
}

static int cnet_dispatcher_drive_all(cnet_dispatcher_impl *impl) {
  cnet_dispatcher dispatcher = {impl};
  size_t index;
  int first_status = TURBO_ETIMEDOUT;
  for (index = 0u; index < impl->shard_count; ++index) {
    const int status = cnet_dispatcher_drive(&dispatcher, (uint32_t)index);
    if (status == TURBO_OK) first_status = TURBO_OK;
    else if (status != TURBO_ETIMEDOUT && status != TURBO_ENOBUFS && status != TURBO_EBUSY)
      return status;
  }
  return first_status;
}

int cnet_dispatcher_wait_idle(cnet_dispatcher *dispatcher, uint32_t timeout_ms) {
  cnet_dispatcher_impl *impl = cnet_dispatcher_get(dispatcher);
  const uint64_t started_ms = turbo_monotonic_ms();
  if (impl == NULL) return TURBO_EINVAL;
  for (;;) {
    const int error = atomic_load_explicit(&impl->first_error, memory_order_acquire);
    int status;
    if (error != TURBO_OK) return error;
    status = cnet_dispatcher_drive_all(impl);
    if (status != TURBO_OK && status != TURBO_ETIMEDOUT) return status;
    if (cnet_dispatcher_is_idle(impl)) return TURBO_OK;
    if (turbo_monotonic_ms() - started_ms >= timeout_ms) return TURBO_ETIMEDOUT;
    turbo_sleep_ms(1u);
  }
}

static int cnet_dispatcher_request_closes(cnet_dispatcher_impl *impl) {
  const size_t entry_count = impl->shard_count * impl->connection_capacity_per_shard;
  size_t index;
  int status = TURBO_OK;

  for (index = 0u; index < entry_count; ++index) {
    cnet_dispatch_entry *entry = &impl->entries[index];
    cnet_shard_connection connection = {0};
    bool claimed = false;
    int close_status;

    turbo_mutex_lock(&impl->lock);
    if (entry->active && !entry->close_requested) {
      connection = entry->connection;
      entry->close_requested = true;
      claimed = true;
    }
    turbo_mutex_unlock(&impl->lock);
    if (!claimed) continue;

    close_status = cnet_shards_close(impl->shards, connection);
    if (close_status == TURBO_OK || close_status == TURBO_EALREADY || close_status == TURBO_ENOENT)
      continue;

    turbo_mutex_lock(&impl->lock);
    if (entry->active && entry->connection.shard == connection.shard &&
        entry->connection.session.slot == connection.session.slot &&
        entry->connection.session.generation == connection.session.generation)
      entry->close_requested = false;
    turbo_mutex_unlock(&impl->lock);
    if (close_status != TURBO_ENOBUFS && status == TURBO_OK) {
      status = close_status;
    }
  }
  return status;
}

int cnet_dispatcher_drain(cnet_dispatcher *dispatcher, uint32_t timeout_ms) {
  cnet_dispatcher_impl *impl = cnet_dispatcher_get(dispatcher);
  const uint64_t started_ms = turbo_monotonic_ms();
  int first_status;
  if (impl == NULL) return TURBO_EINVAL;
  turbo_mutex_lock(&impl->lock);
  if (impl->drained) {
    turbo_mutex_unlock(&impl->lock);
    return TURBO_EALREADY;
  }
  impl->admission_open = false;
  turbo_mutex_unlock(&impl->lock);
  first_status = atomic_load_explicit(&impl->first_error, memory_order_acquire);

  for (;;) {
    int status = cnet_dispatcher_request_closes(impl);
    if (status != TURBO_OK) return status;
    status = cnet_shards_poll(impl->shards, 1u);
    if (status != TURBO_OK && status != TURBO_ETIMEDOUT && first_status == TURBO_OK)
      first_status = status;
    status = cnet_dispatcher_drive_all(impl);
    if (status != TURBO_OK && status != TURBO_ETIMEDOUT && status != TURBO_ENOBUFS &&
        status != TURBO_EBUSY && first_status == TURBO_OK)
      first_status = status;
    status = atomic_load_explicit(&impl->first_error, memory_order_acquire);
    if (status != TURBO_OK && first_status == TURBO_OK) first_status = status;
    if (cnet_dispatcher_is_idle(impl)) break;
    if (turbo_monotonic_ms() - started_ms >= timeout_ms) return TURBO_ETIMEDOUT;
  }
  turbo_mutex_lock(&impl->lock);
  impl->drained = true;
  turbo_mutex_unlock(&impl->lock);
  return first_status;
}

bool cnet_dispatcher_drained(const cnet_dispatcher *dispatcher) {
  const cnet_dispatcher_impl *impl =
      dispatcher != NULL ? (const cnet_dispatcher_impl *)dispatcher->impl : NULL;
  bool drained;
  if (impl == NULL) return false;
  turbo_mutex_lock((turbo_mutex_t *)&impl->lock);
  drained = impl->drained;
  turbo_mutex_unlock((turbo_mutex_t *)&impl->lock);
  return drained;
}

int cnet_dispatcher_destroy(cnet_dispatcher *dispatcher) {
  cnet_dispatcher_impl *impl = cnet_dispatcher_get(dispatcher);
  size_t index;
  if (dispatcher == NULL) return TURBO_EINVAL;
  if (impl == NULL) return TURBO_OK;
  if (!impl->drained || !cnet_dispatcher_is_idle(impl)) return TURBO_EBUSY;
  for (index = 0u; index < impl->shard_count; ++index) {
    if (atomic_load_explicit(&impl->lanes[index].pending, memory_order_acquire) ||
        atomic_flag_test_and_set_explicit(&impl->lanes[index].driving, memory_order_acquire)) {
      size_t release_index;
      for (release_index = 0u; release_index < index; ++release_index)
        atomic_flag_clear_explicit(&impl->lanes[release_index].driving, memory_order_release);
      return TURBO_EBUSY;
    }
  }
  turbo_mutex_destroy(&impl->lock);
  free(impl->lanes);
  free(impl->entries);
  free(impl);
  dispatcher->impl = NULL;
  return TURBO_OK;
}
