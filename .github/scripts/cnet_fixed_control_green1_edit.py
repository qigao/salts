from pathlib import Path


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:120]!r}")
    path.write_text(text.replace(old, new, 1))

# 1) Composed private client profile contract.
path = Path("cnet/src/cnet_client_internal.h")
replace_once(path,
'''#if defined(CNET_INTERNAL_PROFILING)
typedef cnet_owner_profile cnet_client_poll_profile;

/** Samples internal poll-owner stages in a private diagnostic build. */
''',
'''#if defined(CNET_INTERNAL_PROFILING)
typedef struct cnet_client_poll_profile {
  cnet_owner_profile owner;
  uint64_t client_poll_ns;
  uint64_t client_poll_calls;
  uint64_t dispatcher_prepare_ns;
  uint64_t dispatcher_prepare_calls;
  uint64_t dispatcher_invoke_ns;
  uint64_t dispatcher_invoke_calls;
  uint64_t dispatcher_observer_ns;
  uint64_t dispatcher_observer_calls;
  uint64_t dispatcher_release_ns;
  uint64_t dispatcher_release_calls;
} cnet_client_poll_profile;

/** Samples internal poll-owner/client/dispatcher stages in a private diagnostic build. */
''')

# 2) Dispatcher diagnostic profile API.
path = Path("cnet/src/cnet_dispatcher.h")
replace_once(path,
'''bool cnet_dispatcher_drained(const cnet_dispatcher *dispatcher);

/** Requires a completed drain and no retained event lease. */
''',
'''bool cnet_dispatcher_drained(const cnet_dispatcher *dispatcher);

#if defined(CNET_INTERNAL_PROFILING)
typedef struct cnet_dispatcher_profile {
  uint64_t prepare_ns;
  uint64_t prepare_calls;
  uint64_t invoke_ns;
  uint64_t invoke_calls;
  uint64_t observer_ns;
  uint64_t observer_calls;
  uint64_t release_ns;
  uint64_t release_calls;
} cnet_dispatcher_profile;

int cnet_dispatcher_profile_begin(cnet_dispatcher *dispatcher);
int cnet_dispatcher_profile_take(cnet_dispatcher *dispatcher,
                                 cnet_dispatcher_profile *out_profile);
#endif

/** Requires a completed drain and no retained event lease. */
''')

