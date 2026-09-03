#include <cflow/fs_watch_publisher.h>

#include "fs_watch_internal.h"

#include <salts/error_codes.h>
#include <salts/thread.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct cflow_fs_watch_publisher_state {
  cflow_fs_watch watch;
  salts_mutex_t lock;
  salts_cond_t changed;
  cflow_waker waiter;
  cflow_waker terminal_waker;
  const char *name;
  const cmeta_type_desc *output_type;
  cflow_fs_watch_encode_fn encode;
  void *encode_user;
  void *current_out;
  size_t references;
  size_t wake_inflight;
  bool publisher_live;
  bool cancelled;
  bool encoded;
} cflow_fs_watch_publisher_state;

static SALTS_THREAD_LOCAL cflow_fs_watch_publisher_state *cflow_fs_watch_publisher_active_callback;

static void cflow_fs_watch_publisher_free(cflow_fs_watch_publisher_state *state) {
  if (state == NULL) return;
  salts_cond_destroy(&state->changed);
  salts_mutex_destroy(&state->lock);
  free(state);
}

static void cflow_fs_watch_publisher_retain_wake_locked(cflow_fs_watch_publisher_state *state,
                                                     cflow_waker waker) {
  if (waker.wake != NULL) ++state->wake_inflight;
}

static void cflow_fs_watch_publisher_invoke_wake(cflow_fs_watch_publisher_state *state,
                                              cflow_waker waker) {
  cflow_fs_watch_publisher_state *previous;
  if (waker.wake == NULL) return;
  previous = cflow_fs_watch_publisher_active_callback;
  cflow_fs_watch_publisher_active_callback = state;
  waker.wake(waker.user);
  cflow_fs_watch_publisher_active_callback = previous;
  salts_mutex_lock(&state->lock);
  --state->wake_inflight;
  salts_cond_broadcast(&state->changed);
  salts_mutex_unlock(&state->lock);
}

static void cflow_fs_watch_publisher_wait_wakes_locked(cflow_fs_watch_publisher_state *state) {
  while (state->wake_inflight != 0u && cflow_fs_watch_publisher_active_callback != state)
    salts_cond_wait(&state->changed, &state->lock);
}

static void cflow_fs_watch_publisher_wake(void *user) {
  cflow_fs_watch_publisher_state *state = (cflow_fs_watch_publisher_state *)user;
  cflow_waker waiter = {0};
  cflow_waker terminal = {0};
  bool done;
  if (state == NULL) return;
  salts_mutex_lock(&state->lock);
  done = cflow_fs_watch_backend_done_and_empty(&state->watch);
  waiter = state->waiter;
  state->waiter = (cflow_waker){0};
  cflow_fs_watch_publisher_retain_wake_locked(state, waiter);
  if (done) {
    terminal = state->terminal_waker;
    state->terminal_waker = (cflow_waker){0};
    cflow_fs_watch_publisher_retain_wake_locked(state, terminal);
  }
  salts_mutex_unlock(&state->lock);
  cflow_fs_watch_publisher_invoke_wake(state, waiter);
  cflow_fs_watch_publisher_invoke_wake(state, terminal);
}

static void cflow_fs_watch_publisher_event(void *user, const cflow_fs_watch_event *event) {
  cflow_fs_watch_publisher_state *state = (cflow_fs_watch_publisher_state *)user;
  if (state == NULL || state->current_out == NULL) return;
  state->encoded = state->encode(state->encode_user, event, state->current_out);
}

