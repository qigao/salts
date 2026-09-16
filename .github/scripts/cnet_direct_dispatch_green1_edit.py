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
'''#if defined(CNET_INTERNAL_PROFILING)
  uint64_t profile_client_poll_ns;
  uint64_t profile_client_poll_calls;
  bool profile_active;
#endif
''',
'''#if defined(CNET_INTERNAL_PROFILING)
  uint64_t profile_client_poll_ns;
  uint64_t profile_client_poll_calls;
  bool diagnostic_direct_dispatch;
  bool profile_active;
#endif
''')

marker = '''#if defined(CNET_INTERNAL_PROFILING)
int cnet_client_profile_begin(cnet_client *client) {
'''
insert = '''#if defined(CNET_INTERNAL_PROFILING)
int cnet_client_set_diagnostic_direct_dispatch(cnet_client *client, bool enabled) {
  cnet_client_impl *impl = cnet_client_get(client);
  int status = SALTS_OK;
  if (impl == NULL) return SALTS_EINVAL;
  if (cnet_active_callback_client == impl) return SALTS_EBUSY;
  salts_mutex_lock(&impl->lock);
  if (!impl->admission_open || impl->stopped) status = SALTS_ESHUTDOWN;
  else if (impl->active_count != 0u || impl->poll_active || impl->stop_active || impl->profile_active)
    status = SALTS_EBUSY;
  else impl->diagnostic_direct_dispatch = enabled;
  salts_mutex_unlock(&impl->lock);
  return status;
}

int cnet_client_profile_begin(cnet_client *client) {
'''
replace_once(marker, insert)
path.write_text(text)