# 3) Dispatcher clocks, diagnostic-only.
path = Path("cnet/src/cnet_dispatcher.c")
replace_once(path,
'''  atomic_int first_error;
  bool admission_open;
  bool drained;
};
''',
'''  atomic_int first_error;
  bool admission_open;
  bool drained;
#if defined(CNET_INTERNAL_PROFILING)
  cnet_dispatcher_profile profile;
  bool profile_active;
#endif
};
''')
replace_once(path,
'''static cnet_dispatcher_impl *cnet_dispatcher_get(cnet_dispatcher *dispatcher) {
  return dispatcher != NULL ? (cnet_dispatcher_impl *)dispatcher->impl : NULL;
}

''',
'''static cnet_dispatcher_impl *cnet_dispatcher_get(cnet_dispatcher *dispatcher) {
  return dispatcher != NULL ? (cnet_dispatcher_impl *)dispatcher->impl : NULL;
}

#if defined(CNET_INTERNAL_PROFILING)
static uint64_t cnet_dispatcher_profile_start(const cnet_dispatcher_impl *impl) {
  return impl->profile_active ? salts_hrtime() : 0u;
}

static void cnet_dispatcher_profile_finish(cnet_dispatcher_impl *impl, uint64_t started,
                                           uint64_t *total_ns, uint64_t *calls) {
  if (started == 0u) return;
  *total_ns += salts_hrtime() - started;
  ++*calls;
}
#endif

''')
old_prepare = '''static int cnet_dispatcher_prepare(cnet_dispatcher_impl *impl, uint32_t shard,
                                   const cnet_event *event, cnet_dispatch_release_fn release,
                                   uint64_t release_token, cnet_dispatch_job *out_job) {
  const cnet_shard_connection connection = {shard, event->session};
  cnet_dispatch_entry *entry;

  salts_mutex_lock(&impl->lock);
  entry = cnet_dispatcher_entry(impl, connection);
  if (entry == NULL || !entry->active ||
      entry->connection.session.generation != connection.session.generation) {
    salts_mutex_unlock(&impl->lock);
    return SALTS_EBUSY;
  }
  *out_job = (cnet_dispatch_job){.invoke = entry->observer,
                                 .context = entry->observer_context,
                                 .event = {event->kind, event->session, event->state, event->status,
                                           event->stage, event->data, event->size, event->argument},
                                 .release = release,
                                 .release_context = entry,
                                 .release_token = release_token};
  salts_mutex_unlock(&impl->lock);
  return SALTS_OK;
}

static int cnet_dispatcher_invoke(const cnet_dispatch_job *job) {
  const cnet_dispatch_view view = {job->event.kind,   job->event.session, job->event.state,
                                   job->event.status, job->event.stage,   job->event.data,
                                   job->event.size,   job->event.argument};
  int status = SALTS_OK;

  job->invoke(job->context, &view);
  if (job->release != NULL) status = job->release(job->release_context, &view, job->release_token);
  return status;
}
'''
new_prepare = '''static int cnet_dispatcher_prepare(cnet_dispatcher_impl *impl, uint32_t shard,
                                   const cnet_event *event, cnet_dispatch_release_fn release,
                                   uint64_t release_token, cnet_dispatch_job *out_job) {
  const cnet_shard_connection connection = {shard, event->session};
  cnet_dispatch_entry *entry;
  int status = SALTS_OK;
#if defined(CNET_INTERNAL_PROFILING)
  const uint64_t profile_started = cnet_dispatcher_profile_start(impl);
#endif

  salts_mutex_lock(&impl->lock);
  entry = cnet_dispatcher_entry(impl, connection);
  if (entry == NULL || !entry->active ||
      entry->connection.session.generation != connection.session.generation) {
    status = SALTS_EBUSY;
  } else {
    *out_job = (cnet_dispatch_job){.invoke = entry->observer,
                                   .context = entry->observer_context,
                                   .event = {event->kind, event->session, event->state, event->status,
                                             event->stage, event->data, event->size, event->argument},
                                   .release = release,
                                   .release_context = entry,
                                   .release_token = release_token};
  }
  salts_mutex_unlock(&impl->lock);
#if defined(CNET_INTERNAL_PROFILING)
  cnet_dispatcher_profile_finish(impl, profile_started, &impl->profile.prepare_ns,
                                 &impl->profile.prepare_calls);
#endif
  return status;
}

static int cnet_dispatcher_invoke(cnet_dispatcher_impl *impl, const cnet_dispatch_job *job) {
  const cnet_dispatch_view view = {job->event.kind,   job->event.session, job->event.state,
                                   job->event.status, job->event.stage,   job->event.data,
                                   job->event.size,   job->event.argument};
  int status = SALTS_OK;
#if defined(CNET_INTERNAL_PROFILING)
  const uint64_t invoke_started = cnet_dispatcher_profile_start(impl);
  const uint64_t observer_started = cnet_dispatcher_profile_start(impl);
#endif

  job->invoke(job->context, &view);
#if defined(CNET_INTERNAL_PROFILING)
  cnet_dispatcher_profile_finish(impl, observer_started, &impl->profile.observer_ns,
                                 &impl->profile.observer_calls);
#endif
  if (job->release != NULL) {
#if defined(CNET_INTERNAL_PROFILING)
    const uint64_t release_started = cnet_dispatcher_profile_start(impl);
#endif
    status = job->release(job->release_context, &view, job->release_token);
#if defined(CNET_INTERNAL_PROFILING)
    cnet_dispatcher_profile_finish(impl, release_started, &impl->profile.release_ns,
                                   &impl->profile.release_calls);
#endif
  }
#if defined(CNET_INTERNAL_PROFILING)
  cnet_dispatcher_profile_finish(impl, invoke_started, &impl->profile.invoke_ns,
                                 &impl->profile.invoke_calls);
#endif
  return status;
}
'''
replace_once(path, old_prepare, new_prepare)
text = path.read_text()
count = text.count('cnet_dispatcher_invoke(&job)')
if count != 2:
    raise SystemExit(f"dispatcher invoke callers: expected 2, found {count}")
