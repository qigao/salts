#include <turbo_capture.h>
#include <tinytest.h>

#include <stddef.h>

suite("Windows capture integration") {
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

  it("enumerates each device class within caller capacity") {
    turbo_capture_device_t devices[TURBO_CAPTURE_MAX_DEVICES];
    int audio_count = turbo_capture_list_audio_devices(
        devices, TURBO_CAPTURE_MAX_DEVICES);
    int video_count = turbo_capture_list_video_devices(
        devices, TURBO_CAPTURE_MAX_DEVICES);
    int screen_count = turbo_capture_list_screens(
        devices, TURBO_CAPTURE_MAX_DEVICES);
    int gpu_count = turbo_capture_list_gpu_devices(
        devices, TURBO_CAPTURE_MAX_DEVICES);

    check_greater_equal(audio_count, 0);
    check_less_equal(audio_count, TURBO_CAPTURE_MAX_DEVICES);
    check_greater_equal(video_count, 0);
    check_less_equal(video_count, TURBO_CAPTURE_MAX_DEVICES);
    check_greater_equal(screen_count, 0);
    check_less_equal(screen_count, TURBO_CAPTURE_MAX_DEVICES);
    check_greater_equal(gpu_count, 0);
    check_less_equal(gpu_count, TURBO_CAPTURE_MAX_DEVICES);
  }

  it("makes null lifecycle operations safe and deterministic") {
    check_equal(turbo_capture_start(NULL), -1);
    check_equal(turbo_capture_get_state(NULL), TURBO_CAPTURE_STATE_STOPPED);
    turbo_capture_stop(NULL);
    turbo_capture_destroy(NULL);
  }

  it("keeps enumerated camera mode identities unique per device") {
    turbo_capture_device_t devices[TURBO_CAPTURE_MAX_DEVICES];
    int device_count = turbo_capture_list_video_devices(
        devices, TURBO_CAPTURE_MAX_DEVICES);

    for (int device_index = 0; device_index < device_count; ++device_index) {
      turbo_video_native_mode_t modes[TURBO_CAPTURE_MAX_VIDEO_MODES];
      turbo_video_device_t *device = NULL;
      size_t mode_count = 0u;

      if (turbo_video_device_open(devices[device_index].id, &device) !=
          TURBO_CAPTURE_OK)
        continue;
      check_equal(turbo_video_device_list_modes_all(
                      device, modes, TURBO_CAPTURE_MAX_VIDEO_MODES,
                      &mode_count),
                  TURBO_CAPTURE_OK);
      for (size_t index = 0u; index < mode_count; ++index) {
        check_greater(modes[index].width, 0);
        check_greater(modes[index].height, 0);
        check_greater(modes[index].framerate_numerator, 0u);
        check_greater(modes[index].framerate_denominator, 0u);
        for (size_t prior = 0u; prior < index; ++prior)
          check_not_equal(modes[index].mode_id, modes[prior].mode_id);
      }
      turbo_video_device_close(device);
    }
  }
}
