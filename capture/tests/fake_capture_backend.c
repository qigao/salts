#include "capture_video_backend.h"

#include <stdint.h>
#include <string.h>

static const turbo_video_native_mode_t fake_modes[] = {
    {640, 480, 23u, 1u, TURBO_VIDEO_CAPTURE_FORMAT_NV12, 1u},
    {640, 480, 24000u, 1001u, TURBO_VIDEO_CAPTURE_FORMAT_NV12, 2u},
    {1280, 720, 30000u, 1001u, TURBO_VIDEO_CAPTURE_FORMAT_MJPEG, 3u},
    {1920, 1080, 60000u, 1001u, TURBO_VIDEO_CAPTURE_FORMAT_NV12, 4u},
    {1920, 1080, 25u, 1u, TURBO_VIDEO_CAPTURE_FORMAT_I420, 5u},
};

static int fake_open_device(const char *device_id, void **backend_ctx) {
  static int fake_context;

  if (!backend_ctx) return TURBO_CAPTURE_ERR_FORMAT;
  if (device_id && strcmp(device_id, "fake") != 0)
    return TURBO_CAPTURE_ERR_DEVICE;
  *backend_ctx = &fake_context;
  return TURBO_CAPTURE_OK;
}

static void fake_close_device(void *backend_ctx) { (void)backend_ctx; }

static int fake_list_modes(void *backend_ctx,
                           turbo_video_native_mode_t *modes, size_t capacity,
                           size_t *out_count) {
  const size_t available = sizeof(fake_modes) / sizeof(fake_modes[0]);
  const size_t written = capacity < available ? capacity : available;

  if (!backend_ctx || !modes || !out_count || capacity == 0u)
    return TURBO_CAPTURE_ERR_FORMAT;
  memcpy(modes, fake_modes, written * sizeof(*modes));
  *out_count = written;
  return TURBO_CAPTURE_OK;
}

static int fake_create_capture(void *backend_ctx,
                               const turbo_video_native_mode_t *mode,
                               turbo_capture_t **out_capture) {
  (void)backend_ctx;
  (void)mode;
  if (out_capture) *out_capture = NULL;
  return TURBO_CAPTURE_ERR_UNSUPPORTED;
}

const turbo_video_backend_ops_t *turbo_video_platform_backend(void) {
  static const turbo_video_backend_ops_t ops = {
      fake_open_device, fake_close_device, fake_list_modes,
      fake_create_capture};
  return &ops;
}
