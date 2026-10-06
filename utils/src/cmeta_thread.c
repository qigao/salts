#include "cmeta_thread.h"

static int g_single_threaded = 0;

void cmeta_sync_set_single_threaded(int enabled) { g_single_threaded = enabled; }

int cmeta_sync_is_single_threaded(void) { return g_single_threaded; }

int cmeta_getpid(void) {
#ifdef _WIN32
  return (int)GetCurrentProcessId();
#else
  return (int)getpid();
#endif
}
