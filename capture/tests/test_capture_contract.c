#include <turbo_capture.h>
#include <tinytest.h>

#include <stdint.h>

suite("capture portable contract") {
  it("rounds rational frame rates to the nearest integer") {
    turbo_video_native_mode_t ntsc = {
        1920, 1080, 30000u, 1001u, TURBO_VIDEO_CAPTURE_FORMAT_NV12, 1u};

    check_equal(turbo_video_mode_fps(&ntsc), 30);
  }

  it("returns zero for an absent or unrepresentable frame rate") {
    turbo_video_native_mode_t no_denominator = {
        640, 480, 30u, 0u, TURBO_VIDEO_CAPTURE_FORMAT_NV12, 2u};
    turbo_video_native_mode_t too_large = {
        640, 480, UINT32_MAX, 1u, TURBO_VIDEO_CAPTURE_FORMAT_NV12, 3u};

    check_equal(turbo_video_mode_fps(NULL), 0);
    check_equal(turbo_video_mode_fps(&no_denominator), 0);
    check_equal(turbo_video_mode_fps(&too_large), 0);
  }

  it("recognizes rounded broadcast and webcam frame rates") {
    turbo_video_native_mode_t mode = {
        640, 480, 24000u, 1001u, TURBO_VIDEO_CAPTURE_FORMAT_NV12, 4u};

    check_equal(turbo_video_mode_is_standard_fps(&mode), 1);
    mode.framerate_numerator = 30000u;
    check_equal(turbo_video_mode_is_standard_fps(&mode), 1);
    mode.framerate_numerator = 60000u;
    check_equal(turbo_video_mode_is_standard_fps(&mode), 1);
    mode.framerate_numerator = 23u;
    mode.framerate_denominator = 1u;
    check_equal(turbo_video_mode_is_standard_fps(&mode), 0);
  }

  it("rejects invalid device adapter arguments") {
    turbo_video_device_t *device = NULL;

    check_equal(turbo_video_device_open(NULL, NULL),
                TURBO_CAPTURE_ERR_FORMAT);
    check_equal(turbo_video_device_create_capture(NULL, NULL, NULL),
                TURBO_CAPTURE_ERR_FORMAT);
    check_null(device);
  }

  it("filters the default mode list without changing native identities") {
    turbo_video_device_t *device = NULL;
    turbo_video_native_mode_t modes[5];
    size_t count = 0u;

    check_equal(turbo_video_device_open("fake", &device), TURBO_CAPTURE_OK);
    check_not_null(device);
    check_equal(turbo_video_device_list_modes(device, modes, 5u, &count),
                TURBO_CAPTURE_OK);
    check_equal(count, (size_t)4u);
    check_equal(modes[0].mode_id, (uint64_t)2u);
    check_equal(modes[1].mode_id, (uint64_t)3u);
    check_equal(modes[2].mode_id, (uint64_t)4u);
    check_equal(modes[3].mode_id, (uint64_t)5u);
    turbo_video_device_close(device);
  }

  it("honors caller capacity when listing every native mode") {
    turbo_video_device_t *device = NULL;
    turbo_video_native_mode_t modes[2];
    size_t count = 99u;

    check_equal(turbo_video_device_open(NULL, &device), TURBO_CAPTURE_OK);
    check_equal(turbo_video_device_list_modes_all(device, modes, 2u, &count),
                TURBO_CAPTURE_OK);
    check_equal(count, (size_t)2u);
    check_equal(modes[0].mode_id, (uint64_t)1u);
    check_equal(modes[1].mode_id, (uint64_t)2u);
    turbo_video_device_close(device);
  }

  it("clears mode counts and capture outputs on invalid input") {
    turbo_video_device_t *device = NULL;
    turbo_video_native_mode_t mode = {
        640, 480, 30u, 1u, TURBO_VIDEO_CAPTURE_FORMAT_NV12, 1u};
    turbo_capture_t *capture = (turbo_capture_t *)(uintptr_t)1u;
    size_t count = 99u;

    check_equal(turbo_video_device_list_modes(NULL, &mode, 1u, &count),
                TURBO_CAPTURE_ERR_FORMAT);
    check_equal(count, (size_t)0u);
    check_equal(turbo_video_device_create_capture(NULL, &mode, &capture),
                TURBO_CAPTURE_ERR_FORMAT);
    check_null(capture);
    check_null(device);
  }
}