static bool cflow_fs_watch_publisher_arm(void *self, cflow_waker waker) {
  cflow_fs_watch_publisher_state *state = (cflow_fs_watch_publisher_state *)self;
  cflow_waker wake_now = {0};
  bool ready;
  if (state == NULL || waker.wake == NULL) return false;
  salts_mutex_lock(&state->lock);
  if (state->waiter.wake != NULL) {
    salts_mutex_unlock(&state->lock);
    return false;
  }
  state->waiter = waker;
  ready = state->cancelled || cflow_fs_watch_has_ready_or_done(&state->watch);
  if (ready) {
    wake_now = state->waiter;
    state->waiter = (cflow_waker){0};
    cflow_fs_watch_publisher_retain_wake_locked(state, wake_now);
  }
  salts_mutex_unlock(&state->lock);
  cflow_fs_watch_publisher_invoke_wake(state, wake_now);
  return true;
}

static void cflow_fs_watch_publisher_unarm(void *self) {
  cflow_fs_watch_publisher_state *state = (cflow_fs_watch_publisher_state *)self;
  if (state == NULL) return;
  salts_mutex_lock(&state->lock);
  state->waiter = (cflow_waker){0};
  cflow_fs_watch_publisher_wait_wakes_locked(state);
  salts_mutex_unlock(&state->lock);
}

CMETA_IMPLEMENTS(cflow_waitable, cflow_fs_watch_publisher_waitable, 0,
                 .arm = cflow_fs_watch_publisher_arm, .cancel = cflow_fs_watch_publisher_unarm);

static cflow_step cflow_fs_watch_publisher_resume(void *self, cflow_publish_context *ctx, void *out_value) {
  cflow_fs_watch_publisher_state *state = (cflow_fs_watch_publisher_state *)self;
  size_t delivered = 0u;
  int status;
  (void)ctx;
  if (state == NULL || out_value == NULL)
    return (cflow_step){CFLOW_STEP_ERROR, {0}, "filesystem watch Publisher unavailable"};
  salts_mutex_lock(&state->lock);
  if (state->cancelled) {
    salts_mutex_unlock(&state->lock);
    return (cflow_step){CFLOW_STEP_DONE, {0}, NULL};
  }
  state->current_out = out_value;
  state->encoded = false;
  memset(out_value, 0, state->output_type->size);
  salts_mutex_unlock(&state->lock);

  status = cflow_fs_watch_run_ready(&state->watch, 1u, &delivered);

  salts_mutex_lock(&state->lock);
  state->current_out = NULL;
  if (status != SALTS_OK) {
    salts_mutex_unlock(&state->lock);
    return (cflow_step){CFLOW_STEP_ERROR, {0}, "filesystem watch driver failed"};
  }
  if (delivered != 0u) {
    const bool encoded = state->encoded;
    const bool done = cflow_fs_watch_backend_done_and_empty(&state->watch);
    salts_mutex_unlock(&state->lock);
    if (!encoded) return (cflow_step){CFLOW_STEP_ERROR, {0}, "filesystem watch encoder failed"};
    return (cflow_step){done ? CFLOW_STEP_VALUE_AND_DONE : CFLOW_STEP_VALUE, {0}, NULL};
  }
  salts_mutex_unlock(&state->lock);

  if (cflow_fs_watch_backend_done_and_empty(&state->watch))
    return (cflow_step){CFLOW_STEP_DONE, {0}, NULL};
  return (cflow_step){CFLOW_STEP_WAIT, cflow_fs_watch_publisher_waitable_as_cflow_waitable(state),
                      NULL};
}

static void cflow_fs_watch_publisher_cancel(void *self) {
  cflow_fs_watch_publisher_state *state = (cflow_fs_watch_publisher_state *)self;
  cflow_waker waiter = {0};
  cflow_waker terminal = {0};
  if (state == NULL) return;
  salts_mutex_lock(&state->lock);
  if (!state->cancelled) {
    state->cancelled = true;
    waiter = state->waiter;
    terminal = state->terminal_waker;
    state->waiter = (cflow_waker){0};
    state->terminal_waker = (cflow_waker){0};
    cflow_fs_watch_publisher_retain_wake_locked(state, waiter);
    cflow_fs_watch_publisher_retain_wake_locked(state, terminal);
  }
  salts_mutex_unlock(&state->lock);
  (void)cflow_fs_watch_close(&state->watch);
  cflow_fs_watch_publisher_invoke_wake(state, waiter);
  cflow_fs_watch_publisher_invoke_wake(state, terminal);
  salts_mutex_lock(&state->lock);
  cflow_fs_watch_publisher_wait_wakes_locked(state);
  salts_mutex_unlock(&state->lock);
}

