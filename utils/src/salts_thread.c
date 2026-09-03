#include "salts_thread.h"

static int g_single_threaded = 0;

void salts_sync_set_single_threaded(int enabled) { g_single_threaded = enabled; }

int salts_sync_is_single_threaded(void) { return g_single_threaded; }

int salts_getpid(void) {
#ifdef _WIN32
  return (int)GetCurrentProcessId();
#else
  return (int)getpid();
#endif
}
