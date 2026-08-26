#include <turbo_capture.h>
#include <tinytest.h>

suite("capture Android lifecycle") {
  it("starts the native screen lifecycle before Java produces frames") {
    turbo_screen_capture_config_t config = {0, 30, 1, 0};
    turbo_capture_t *capture = turbo_screen_capture_create(&config);

    check_not_null(capture);
    if (!capture) return;

    check_equal(turbo_capture_get_state(capture),
                TURBO_CAPTURE_STATE_STOPPED);
    check_equal(turbo_capture_start(capture), TURBO_CAPTURE_OK);
    check_equal(turbo_capture_get_state(capture),
                TURBO_CAPTURE_STATE_RUNNING);

    turbo_capture_stop(capture);
    check_equal(turbo_capture_get_state(capture),
                TURBO_CAPTURE_STATE_STOPPED);
    turbo_capture_destroy(capture);
  }
}