static void cflow_fs_watch_publisher_destroy(void *self) {
  cflow_fs_watch_publisher_state *state = (cflow_fs_watch_publisher_state *)self;
  bool free_state = false;
  if (state == NULL) return;
  cflow_fs_watch_publisher_cancel(state);
  salts_mutex_lock(&state->lock);
  if (state->publisher_live) {
    state->publisher_live = false;
    --state->references;
    free_state = state->references == 0u;
  }
  salts_mutex_unlock(&state->lock);
  if (free_state) cflow_fs_watch_publisher_free(state);
}

static const char *cflow_fs_watch_publisher_name(void *self) {
  cflow_fs_watch_publisher_state *state = (cflow_fs_watch_publisher_state *)self;
  return state != NULL && state->name != NULL ? state->name : "filesystem-watch";
}

static const cmeta_type_desc *cflow_fs_watch_publisher_type(void *self) {
  cflow_fs_watch_publisher_state *state = (cflow_fs_watch_publisher_state *)self;
  return state != NULL ? state->output_type : NULL;
}

static void cflow_fs_watch_publisher_bind_terminal(void *self, cflow_waker waker) {
  cflow_fs_watch_publisher_state *state = (cflow_fs_watch_publisher_state *)self;
  bool wake_now;
  if (state == NULL) return;
  salts_mutex_lock(&state->lock);
  wake_now = state->cancelled || cflow_fs_watch_backend_done_and_empty(&state->watch);
  if (!wake_now) state->terminal_waker = waker;
  else cflow_fs_watch_publisher_retain_wake_locked(state, waker);
  salts_mutex_unlock(&state->lock);
  if (wake_now) cflow_fs_watch_publisher_invoke_wake(state, waker);
}

static cflow_publisher_terminal cflow_fs_watch_publisher_poll_terminal(void *self, const char **error) {
  cflow_fs_watch_publisher_state *state = (cflow_fs_watch_publisher_state *)self;
  bool cancelled;
  (void)error;
  if (state == NULL) return CFLOW_PUBLISHER_ERROR;
  salts_mutex_lock(&state->lock);
  cancelled = state->cancelled || cflow_fs_watch_backend_done_and_empty(&state->watch);
  salts_mutex_unlock(&state->lock);
  return cancelled ? CFLOW_PUBLISHER_DONE : CFLOW_PUBLISHER_OPEN;
}

CMETA_IMPLEMENTS(cflow_publisher, cflow_fs_watch_publisher, 0, .name = cflow_fs_watch_publisher_name,
                 .output_type = cflow_fs_watch_publisher_type, .resume = cflow_fs_watch_publisher_resume,
                 .cancel = cflow_fs_watch_publisher_cancel, .destroy = cflow_fs_watch_publisher_destroy,
                 .bind_terminal_waker = cflow_fs_watch_publisher_bind_terminal,
                 .poll_terminal = cflow_fs_watch_publisher_poll_terminal);

