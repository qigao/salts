#include "cnet_module.h"

#if defined(_WIN32)
// clang-format off
#include <winsock2.h>
// clang-format on
#endif

#include <ares.h>
#include <salts/thread.h>

#include <stddef.h>
#include <stdint.h>

typedef struct cnet_module_state {
  salts_mutex_t lock;
  size_t references;
  size_t resolvers;
} cnet_module_state;

static cnet_module_state cnet_module_global;
static salts_once_t cnet_module_once = SALTS_ONCE_INIT;

static void cnet_module_once_init(void) { salts_mutex_init(&cnet_module_global.lock); }

static void cnet_module_lock(void) {
  salts_once(&cnet_module_once, cnet_module_once_init);
  salts_mutex_lock(&cnet_module_global.lock);
}

int cnet_module_init(void) {
  int status;
#if defined(_WIN32)
  WSADATA winsock_data;
#endif

  cnet_module_lock();
  if (cnet_module_global.references != 0u) {
    if (cnet_module_global.references == SIZE_MAX) {
      salts_mutex_unlock(&cnet_module_global.lock);
      return SALTS_ERANGE;
    }
    ++cnet_module_global.references;
    salts_mutex_unlock(&cnet_module_global.lock);
    return SALTS_OK;
  }

#if defined(_WIN32)
  if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) {
    salts_mutex_unlock(&cnet_module_global.lock);
    return SALTS_EIO;
  }
#endif
  status = ares_library_init(ARES_LIB_INIT_ALL);
  if (status != ARES_SUCCESS) {
#if defined(_WIN32)
    (void)WSACleanup();
#endif
    salts_mutex_unlock(&cnet_module_global.lock);
    return status == ARES_ENOMEM ? SALTS_ENOMEM : SALTS_EAI_FAIL;
  }
  if (ares_threadsafety() != ARES_TRUE) {
    ares_library_cleanup();
#if defined(_WIN32)
    (void)WSACleanup();
#endif
    salts_mutex_unlock(&cnet_module_global.lock);
    return SALTS_ENOTSUP;
  }

  cnet_module_global.references = 1u;
  salts_mutex_unlock(&cnet_module_global.lock);
  return SALTS_OK;
}

int cnet_module_shutdown(void) {
  cnet_module_lock();
  if (cnet_module_global.references == 0u) {
    salts_mutex_unlock(&cnet_module_global.lock);
    return SALTS_EINVAL;
  }
  if (cnet_module_global.references == 1u && cnet_module_global.resolvers != 0u) {
    salts_mutex_unlock(&cnet_module_global.lock);
    return SALTS_EBUSY;
  }
  --cnet_module_global.references;
  if (cnet_module_global.references == 0u) {
    ares_library_cleanup();
#if defined(_WIN32)
    (void)WSACleanup();
#endif
  }
  salts_mutex_unlock(&cnet_module_global.lock);
  return SALTS_OK;
}

int cnet_module_acquire_resolver(void) {
  cnet_module_lock();
  if (cnet_module_global.references == 0u) {
    salts_mutex_unlock(&cnet_module_global.lock);
    return SALTS_ESHUTDOWN;
  }
  if (cnet_module_global.resolvers == SIZE_MAX) {
    salts_mutex_unlock(&cnet_module_global.lock);
    return SALTS_ERANGE;
  }
  ++cnet_module_global.resolvers;
  salts_mutex_unlock(&cnet_module_global.lock);
  return SALTS_OK;
}

void cnet_module_release_resolver(void) {
  cnet_module_lock();
  if (cnet_module_global.resolvers != 0u) --cnet_module_global.resolvers;
  salts_mutex_unlock(&cnet_module_global.lock);
}
