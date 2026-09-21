#include "salts_fs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int expect(int condition, const char *message) {
  if (condition)
    return 0;
  fprintf(stderr, "FAIL: %s\n", message);
  return 1;
}

int main(void) {
  const char *staging = "salts_fs_replace_fault_stage.tmp";
  const char *destination = "salts_fs_replace_fault_destination.tmp";
  salts_fs_buf_t old_bytes = salts_fs_buf_init((char *)"old", 3u);
  salts_fs_buf_t new_bytes = salts_fs_buf_init((char *)"new-durable", 11u);
  salts_fs_buf_t actual = {0};
  salts_fs_replace_state_t state = SALTS_FS_REPLACE_NOT_PUBLISHED;
  int rc;
  int failed = 0;
  char cross_stage[256];
  const char *cross_destination = "salts_fs_cross_device_destination.tmp";
  struct stat current_stat;
  struct stat shm_stat;

  (void)salts_fs_unlink(staging);
  (void)salts_fs_unlink(destination);
  (void)salts_fs_unlink(cross_destination);

  failed |= expect(salts_fs_write_file(destination, &old_bytes) == 0,
                   "write old destination");
  failed |= expect(salts_fs_write_file(staging, &new_bytes) == 0,
                   "write staging file");

  rc = salts_fs_replace_durable(staging, destination, &state);
  failed |= expect(rc == -EIO,
                   "post-rename parent fsync fault must surface EIO");
  failed |= expect(state == SALTS_FS_REPLACE_DURABILITY_UNKNOWN,
                   "post-rename fsync fault must report DURABILITY_UNKNOWN");
  failed |= expect(salts_fs_access(staging, SALTS_FS_ACCESS_EXISTS) < 0,
                   "renamed staging path must be gone");
  failed |= expect(salts_fs_read_file(destination, &actual) == 0,
                   "published destination must be readable");
  if (actual.base != NULL) {
    failed |= expect(actual.len == new_bytes.len,
                     "published destination length");
    failed |= expect(actual.len == new_bytes.len &&
                         memcmp(actual.base, new_bytes.base, new_bytes.len) == 0,
                     "published destination contains complete staging bytes");
    salts_fs_buf_free(&actual);
  }

  failed |= expect(stat(".", &current_stat) == 0, "stat current filesystem");
  failed |= expect(stat("/dev/shm", &shm_stat) == 0, "stat /dev/shm");
  failed |= expect(current_stat.st_dev != shm_stat.st_dev,
                   "canonical Linux CI requires /dev/shm to be a different filesystem");

  (void)snprintf(cross_stage, sizeof(cross_stage),
                 "/dev/shm/salts-fs-cross-device-%ld.tmp", (long)getpid());
  (void)salts_fs_unlink(cross_stage);
  failed |= expect(salts_fs_write_file(cross_stage, &new_bytes) == 0,
                   "write cross-device staging file");
  failed |= expect(salts_fs_write_file(cross_destination, &old_bytes) == 0,
                   "write cross-device destination");
  state = SALTS_FS_REPLACE_PUBLISHED_DURABLE;
  rc = salts_fs_replace_durable(cross_stage, cross_destination, &state);
  failed |= expect(rc == -EXDEV,
                   "cross-filesystem publication must fail with EXDEV");
  failed |= expect(state == SALTS_FS_REPLACE_NOT_PUBLISHED,
                   "cross-filesystem failure must remain NOT_PUBLISHED");
  failed |= expect(salts_fs_access(cross_stage, SALTS_FS_ACCESS_EXISTS) == 0,
                   "cross-device staging file must remain caller-owned");
  actual = (salts_fs_buf_t){0};
  failed |= expect(salts_fs_read_file(cross_destination, &actual) == 0,
                   "old cross-device destination remains readable");
  if (actual.base != NULL) {
    failed |= expect(actual.len == old_bytes.len &&
                         memcmp(actual.base, old_bytes.base, old_bytes.len) == 0,
                     "cross-device failure leaves old destination authoritative");
    salts_fs_buf_free(&actual);
  }

  (void)salts_fs_unlink(staging);
  (void)salts_fs_unlink(destination);
  (void)salts_fs_unlink(cross_stage);
  (void)salts_fs_unlink(cross_destination);

  if (failed != 0)
    return 1;
  puts("PASS: durable replace uncertain-publication and cross-device contract");
  return 0;
}
