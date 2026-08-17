#include "turbo_error.h"
#include "turbo_thread.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define TURBO_ERROR_MAX_CUSTOM_DOMAINS 64

#define TURBO_ERROR_TABLE_ITEM(code, value, name, message) {code, name, message},

static const turbo_error_entry_t turbo_error_table[] = {
  TURBO_ERROR_CODE_ITEMS(TURBO_ERROR_TABLE_ITEM)
};
#undef TURBO_ERROR_TABLE_ITEM

static turbo_once_t turbo_error_registry_once = TURBO_ONCE_INIT;
static turbo_mutex_t turbo_error_registry_mutex;
static const turbo_error_domain_desc_t *turbo_error_custom_domains[TURBO_ERROR_MAX_CUSTOM_DOMAINS];
static size_t turbo_error_custom_domain_count;

static void turbo_error_registry_init(void) {
  turbo_mutex_init(&turbo_error_registry_mutex);
}

static const turbo_error_entry_t *turbo_find_error(int err) {
  for (size_t i = 0; i < sizeof(turbo_error_table) / sizeof(turbo_error_table[0]); ++i) {
    if (turbo_error_table[i].code == err) return &turbo_error_table[i];
  }
  return NULL;
}

static int turbo_custom_domain_valid(const turbo_error_domain_desc_t *domain) {
  if (!domain || !domain->domain_name || !domain->entries || domain->count == 0) return 0;
  if (domain->domain < TURBO_ERROR_CUSTOM_DOMAIN_MIN ||
      domain->domain > TURBO_ERROR_CUSTOM_DOMAIN_MAX)
    return 0;
  for (size_t i = 0; i < domain->count; ++i) {
    const turbo_error_entry_t *entry = &domain->entries[i];
    if (!entry->name || !entry->message) return 0;
    if (entry->code >= 0) return 0;
    if (TURBO_ERROR_CUSTOM_DOMAIN(entry->code) != domain->domain) return 0;
    if (TURBO_ERROR_CUSTOM_LOCAL(entry->code) == 0) return 0;
  }
  return 1;
}

static const turbo_error_entry_t *turbo_find_custom_error_locked(
    int err, const turbo_error_domain_desc_t **out_domain) {
  int domain_id;

  if (err >= 0) return NULL;
  domain_id = TURBO_ERROR_CUSTOM_DOMAIN(err);
  if (domain_id < TURBO_ERROR_CUSTOM_DOMAIN_MIN ||
      domain_id > TURBO_ERROR_CUSTOM_DOMAIN_MAX) {
    return NULL;
  }

  for (size_t i = 0; i < turbo_error_custom_domain_count; ++i) {
    const turbo_error_domain_desc_t *domain = turbo_error_custom_domains[i];
    if (!domain || domain->domain != domain_id) continue;
    for (size_t j = 0; j < domain->count; ++j) {
      if (domain->entries[j].code == err) {
        if (out_domain) *out_domain = domain;
        return &domain->entries[j];
      }
    }
  }
  return NULL;
}

int turbo_error_register_domain(const turbo_error_domain_desc_t *domain) {
  turbo_once(&turbo_error_registry_once, turbo_error_registry_init);
  if (!turbo_custom_domain_valid(domain)) return TURBO_EINVAL;

  turbo_mutex_lock(&turbo_error_registry_mutex);
  for (size_t i = 0; i < turbo_error_custom_domain_count; ++i) {
    if (turbo_error_custom_domains[i] &&
        turbo_error_custom_domains[i]->domain == domain->domain) {
      turbo_mutex_unlock(&turbo_error_registry_mutex);
      return TURBO_EALREADY;
    }
  }
  if (turbo_error_custom_domain_count >= TURBO_ERROR_MAX_CUSTOM_DOMAINS) {
    turbo_mutex_unlock(&turbo_error_registry_mutex);
    return TURBO_ENOSPC;
  }
  turbo_error_custom_domains[turbo_error_custom_domain_count++] = domain;
  turbo_mutex_unlock(&turbo_error_registry_mutex);
  return TURBO_OK;
}