int cflow_fs_watch_publisher_open(cflow_publisher *out, cflow_fs_watch_publisher_owner *owner,
                               const char *path, const cflow_fs_watch_publisher_config *config) {
  const cmeta_trait_flags required = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY;
  cflow_fs_watch_publisher_state *state;
  cflow_fs_watch_config watch_config;
  int status;
  if (out == NULL || owner == NULL || cflow_publisher_valid(out) || owner->impl != NULL ||
      path == NULL || path[0] == '\0' || config == NULL || config->event_capacity == 0u ||
      config->watch_capacity == 0u || config->path_capacity < 2u ||
      config->native_buffer_capacity < 1024u || config->native_buffer_capacity > 65536u ||
      config->encode == NULL || !cmeta_type_desc_valid(config->output_type))
    return SALTS_EINVAL;
  if (cmeta_type_require_traits(config->output_type, required) != CMETA_OK) return SALTS_ENOTSUP;

  state = (cflow_fs_watch_publisher_state *)calloc(1u, sizeof(*state));
  if (state == NULL) return SALTS_ENOMEM;
  salts_mutex_init(&state->lock);
  salts_cond_init(&state->changed);
  if (state->lock == NULL || state->changed == NULL) {
    salts_cond_destroy(&state->changed);
    salts_mutex_destroy(&state->lock);
    free(state);
    return SALTS_ENOMEM;
  }
  state->name = config->name;
  state->output_type = config->output_type;
  state->encode = config->encode;
  state->encode_user = config->encode_user;
  watch_config = (cflow_fs_watch_config){
      .recursive = config->recursive,
      .event_capacity = config->event_capacity,
      .watch_capacity = config->watch_capacity,
      .path_capacity = config->path_capacity,
      .native_buffer_capacity = config->native_buffer_capacity,
      .event = cflow_fs_watch_publisher_event,
      .event_user = state,
  };
  status = cflow_fs_watch_open_notified(&state->watch, path, &watch_config,
                                        cflow_fs_watch_publisher_wake, state);
  if (status != SALTS_OK) {
    cflow_fs_watch_publisher_free(state);
    return status;
  }
  state->references = 2u;
  state->publisher_live = true;
  *out = cflow_fs_watch_publisher_as_cflow_publisher(state);
  owner->impl = state;
  return SALTS_OK;
}

int cflow_fs_watch_publisher_owner_acknowledge_rescan(cflow_fs_watch_publisher_owner *owner) {
  cflow_fs_watch_publisher_state *state =
      owner != NULL ? (cflow_fs_watch_publisher_state *)owner->impl : NULL;
  if (state == NULL) return SALTS_EINVAL;
  return cflow_fs_watch_acknowledge_rescan(&state->watch);
}

bool cflow_fs_watch_publisher_owner_get_stats(const cflow_fs_watch_publisher_owner *owner,
                                           cflow_fs_watch_stats *out) {
  cflow_fs_watch_publisher_state *state =
      owner != NULL ? (cflow_fs_watch_publisher_state *)owner->impl : NULL;
  return state != NULL && cflow_fs_watch_get_stats(&state->watch, out);
}

int cflow_fs_watch_publisher_owner_close(cflow_fs_watch_publisher_owner *owner) {
  cflow_fs_watch_publisher_state *state;
  size_t delivered = 0u;
  bool free_state;
  int status;
  if (owner == NULL) return SALTS_EINVAL;
  if (owner->impl == NULL) return SALTS_OK;
  state = (cflow_fs_watch_publisher_state *)owner->impl;
  salts_mutex_lock(&state->lock);
  if (state->publisher_live || state->wake_inflight != 0u) {
    salts_mutex_unlock(&state->lock);
    return SALTS_EBUSY;
  }
  salts_mutex_unlock(&state->lock);

  status = cflow_fs_watch_close(&state->watch);
  if (status != SALTS_OK && status != SALTS_EALREADY) return status;
  status = cflow_fs_watch_run_ready(&state->watch, SIZE_MAX, &delivered);
  if (status != SALTS_OK) return status;
  if (!cflow_fs_watch_is_quiescent(&state->watch)) return SALTS_EBUSY;
  status = cflow_fs_watch_destroy(&state->watch);
  if (state->watch.impl != NULL) return status;

  salts_mutex_lock(&state->lock);
  --state->references;
  free_state = state->references == 0u;
  owner->impl = NULL;
  salts_mutex_unlock(&state->lock);
  if (free_state) cflow_fs_watch_publisher_free(state);
  return status;
}