path.write_text(text.replace('cnet_dispatcher_invoke(&job)', 'cnet_dispatcher_invoke(impl, &job)'))
replace_once(path,
'''int cnet_dispatcher_destroy(cnet_dispatcher *dispatcher) {
''',
'''#if defined(CNET_INTERNAL_PROFILING)
int cnet_dispatcher_profile_begin(cnet_dispatcher *dispatcher) {
  cnet_dispatcher_impl *impl = cnet_dispatcher_get(dispatcher);
  int status = SALTS_OK;
  if (impl == NULL) return SALTS_EINVAL;
  salts_mutex_lock(&impl->lock);
  if (impl->profile_active) status = SALTS_EBUSY;
  else {
    memset(&impl->profile, 0, sizeof(impl->profile));
    impl->profile_active = true;
  }
  salts_mutex_unlock(&impl->lock);
  return status;
}

int cnet_dispatcher_profile_take(cnet_dispatcher *dispatcher,
                                 cnet_dispatcher_profile *out_profile) {
  cnet_dispatcher_impl *impl = cnet_dispatcher_get(dispatcher);
  int status = SALTS_OK;
  if (impl == NULL || out_profile == NULL) return SALTS_EINVAL;
  salts_mutex_lock(&impl->lock);
  if (!impl->profile_active) status = SALTS_EBUSY;
  else {
    *out_profile = impl->profile;
    impl->profile_active = false;
  }
  salts_mutex_unlock(&impl->lock);
  return status;
}
#endif

int cnet_dispatcher_destroy(cnet_dispatcher *dispatcher) {
''')

# 4) Client owns the composed sampling window and measures accepted poll calls.
path = Path("cnet/src/cnet_client.c")
replace_once(path,
'''  bool stop_active;
  bool stopped;
};
''',
'''  bool stop_active;
  bool stopped;
#if defined(CNET_INTERNAL_PROFILING)
  uint64_t profile_client_poll_ns;
  uint64_t profile_client_poll_calls;
  bool profile_active;
#endif
};
''')
replace_once(path,
'''  uint32_t remaining_ms = timeout_ms;
  int status;
''',
'''  uint32_t remaining_ms = timeout_ms;
  int status;
#if defined(CNET_INTERNAL_PROFILING)
  uint64_t profile_started = 0u;
#endif
''')
replace_once(path,
'''  salts_mutex_unlock(&impl->lock);
  if (status != SALTS_OK) return status;

  for (;;) {
''',
'''  salts_mutex_unlock(&impl->lock);
  if (status != SALTS_OK) return status;
#if defined(CNET_INTERNAL_PROFILING)
  if (impl->profile_active) profile_started = salts_hrtime();
#endif

  for (;;) {
''')
replace_once(path,
'''  *out_events = impl->poll_callback_count;
  impl->poll_active = false;
  salts_mutex_unlock(&impl->lock);
  return status;
}

#if defined(CNET_INTERNAL_PROFILING)
int cnet_client_profile_begin(cnet_client *client) {
  cnet_client_impl *impl = cnet_client_get(client);
  int status;
  if (impl == NULL) return SALTS_EINVAL;
  if (cnet_active_callback_client == impl) return SALTS_EBUSY;
  salts_mutex_lock(&impl->lock);
  if (!impl->admission_open || impl->stopped) status = SALTS_ESHUTDOWN;
  else if (impl->poll_active || impl->stop_active) status = SALTS_EBUSY;
  else status = cnet_shards_profile_begin(&impl->shards);
  salts_mutex_unlock(&impl->lock);
  return status;
}

int cnet_client_profile_take(cnet_client *client, cnet_client_poll_profile *out_profile) {
  cnet_client_impl *impl = cnet_client_get(client);
  int status;
  if (impl == NULL || out_profile == NULL) return SALTS_EINVAL;
  if (cnet_active_callback_client == impl) return SALTS_EBUSY;
  salts_mutex_lock(&impl->lock);
  if (impl->poll_active || impl->stop_active) status = SALTS_EBUSY;
  else status = cnet_shards_profile_take(&impl->shards, out_profile);
  salts_mutex_unlock(&impl->lock);
  return status;
}
#endif
''',
'''  *out_events = impl->poll_callback_count;
  impl->poll_active = false;
  salts_mutex_unlock(&impl->lock);
#if defined(CNET_INTERNAL_PROFILING)
  if (profile_started != 0u) {
    impl->profile_client_poll_ns += salts_hrtime() - profile_started;
    ++impl->profile_client_poll_calls;
  }
#endif
  return status;
}

#if defined(CNET_INTERNAL_PROFILING)
int cnet_client_profile_begin(cnet_client *client) {
  cnet_client_impl *impl = cnet_client_get(client);
  cnet_owner_profile ignored_owner = {0};
  bool shards_started = false;
  int status;
  if (impl == NULL) return SALTS_EINVAL;
  if (cnet_active_callback_client == impl) return SALTS_EBUSY;
  salts_mutex_lock(&impl->lock);
  if (!impl->admission_open || impl->stopped) status = SALTS_ESHUTDOWN;
  else if (impl->poll_active || impl->stop_active || impl->profile_active) status = SALTS_EBUSY;
  else {
    status = cnet_shards_profile_begin(&impl->shards);
    if (status == SALTS_OK) {
      shards_started = true;
      status = cnet_dispatcher_profile_begin(&impl->dispatcher);
    }
    if (status == SALTS_OK) {
      impl->profile_client_poll_ns = 0u;
      impl->profile_client_poll_calls = 0u;
      impl->profile_active = true;
    } else if (shards_started) {
      (void)cnet_shards_profile_take(&impl->shards, &ignored_owner);
    }
  }
  salts_mutex_unlock(&impl->lock);
  return status;
}

int cnet_client_profile_take(cnet_client *client, cnet_client_poll_profile *out_profile) {
  cnet_client_impl *impl = cnet_client_get(client);
  cnet_dispatcher_profile dispatcher_profile = {0};
  int status;
  if (impl == NULL || out_profile == NULL) return SALTS_EINVAL;
  if (cnet_active_callback_client == impl) return SALTS_EBUSY;
  salts_mutex_lock(&impl->lock);
  if (impl->poll_active || impl->stop_active || !impl->profile_active) status = SALTS_EBUSY;
  else {
    memset(out_profile, 0, sizeof(*out_profile));
    status = cnet_shards_profile_take(&impl->shards, &out_profile->owner);
    if (status == SALTS_OK)
      status = cnet_dispatcher_profile_take(&impl->dispatcher, &dispatcher_profile);
    if (status == SALTS_OK) {
      out_profile->client_poll_ns = impl->profile_client_poll_ns;
      out_profile->client_poll_calls = impl->profile_client_poll_calls;
      out_profile->dispatcher_prepare_ns = dispatcher_profile.prepare_ns;
      out_profile->dispatcher_prepare_calls = dispatcher_profile.prepare_calls;
      out_profile->dispatcher_invoke_ns = dispatcher_profile.invoke_ns;
      out_profile->dispatcher_invoke_calls = dispatcher_profile.invoke_calls;
      out_profile->dispatcher_observer_ns = dispatcher_profile.observer_ns;
      out_profile->dispatcher_observer_calls = dispatcher_profile.observer_calls;
      out_profile->dispatcher_release_ns = dispatcher_profile.release_ns;
      out_profile->dispatcher_release_calls = dispatcher_profile.release_calls;
      impl->profile_active = false;
    }
  }
  salts_mutex_unlock(&impl->lock);
  return status;
}
#endif
''')

