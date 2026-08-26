/**
 * Unified Audio Capture Implementation using miniaudio
 *
 * Cross-platform audio capture supporting:
 * - Windows (WASAPI, DirectSound, WinMM)
 * - macOS/iOS (Core Audio)
 * - Linux (ALSA, PulseAudio, JACK)
 * - Android (AAudio, OpenSL|ES)
 * - Web (Web Audio via Emscripten)
 */

#include "miniaudio.h"

#include "turbo_capture.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =============================================================================
 * Context Structure
 * ============================================================================= */

typedef struct {
    ma_device device;
    ma_device_config device_config;
    ma_context context;
    int context_initialized;

    turbo_audio_capture_config_t config;
    turbo_capture_t *capture;

    /* Monotonic sample counter for timestamp derivation, owned per device. */
    uint64_t sample_counter;

    /* Device selection */
    ma_device_id *device_id;
    char device_id_str[128];
} miniaudio_capture_ctx_t;

/* =============================================================================
 * miniaudio Capture Callback
 * ============================================================================= */

static void audio_capture_callback(ma_device *pDevice, void *pOutput,
                                    const void *pInput, ma_uint32 frameCount) {
    miniaudio_capture_ctx_t *ctx = (miniaudio_capture_ctx_t *)pDevice->pUserData;
    (void)pOutput;

    if (!ctx || !ctx->capture || !ctx->capture->audio_cb) return;
    if (ctx->capture->state != TURBO_CAPTURE_STATE_RUNNING) return;

    size_t bytes_per_sample = (ctx->config.bits_per_sample == 32) ? 4 : 2;
    size_t len = frameCount * ctx->config.channels * bytes_per_sample;

    /* Get timestamp in microseconds */
    uint64_t timestamp = (ctx->sample_counter * 1000000ULL) / ctx->config.sample_rate;
    ctx->sample_counter += frameCount;

    ctx->capture->audio_cb(ctx->capture, (const uint8_t *)pInput, len,
                           timestamp, ctx->capture->user_data);
}

/* =============================================================================
 * Device Enumeration
 * ============================================================================= */

int turbo_capture_list_audio_devices(turbo_capture_device_t *devices, int max_count) {
    if (!devices || max_count <= 0) return -1;

    ma_context context;
    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
        return TURBO_CAPTURE_ERR_DEVICE;
    }

    ma_device_info *capture_infos;
    ma_uint32 capture_count;
    ma_device_info *playback_infos;
    ma_uint32 playback_count;

    if (ma_context_get_devices(&context, &playback_infos, &playback_count,
                               &capture_infos, &capture_count) != MA_SUCCESS) {
        ma_context_uninit(&context);
        return TURBO_CAPTURE_ERR_DEVICE;
    }

    int count = 0;
    int has_default = 0;
    for (ma_uint32 i = 0; i < capture_count && count < max_count; i++) {
        turbo_capture_device_t *dev = &devices[count];
        memset(dev, 0, sizeof(*dev));

        dev->index = count;
        dev->type = TURBO_CAPTURE_TYPE_AUDIO;
        dev->is_default = capture_infos[i].isDefault ? 1 : 0;
        has_default |= dev->is_default;

        strncpy(dev->name, capture_infos[i].name, sizeof(dev->name) - 1);
        dev->name[sizeof(dev->name) - 1] = '\0';

        /* Use index as ID string */
        snprintf(dev->id, sizeof(dev->id), "%u", i);

        count++;
    }

    /* Some backends expose a usable fallback entry without flagging it. */
    if (count > 0 && !has_default) {
        devices[0].is_default = 1;
    }

    ma_context_uninit(&context);
    return count;
}

/* =============================================================================
 * Audio Capture Creation
 * ============================================================================= */

