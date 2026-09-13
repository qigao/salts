#include "tlog.h"
#include "tinytest.h"

#include <stdio.h>
#include <string.h>

static int surface_count;
static char surface_message[128];
static char surface_component[64];

static void copy_view(char *dst, size_t dst_size, vstr value) {
  size_t n;
  if (dst_size == 0) return;
  n = value.len < dst_size - 1 ? value.len : dst_size - 1;
  if (n > 0) memcpy(dst, value.data, n);
  dst[n] = '\0';
}

static void surface_capture(const salts_log_entry_t *entry, void *user_data) {
  (void)user_data;
  ++surface_count;
  copy_view(surface_message, sizeof(surface_message), entry->message);
  copy_view(surface_component, sizeof(surface_component), entry->component);
}

spec("TLog surface contract") {
  it("separates raw and formatted explicit-logger calls") {
    tlog_t *logger = tlog_create(NULL);
    salts_log_sink_t *sink = salts_sink_callback_create(surface_capture, NULL);
    check_not_null(logger);
    check_not_null(sink);
    check_equal(tlog_add_sink(logger, sink), 0);

    surface_count = 0;
    SALTS_LOG_INFO(logger, "worker", "ready");
    SALTS_LOG_INFOF(logger, "worker", "value={}", 7);
    tlog_flush(logger);

    check_equal(surface_count, 2);
    check_equal(surface_component, "worker");
    check_equal(surface_message, "value=7");
    tlog_destroy(logger);
  }

  it("separates raw and formatted default-logger calls") {
    tlog_t *logger = tlog_create(NULL);
    salts_log_sink_t *sink = salts_sink_callback_create(surface_capture, NULL);
    check_not_null(logger);
    check_not_null(sink);
    check_equal(tlog_add_sink(logger, sink), 0);
    tlog_set_default(logger);

    surface_count = 0;
    TLOG_INFO("ready");
    TLOG_INFOF("value={}", 9);
    tlog_flush(logger);

    check_equal(surface_count, 2);
    check_equal(surface_message, "value=9");
    tlog_set_default(NULL);
    tlog_destroy(logger);
  }
}