# 5) Existing internal profile test now reads the owner subprofile and verifies poll sampling.
path = Path("cnet/tests/cnet_api_test.c")
text = path.read_text()
for field in [
    "owner_drive_calls", "receive_rearm_stage_calls", "receive_rearm_request_lifecycle_calls",
    "command_stage_calls", "command_request_lifecycle_calls", "request_lifecycle_calls",
    "request_start_calls", "observe_calls", "request_completion_calls", "event_publish_calls",
    "owner_drive_ns", "receive_rearm_stage_ns", "receive_rearm_request_lifecycle_ns",
    "command_request_lifecycle_ns", "command_stage_ns", "request_lifecycle_ns",
    "request_start_ns", "observe_ns"
]:
    text = text.replace(f"profile.{field}", f"profile.owner.{field}")
needle = '''    check_equal(profile.owner.event_publish_calls, (uint64_t)0u);\n'''
if text.count(needle) != 1:
    raise SystemExit("profile test anchor missing")
text = text.replace(needle, needle + '''    check_equal(profile.client_poll_calls, (uint64_t)1u);\n    check_true(profile.client_poll_ns >= profile.owner.owner_drive_ns);\n    check_equal(profile.dispatcher_prepare_calls, (uint64_t)0u);\n    check_equal(profile.dispatcher_invoke_calls, (uint64_t)0u);\n    check_equal(profile.dispatcher_observer_calls, (uint64_t)0u);\n    check_equal(profile.dispatcher_release_calls, (uint64_t)0u);\n''', 1)
path.write_text(text)

# 6) Existing benchmark owner-profile accesses move under the composed owner subprofile.
path = Path("cnet/benchmarks/cnet_io_benchmark.c")
text = path.read_text()
text = text.replace("cnet_profile.", "cnet_profile.owner.")
path.write_text(text)
