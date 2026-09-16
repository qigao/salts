from pathlib import Path

path = Path("cnet/src/cnet_client.c")
text = path.read_text()

def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected one match, got {count}: {old!r}")
    text = text.replace(old, new, 1)

replace_once(
'''static int cnet_client_dispatch(void *context, uint32_t shard, const cnet_event *event) {
  return cnet_dispatcher_publish((cnet_dispatcher *)context, shard, event);
}
''',
'''static int cnet_client_dispatch(void *context, uint32_t shard, const cnet_event *event);
''')

observe_end = '''  }
}

static void cnet_client_cleanup_init(cnet_client_impl *impl) {
'''
direct_impl = '''  }
}

#if defined(CNET_INTERNAL_PROFILING)
static int cnet_client_direct_dispatch(cnet_client_impl *impl, uint32_t shard,
                                       const cnet_event *event) {
  cnet_client_record *record;
  cnet_shard_connection internal;
  cnet_dispatch_view view;
  cnet_session_terminal terminal = {0};
  size_t index;
  bool terminal_event;
  int status = SALTS_OK;

  if (impl == NULL || event == NULL || shard >= impl->shard_count || event->session.slot == 0u ||
      (size_t)event->session.slot > impl->capacity_per_shard)
    return SALTS_EINVAL;

  index = (size_t)shard * impl->capacity_per_shard + (size_t)event->session.slot - 1u;
  if (index >= impl->record_count) return SALTS_EPROTO;

  salts_mutex_lock(&impl->lock);
  record = &impl->records[index];
  if (!record->active || record->internal.shard != shard ||
      record->internal.session.slot != event->session.slot ||
      record->internal.session.generation != event->session.generation) {
    salts_mutex_unlock(&impl->lock);
    cnet_client_record_error(impl, SALTS_EPROTO);
    return SALTS_EPROTO;
  }
  internal = record->internal;
  salts_mutex_unlock(&impl->lock);

  view = (cnet_dispatch_view){event->kind,   event->session, event->state, event->status,
                              event->stage, event->data,    event->size,  event->argument};
  terminal_event = event->kind == CNET_EVENT_STATE && cnet_client_terminal(event->state);
  cnet_client_observe(record, &view);

  if (terminal_event) {
    status = cnet_shards_recycle(&impl->shards, internal, &terminal);
    if (status == SALTS_OK) {
      const bool valid_closed = event->state == CNET_EVENT_STATE_CLOSED &&
                                terminal.kind == CNET_SESSION_TERMINAL_CLOSED &&
                                terminal.status == SALTS_OK;
      const bool valid_failed = event->state == CNET_EVENT_STATE_FAILED &&
                                terminal.kind == CNET_SESSION_TERMINAL_FAILED &&
                                terminal.status == event->status && terminal.stage == event->stage;
      if (!valid_closed && !valid_failed) status = SALTS_EPROTO;
    }
  }
  if (status != SALTS_OK) cnet_client_record_error(impl, status);
  return status;
}
#endif

static int cnet_client_dispatch(void *context, uint32_t shard, const cnet_event *event) {
#if defined(CNET_INTERNAL_PROFILING)
  cnet_client_impl *impl = (cnet_client_impl *)context;
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->diagnostic_direct_dispatch) return cnet_client_direct_dispatch(impl, shard, event);
  return cnet_dispatcher_publish(&impl->dispatcher, shard, event);
#else
  return cnet_dispatcher_publish((cnet_dispatcher *)context, shard, event);
#endif
}

static void cnet_client_cleanup_init(cnet_client_impl *impl) {
'''
replace_once(observe_end, direct_impl)

replace_once(
'''  if (status == SALTS_OK)
    status = cnet_shards_bind_event_sink(&impl->shards, cnet_client_dispatch, &impl->dispatcher);
''',
'''  if (status == SALTS_OK) {
#if defined(CNET_INTERNAL_PROFILING)
    status = cnet_shards_bind_event_sink(&impl->shards, cnet_client_dispatch, impl);
#else
    status = cnet_shards_bind_event_sink(&impl->shards, cnet_client_dispatch, &impl->dispatcher);
#endif
  }
''')

replace_once(
'''  ++impl->active_count;
  status = cnet_dispatcher_register(&impl->dispatcher, internal, cnet_client_observe, record);
  if (status == SALTS_OK) {
''',
'''  ++impl->active_count;
#if defined(CNET_INTERNAL_PROFILING)
  status = impl->diagnostic_direct_dispatch
               ? SALTS_OK
               : cnet_dispatcher_register(&impl->dispatcher, internal, cnet_client_observe, record);
#else
  status = cnet_dispatcher_register(&impl->dispatcher, internal, cnet_client_observe, record);
#endif
  if (status == SALTS_OK) {
''')

path.write_text(text)
