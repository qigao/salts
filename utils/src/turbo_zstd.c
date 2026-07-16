#include "turbo_zstd.h"

#include "turbo_error.h"

#include <stdint.h>
#include <zstd.h>
#include <zstd_errors.h>

static int turbo_zstd_error(size_t result) {
  ZSTD_ErrorCode code = ZSTD_getErrorCode(result);

  switch (code) {
  case ZSTD_error_parameter_unsupported:
  case ZSTD_error_parameter_combination_unsupported:
  case ZSTD_error_parameter_outOfBound:
  case ZSTD_error_dstBuffer_null:
  case ZSTD_error_srcBuffer_wrong:
    return TURBO_EINVAL;
  case ZSTD_error_memory_allocation:
    return TURBO_ENOMEM;
  case ZSTD_error_workSpace_tooSmall:
  case ZSTD_error_dstSize_tooSmall:
  case ZSTD_error_noForwardProgress_destFull:
    return TURBO_ENOSPC;
  case ZSTD_error_prefix_unknown:
  case ZSTD_error_version_unsupported:
  case ZSTD_error_frameParameter_unsupported:
  case ZSTD_error_frameParameter_windowTooLarge:
  case ZSTD_error_corruption_detected:
  case ZSTD_error_checksum_wrong:
  case ZSTD_error_literals_headerWrong:
  case ZSTD_error_dictionary_corrupted:
  case ZSTD_error_dictionary_wrong:
  case ZSTD_error_dictionaryCreation_failed:
  case ZSTD_error_srcSize_wrong:
  case ZSTD_error_noForwardProgress_inputEmpty:
    return TURBO_EPROTO;
  case ZSTD_error_no_error:
    return TURBO_OK;
  default:
    return TURBO_EIO;
  }
}

int turbo_zstd_compress_bound(size_t source_size, size_t *out_bound) {
  size_t bound;

  if (out_bound == NULL) {
    return TURBO_EINVAL;
  }

  bound = ZSTD_compressBound(source_size);
  if (ZSTD_isError(bound)) {
    if (ZSTD_getErrorCode(bound) == ZSTD_error_srcSize_wrong) {
      return TURBO_ERANGE;
    }
    return turbo_zstd_error(bound);
  }

  *out_bound = bound;
  return TURBO_OK;
}

int turbo_zstd_compress(void *destination, size_t destination_capacity,
                        size_t *out_size, const void *source,
                        size_t source_size, int level) {
  static const unsigned char empty_source = 0U;
  size_t result;

  if (destination == NULL || out_size == NULL ||
      (source == NULL && source_size != 0U)) {
    return TURBO_EINVAL;
  }
  if (level != 0 &&
      (level < ZSTD_minCLevel() || level > ZSTD_maxCLevel())) {
    return TURBO_EINVAL;
  }

  result = ZSTD_compress(destination, destination_capacity,
                         source != NULL ? source : &empty_source, source_size,
                         level);
  if (ZSTD_isError(result)) {
    return turbo_zstd_error(result);
  }

  *out_size = result;
  return TURBO_OK;
}

int turbo_zstd_decompress(void *destination, size_t destination_capacity,
                          size_t *out_size, const void *source,
                          size_t source_size) {
  unsigned char empty_destination;
  size_t result;

  if (out_size == NULL || source == NULL || source_size == 0U ||
      (destination == NULL && destination_capacity != 0U)) {
    return TURBO_EINVAL;
  }

  result = ZSTD_decompress(destination != NULL ? destination : &empty_destination,
                           destination_capacity, source, source_size);
  if (ZSTD_isError(result)) {
    return turbo_zstd_error(result);
  }

  *out_size = result;
  return TURBO_OK;
}

int turbo_zstd_frame_content_size(const void *source, size_t source_size,
                                  size_t *out_size) {
  unsigned long long content_size;

  if (source == NULL || source_size == 0U || out_size == NULL) {
    return TURBO_EINVAL;
  }

  content_size = ZSTD_getFrameContentSize(source, source_size);
  if (content_size == ZSTD_CONTENTSIZE_ERROR) {
    return TURBO_EPROTO;
  }
  if (content_size == ZSTD_CONTENTSIZE_UNKNOWN) {
    return TURBO_ENOTSUP;
  }
  if (content_size > (unsigned long long)SIZE_MAX) {
    return TURBO_ERANGE;
  }

  *out_size = (size_t)content_size;
  return TURBO_OK;
}

int turbo_zstd_default_level(void) { return ZSTD_defaultCLevel(); }

int turbo_zstd_min_level(void) { return ZSTD_minCLevel(); }

int turbo_zstd_max_level(void) { return ZSTD_maxCLevel(); }
