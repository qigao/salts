from pathlib import Path

path = Path("cnet/src/cnet_client.c")
text = path.read_text()
old = '''  salts_mutex_lock(&impl->lock);\n  *out_events = impl->poll_callback_count;\n  impl->poll_active = false;\n  salts_mutex_unlock(&impl->lock);\n#if defined(CNET_INTERNAL_PROFILING)\n  if (profile_started != 0u) {\n    impl->profile_client_poll_ns += salts_hrtime() - profile_started;\n    ++impl->profile_client_poll_calls;\n  }\n#endif\n  return status;\n}\n'''
new = '''  salts_mutex_lock(&impl->lock);\n  *out_events = impl->poll_callback_count;\n#if defined(CNET_INTERNAL_PROFILING)\n  if (profile_started != 0u) {\n    impl->profile_client_poll_ns += salts_hrtime() - profile_started;\n    ++impl->profile_client_poll_calls;\n  }\n#endif\n  impl->poll_active = false;\n  salts_mutex_unlock(&impl->lock);\n  return status;\n}\n'''
if text.count(old) != 1:
    raise SystemExit(f"expected one poll completion block, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
