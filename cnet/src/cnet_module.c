#include "cnet_module.h"

#if defined(_WIN32)
// clang-format off
#include <winsock2.h>
// clang-format on
#endif

#include <ares.h>
#include <turbo/thread.h>

#include <stddef.h>
#include <stdint.h>

typedef struct cnet_module_state {
  turbo_mutex_t lock;
  size_t references;
  size_t resolvers;
} cnet_module_state;

static cnet_module_state cnet_module_global;
static turbo_once_t cnet_module_once = TURBO_ONCE_INIT;

static void cnet_module_once_init(void) { turbo_mutex_init(&cnet_module_global.lock); }

static void cnet_module_lock(void) {
  turbo_once(&cnet_module_once, cnet_module_once_init);
  turbo_mutex_lock(&cnet_module_global.lock);
}

int cnet_module_init(void) {
  int status;
#if defined(_WIN32)
  WSADATA winsock_data;
#endif

  cnet_module_lock();
  if (cnet_module_global.references != 0u) {
    if (cnet_module_global.references == SIZE_MAX) {
      turbo_mutex_unlock(&cnet_module_global.lock);
      return TURBO_ERANGE;
    }
    ++cnet_module_global.references;
    turbo_mutex_unlock(&cnet_module_global.lock);
    return TURBO_OK;
  }

#if defined(_WIN32)
  if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) {
    turbo_mutex_unlock(&cnet_module_global.lock);
    return TURBO_EIO;
  }
#endif
  status = ares_library_init(ARES_LIB_INIT_ALL);
  if (status != ARES_SUCCESS) {
#if defined(_WIN32)
    (void)WSACleanup();
#endif
    turbo_mutex_unlock(&cnet_module_global.lock);
    return status == ARES_ENOMEM ? TURBO_ENOMEM : TURBO_EAI_FAIL;
  }
  if (ares_threadsafety() != ARES_TRUE) {
    ares_library_cleanup();
#if defined(_WIN32)
    (void)WSACleanup();
#endif
    turbo_mutex_unlock(&cnet_module_global.lock);
    return TURBO_ENOTSUP;
  }

  cnet_module_global.references = 1u;
  turbo_mutex_unlock(&cnet_module_global.lock);
  return TURBO_OK;
}

int cnet_module_shutdown(void) {
  cnet_module_lock();
  if (cnet_module_global.references == 0u) {
    turbo_mutex_unlock(&cnet_module_global.lock);
    return TURBO_EINVAL;
  }
  if (cnet_module_global.references == 1u && cnet_module_global.resolvers != 0u) {
    turbo_mutex_unlock(&cnet_module_global.lock);
    return TURBO_EBUSY;
  }
  --cnet_module_global.references;
  if (cnet_module_global.references == 0u) {
    ares_library_cleanup();
#if defined(_WIN32)
    (void)WSACleanup();
#endif
  }
  turbo_mutex_unlock(&cnet_module_global.lock);
  return TURBO_OK;
}

int cnet_module_acquire_resolver(void) {
  cnet_module_lock();
  if (cnet_module_global.references == 0u) {
    turbo_mutex_unlock(&cnet_module_global.lock);
    return TURBO_ESHUTDOWN;
  }
  if (cnet_module_global.resolvers == SIZE_MAX) {
    turbo_mutex_unlock(&cnet_module_global.lock);
    return TURBO_ERANGE;
  }
  ++cnet_module_global.resolvers;
  turbo_mutex_unlock(&cnet_module_global.lock);
  return TURBO_OK;
}

void cnet_module_release_resolver(void) {
  cnet_module_lock();
  if (cnet_module_global.resolvers != 0u) --cnet_module_global.resolvers;
  turbo_mutex_unlock(&cnet_module_global.lock);
}
