#include <turbo_capture.h>
#include <tinytest.h>

suite("POSIX capture integration") {
  it("rejects invalid enumeration buffers") {
    turbo_capture_device_t device;

    check_equal(turbo_capture_list_audio_devices(NULL, 1), -1);
    check_equal(turbo_capture_list_video_devices(NULL, 1), -1);
    check_equal(turbo_capture_list_screens(NULL, 1), -1);
    check_equal(turbo_capture_list_gpu_devices(NULL, 1), -1);
    check_equal(turbo_capture_list_audio_devices(&device, 0), -1);
    check_equal(turbo_capture_list_video_devices(&device, 0), -1);
    check_equal(turbo_capture_list_screens(&device, 0), -1);
    check_equal(turbo_capture_list_gpu_devices(&device, 0), -1);
  }

  it("reports device absence without exceeding caller capacity") {
    turbo_capture_device_t devices[TURBO_CAPTURE_MAX_DEVICES];
    int audio_count = turbo_capture_list_audio_devices(
        devices, TURBO_CAPTURE_MAX_DEVICES);
    int video_count = turbo_capture_list_video_devices(
        devices, TURBO_CAPTURE_MAX_DEVICES);
    int screen_count = turbo_capture_list_screens(
        devices, TURBO_CAPTURE_MAX_DEVICES);

    check_greater_equal(audio_count, 0);
    check_less_equal(audio_count, TURBO_CAPTURE_MAX_DEVICES);
    check_greater_equal(video_count, 0);
    check_less_equal(video_count, TURBO_CAPTURE_MAX_DEVICES);
    check_greater_equal(screen_count, 0);
    check_less_equal(screen_count, TURBO_CAPTURE_MAX_DEVICES);
  }

  it("makes null lifecycle operations safe and deterministic") {
    check_equal(turbo_capture_start(NULL), -1);
    check_equal(turbo_capture_get_state(NULL), TURBO_CAPTURE_STATE_STOPPED);
    turbo_capture_stop(NULL);
    turbo_capture_destroy(NULL);
  }
}
