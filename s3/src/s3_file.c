#include "s3_internal.h"

#include <s3/s3_object.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <turbo_fs.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { S3_FILE_HASH_CHUNK_BYTES = 64u * 1024u, S3_FILE_SHA256_BYTES = 32 };

static int s3_file_source_unexpected(void *user, void *buffer, size_t capacity, size_t *out_size) {
  (void)user;
  (void)buffer;
  (void)capacity;
  if (out_size != NULL) *out_size = 0u;
  return TURBO_EPROTO;
}

static int s3_file_sha256(const char *path, char out_hex[S3_SIGNER_SHA256_HEX_SIZE + 1u],
                          size_t *out_size, s3_error *out_error) {
  static const char digits[] = "0123456789abcdef";
  turbo_fs_stat_t file_stat = {0};
  turbo_fs_stat_t after_stat = {0};
  turbo_file_t file = TURBO_INVALID_FILE;
  EVP_MD_CTX *context = NULL;
  unsigned char digest[S3_FILE_SHA256_BYTES];
  unsigned char buffer[S3_FILE_HASH_CHUNK_BYTES];
  unsigned int digest_size = 0u;
  size_t index;
  int read_size;
  int close_status = TURBO_OK;
  int status;
  if (path == NULL || path[0] == '\0' || out_hex == NULL || out_size == NULL || out_error == NULL)
    return TURBO_EINVAL;
  status = turbo_fs_stat(path, &file_stat);
  if (status != TURBO_OK) {
    *out_error = (s3_error){.status = TURBO_EIO, .native_status = status, .stage = "file-stat"};
    return TURBO_EIO;
  }
  if (!file_stat.is_file) {
    *out_error = (s3_error){.status = TURBO_EISDIR, .stage = "file-stat"};
    return TURBO_EISDIR;
  }
  if (file_stat.size > SIZE_MAX) {
    *out_error = (s3_error){.status = TURBO_EFBIG, .stage = "file-stat"};
    return TURBO_EFBIG;
  }
  file = turbo_fs_open(path, TURBO_FS_O_RDONLY, 0);
  if (file == TURBO_INVALID_FILE) {
    *out_error = (s3_error){.status = TURBO_EIO, .stage = "file-hash-open"};
    return TURBO_EIO;
  }
  context = EVP_MD_CTX_new();
  if (context == NULL) {
    status = TURBO_ENOMEM;
    goto done;
  }
  if (EVP_DigestInit_ex(context, EVP_sha256(), NULL) != 1) {
    status = TURBO_EIO;
    goto done;
  }
  status = TURBO_OK;
  while ((read_size = turbo_fs_read(file, (char *)buffer, sizeof(buffer))) > 0) {
    if (EVP_DigestUpdate(context, buffer, (size_t)read_size) != 1) {
      status = TURBO_EIO;
      break;
    }
  }
  if (read_size < 0 && status == TURBO_OK) {
    status = TURBO_EIO;
    *out_error =
        (s3_error){.status = status, .native_status = read_size, .stage = "file-hash-read"};
  }
  if (status == TURBO_OK &&
      (EVP_DigestFinal_ex(context, digest, &digest_size) != 1 || digest_size != sizeof(digest)))
    status = TURBO_EIO;
  if (status == TURBO_OK) {
    for (index = 0u; index < sizeof(digest); ++index) {
      out_hex[index * 2u] = digits[digest[index] >> 4u];
      out_hex[index * 2u + 1u] = digits[digest[index] & 0x0fu];
    }
    out_hex[S3_SIGNER_SHA256_HEX_SIZE] = '\0';
    *out_size = (size_t)file_stat.size;
  }

done:
  EVP_MD_CTX_free(context);
  OPENSSL_cleanse(buffer, sizeof(buffer));
  OPENSSL_cleanse(digest, sizeof(digest));
  close_status = turbo_fs_close(file);
  if (status == TURBO_OK && close_status != TURBO_OK) {
    status = TURBO_EIO;
    *out_error =
        (s3_error){.status = status, .native_status = close_status, .stage = "file-hash-close"};
  }
  if (status == TURBO_OK) {
    const int stat_status = turbo_fs_stat(path, &after_stat);
    if (stat_status != TURBO_OK) {
      status = TURBO_EIO;
      *out_error =
          (s3_error){.status = status, .native_status = stat_status, .stage = "file-restat"};
    } else if (!after_stat.is_file || after_stat.size != file_stat.size ||
               after_stat.mtime != file_stat.mtime || after_stat.ctime != file_stat.ctime) {
      status = TURBO_EBUSY;
      *out_error = (s3_error){.status = status, .stage = "file-changed"};
    }
  }
  if (status != TURBO_OK && out_error->status == TURBO_OK)
    *out_error = (s3_error){.status = status, .stage = "file-hash"};
  return status;
}

