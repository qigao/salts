#include "tlog.h"
#include "tinytest.hpp"

#include <cstdio>
#include <string>

static int cpp_surface_count;
static std::string cpp_surface_message;

static void cpp_surface_capture(const salts_log_entry_t *entry, void *user_data) {
  (void)user_data;
  ++cpp_surface_count;
  cpp_surface_message = entry->message ? entry->message : "";
}

spec("TLog C++ surface contract") {
  it("keeps native C++ type detection on formatted logging") {
    tlog_t *logger = tlog_create(NULL);
    salts_log_sink_t *sink = salts_sink_callback_create(cpp_surface_capture, NULL);
    check_not_null(logger);
    check_not_null(sink);
    check_equal(tlog_add_sink(logger, sink), 0);
    tlog_set_default(logger);

    cpp_surface_count = 0;
    TLOG_INFO("ready");
    std::string value = "ok";
    TLOG_INFOF("value={}", value);
    tlog_flush(logger);

    check_equal(cpp_surface_count, 2);
    check_equal(cpp_surface_message, std::string("value=ok"));
    tlog_set_default(NULL);
    tlog_destroy(logger);
  }
}