int turbo_error_unregister_domain(int domain) {
  turbo_once(&turbo_error_registry_once, turbo_error_registry_init);
  if (domain < TURBO_ERROR_CUSTOM_DOMAIN_MIN || domain > TURBO_ERROR_CUSTOM_DOMAIN_MAX) {
    return TURBO_EINVAL;
  }

  turbo_mutex_lock(&turbo_error_registry_mutex);
  for (size_t i = 0; i < turbo_error_custom_domain_count; ++i) {
    if (turbo_error_custom_domains[i] && turbo_error_custom_domains[i]->domain == domain) {
      turbo_error_custom_domains[i] =
          turbo_error_custom_domains[turbo_error_custom_domain_count - 1];
      turbo_error_custom_domains[turbo_error_custom_domain_count - 1] = NULL;
      --turbo_error_custom_domain_count;
      turbo_mutex_unlock(&turbo_error_registry_mutex);
      return TURBO_OK;
    }
  }
  turbo_mutex_unlock(&turbo_error_registry_mutex);
  return TURBO_ENOENT;
}

static const char *turbo_unknown_message(int err) {
  static TURBO_THREAD_LOCAL char buf[64];
  snprintf(buf, sizeof(buf), "unknown error %d", err);
  return buf;
}

static const char *turbo_posix_strerror(int posix_code) {
  const char *msg;

  if (posix_code <= 0) return NULL;
  msg = strerror(posix_code);
  if (!msg || msg[0] == '\0') return NULL;
  if (strstr(msg, "Unknown error") != NULL || strstr(msg, "unknown error") != NULL) return NULL;
  return msg;
}

#ifdef _WIN32
static const char *turbo_win32_strerror(int win32_code) {
  static TURBO_THREAD_LOCAL char buf[256];
  DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
  DWORD written;

  if (win32_code <= 0) return NULL;
  written = FormatMessageA(flags, NULL, (DWORD)win32_code, 0, buf, (DWORD)sizeof(buf), NULL);
  if (written == 0) return NULL;
  while (written > 0 && (buf[written - 1] == '\n' || buf[written - 1] == '\r' ||
                         buf[written - 1] == ' ' || buf[written - 1] == '\t')) {
    buf[--written] = '\0';
  }
  return buf;
}
#endif

turbo_error_info_t turbo_error_info(int err) {
  turbo_error_info_t info;
  const turbo_error_entry_t *entry;
  const turbo_error_domain_desc_t *custom_domain = NULL;
  int native_code;
  const char *native_msg;

  info.code = err;
  info.custom_domain = 0;
  info.domain = TURBO_ERROR_DOMAIN_UNKNOWN;
  info.domain_name = "unknown";
  info.name = "UNKNOWN";
  info.message = NULL;

  if (err == TURBO_OK) {
    info.domain = TURBO_ERROR_DOMAIN_NONE;
    info.domain_name = "none";
    info.name = "TURBO_OK";
    info.message = "success";
    return info;
  }

  entry = turbo_find_error(err);
  if (entry) {
    info.domain = TURBO_ERROR_DOMAIN_TURBO;
    info.domain_name = "turbo";
    info.name = entry->name;
    info.message = entry->message;
    return info;
  }

  turbo_once(&turbo_error_registry_once, turbo_error_registry_init);
  turbo_mutex_lock(&turbo_error_registry_mutex);
  entry = turbo_find_custom_error_locked(err, &custom_domain);
  if (entry) {
    info.custom_domain = custom_domain->domain;
    info.domain = TURBO_ERROR_DOMAIN_CUSTOM;
    info.domain_name = custom_domain->domain_name;
    info.name = entry->name;
    info.message = entry->message;
    turbo_mutex_unlock(&turbo_error_registry_mutex);
    return info;
  }
  turbo_mutex_unlock(&turbo_error_registry_mutex);

  if (err < 0) {
    int domain_id = TURBO_ERROR_CUSTOM_DOMAIN(err);
    if (domain_id >= TURBO_ERROR_CUSTOM_DOMAIN_MIN &&
        domain_id <= TURBO_ERROR_CUSTOM_DOMAIN_MAX) {
      info.custom_domain = domain_id;
      info.domain_name = "custom";
      info.message = turbo_unknown_message(err);
      return info;
    }
  }

  native_code = err < 0 ? -err : err;
  native_msg = turbo_posix_strerror(native_code);
  if (native_msg) {
    info.domain = TURBO_ERROR_DOMAIN_POSIX;
    info.domain_name = "posix";
    info.name = "POSIX";
    info.message = native_msg;
    return info;
  }

#ifdef _WIN32
  native_msg = turbo_win32_strerror(native_code);
  if (native_msg) {
    info.domain = TURBO_ERROR_DOMAIN_WIN32;
    info.domain_name = "win32";
    info.name = "WIN32";
    info.message = native_msg;
    return info;
  }
#endif

  info.message = turbo_unknown_message(err);
  return info;
}

const char *turbo_strerror(int err) {
  return turbo_error_info(err).message;
}