static int s3_file_put_headers(s3_client_impl *client, const s3_put_object_options *options,
                               s3_sse_headers *sse_headers, chttp_header headers[4],
                               size_t *out_count) {
  size_t count = 0u;
  size_t index;
  int status;
  if (client == NULL || options == NULL || options->size != sizeof(*options) ||
      sse_headers == NULL || headers == NULL || out_count == NULL)
    return TURBO_EINVAL;
  status = s3_sse_headers_build(options->sse, 1, client->base.config.max_header_bytes, sse_headers);
  if (status != TURBO_OK) return status;
  if (options->content_type != NULL)
    headers[count++] = (chttp_header){"Content-Type", options->content_type};
  for (index = 0u; index < sse_headers->count; ++index)
    headers[count++] = sse_headers->items[index];
  *out_count = count;
  return TURBO_OK;
}

int s3_put_object_file(s3_client *client, const char *bucket, const char *key, const char *path,
                       const s3_put_object_options *options, chttp_progress_fn progress,
                       void *progress_user, s3_response *out_response, s3_error *out_error) {
  s3_client_impl *impl = client != NULL ? (s3_client_impl *)client->impl : NULL;
  s3_sse_headers sse_headers = {0};
  chttp_header headers[4];
  chttp_body_source source = {.read = s3_file_source_unexpected, .content_length_known = 1};
  s3_request_options request;
  char payload_sha256[S3_SIGNER_SHA256_HEX_SIZE + 1u] = {0};
  size_t header_count = 0u;
  size_t file_size = 0u;
  int status;
  if (out_error == NULL || out_response == NULL || !s3_response_is_empty(out_response))
    return TURBO_EINVAL;
  *out_error = (s3_error){0};
  if (impl == NULL || options == NULL || options->size != sizeof(*options)) {
    *out_error = (s3_error){.status = TURBO_EINVAL, .stage = "s3-put-file"};
    return TURBO_EINVAL;
  }
  status = s3_file_put_headers(impl, options, &sse_headers, headers, &header_count);
  if (status == TURBO_OK) status = s3_file_sha256(path, payload_sha256, &file_size, out_error);
  source.content_length = file_size;
  request = (s3_request_options){.size = sizeof(request),
                                 .method = S3_METHOD_PUT,
                                 .bucket = bucket,
                                 .key = key,
                                 .headers = headers,
                                 .header_count = header_count,
                                 .body_source = &source,
                                 .payload_sha256 = payload_sha256};
  if (status == TURBO_OK)
    status = s3_request_put_file(client, &request, path, progress, progress_user, out_response,
                                 out_error);
  if (status != TURBO_OK && out_error->status == TURBO_OK)
    *out_error = (s3_error){.status = status, .stage = "s3-put-file"};
  OPENSSL_cleanse(payload_sha256, sizeof(payload_sha256));
  s3_sse_headers_destroy(&sse_headers);
  return status;
}

int s3_get_object_file(s3_client *client, const char *bucket, const char *key,
                       const char *output_path, const s3_get_object_options *options,
                       chttp_progress_fn progress, void *progress_user, s3_response *out_response,
                       s3_error *out_error) {
  s3_client_impl *impl = client != NULL ? (s3_client_impl *)client->impl : NULL;
  s3_sse_headers sse_headers = {0};
  s3_request_options request;
  int status;
  if (out_error == NULL || out_response == NULL || !s3_response_is_empty(out_response))
    return TURBO_EINVAL;
  *out_error = (s3_error){0};
  if (impl == NULL || (options != NULL && options->size != sizeof(*options))) {
    *out_error = (s3_error){.status = TURBO_EINVAL, .stage = "s3-get-file"};
    return TURBO_EINVAL;
  }
  status = s3_sse_headers_build(options != NULL ? options->sse : NULL, 0,
                                impl->base.config.max_header_bytes, &sse_headers);
  request = (s3_request_options){.size = sizeof(request),
                                 .method = S3_METHOD_GET,
                                 .bucket = bucket,
                                 .key = key,
                                 .headers = sse_headers.items,
                                 .header_count = sse_headers.count};
  if (status == TURBO_OK)
    status = s3_request_get_file(client, &request, output_path, progress, progress_user,
                                 out_response, out_error);
  if (status != TURBO_OK && out_error->status == TURBO_OK)
    *out_error = (s3_error){.status = status, .stage = "s3-get-file"};
  s3_sse_headers_destroy(&sse_headers);
  return status;
}