turbo_capture_t *turbo_audio_capture_create(const char *device_id,
                                             const turbo_audio_capture_config_t *config) {
    turbo_capture_t *capture = (turbo_capture_t *)calloc(1, sizeof(turbo_capture_t));
    if (!capture) return NULL;

    miniaudio_capture_ctx_t *ctx = (miniaudio_capture_ctx_t *)calloc(1, sizeof(miniaudio_capture_ctx_t));
    if (!ctx) {
        free(capture);
        return NULL;
    }

    capture->type = TURBO_CAPTURE_TYPE_AUDIO;
    capture->state = TURBO_CAPTURE_STATE_STOPPED;
    capture->platform_ctx = ctx;
    ctx->capture = capture;

    /* Store config */
    if (config) {
        ctx->config = *config;
    } else {
        ctx->config.sample_rate = 48000;
        ctx->config.channels = 1;
        ctx->config.bits_per_sample = 16;
        ctx->config.frame_size_ms = 20;
    }

    /* Store device ID if provided */
    if (device_id && device_id[0]) {
        strncpy(ctx->device_id_str, device_id, sizeof(ctx->device_id_str) - 1);
    }

    /* Initialize context */
    if (ma_context_init(NULL, 0, NULL, &ctx->context) != MA_SUCCESS) {
        free(ctx);
        free(capture);
        return NULL;
    }
    ctx->context_initialized = 1;

    /* Configure device */
    ma_format format = (ctx->config.bits_per_sample == 32) ? ma_format_s32 : ma_format_s16;

    ctx->device_config = ma_device_config_init(ma_device_type_capture);
    ctx->device_config.capture.format = format;
    ctx->device_config.capture.channels = ctx->config.channels;
    ctx->device_config.sampleRate = ctx->config.sample_rate;
    ctx->device_config.dataCallback = audio_capture_callback;
    ctx->device_config.pUserData = ctx;

    /* Calculate period size from frame_size_ms */
    ctx->device_config.periodSizeInFrames =
        (ctx->config.sample_rate * ctx->config.frame_size_ms) / 1000;

    /* Select device by index if specified */
    if (device_id && device_id[0]) {
        ma_device_info *capture_infos;
        ma_uint32 capture_count;
        ma_device_info *playback_infos;
        ma_uint32 playback_count;

        if (ma_context_get_devices(&ctx->context, &playback_infos, &playback_count,
                                   &capture_infos, &capture_count) == MA_SUCCESS) {
            int idx = atoi(device_id);
            if (idx >= 0 && (ma_uint32)idx < capture_count) {
                ctx->device_config.capture.pDeviceID = &capture_infos[idx].id;
            }
        }
    }

    /* Initialize device */
    if (ma_device_init(&ctx->context, &ctx->device_config, &ctx->device) != MA_SUCCESS) {
        ma_context_uninit(&ctx->context);
        free(ctx);
        free(capture);
        return NULL;
    }

    return capture;
}

/* =============================================================================
 * Callback Setup
 * ============================================================================= */

void turbo_audio_capture_set_callback(turbo_capture_t *capture,
                                       turbo_audio_capture_cb cb,
                                       void *user_data) {
    if (!capture) return;
    capture->audio_cb = cb;
    capture->user_data = user_data;
}

/* =============================================================================
 * Platform Hooks (called from capture dispatcher)
 * ============================================================================= */

int miniaudio_audio_start(turbo_capture_t *capture) {
    if (!capture || !capture->platform_ctx) return -1;
    miniaudio_capture_ctx_t *ctx = (miniaudio_capture_ctx_t *)capture->platform_ctx;

    /* Restart resets the per-device timestamp origin. */
    ctx->sample_counter = 0;

    if (ma_device_start(&ctx->device) != MA_SUCCESS) {
        return TURBO_CAPTURE_ERR_DEVICE;
    }

    return TURBO_CAPTURE_OK;
}

void miniaudio_audio_stop(turbo_capture_t *capture) {
    if (!capture || !capture->platform_ctx) return;
    miniaudio_capture_ctx_t *ctx = (miniaudio_capture_ctx_t *)capture->platform_ctx;

    ma_device_stop(&ctx->device);
}

void miniaudio_audio_destroy(turbo_capture_t *capture) {
    if (!capture) return;

    miniaudio_capture_ctx_t *ctx = (miniaudio_capture_ctx_t *)capture->platform_ctx;
    if (ctx) {
        ma_device_uninit(&ctx->device);
        if (ctx->context_initialized) {
            ma_context_uninit(&ctx->context);
        }
        free(ctx);
    }

    free(capture);
}

#if defined(__linux__) && !defined(__ANDROID__)
int linux_audio_start(turbo_capture_t *capture) {
    return miniaudio_audio_start(capture);
}

void linux_audio_stop(turbo_capture_t *capture) {
    miniaudio_audio_stop(capture);
}

void linux_audio_destroy(turbo_capture_t *capture) {
    miniaudio_audio_destroy(capture);
}
#endif

#if defined(__APPLE__) && defined(__MACH__)
int coreaudio_start(turbo_capture_t *capture) {
    return miniaudio_audio_start(capture);
}

void coreaudio_stop(turbo_capture_t *capture) {
    miniaudio_audio_stop(capture);
}

void coreaudio_destroy(turbo_capture_t *capture) {
    miniaudio_audio_destroy(capture);
}
#endif
