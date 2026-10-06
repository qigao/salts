#include "cmeta_fs.h"
#include "tinytest.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void test_replace_fault(void) {
  const char *staging = "cmeta_fs_replace_fault_stage.tmp";
  const char *destination = "cmeta_fs_replace_fault_destination.tmp";
  cmeta_fs_buf_t old_bytes = cmeta_fs_buf_init((char *)"old", 3u);
  cmeta_fs_buf_t new_bytes = cmeta_fs_buf_init((char *)"new-durable", 11u);
  cmeta_fs_buf_t actual = {0};
  cmeta_fs_replace_state_t state = SALTS_FS_REPLACE_NOT_PUBLISHED;
  const char *requested = getenv("SALTS_FS_TEST_FAIL_FSYNC_CALL");
  const int fail_call = requested != NULL ? atoi(requested) : 2;
  int rc;

  (void)cmeta_fs_unlink(staging);
  (void)cmeta_fs_unlink(destination);

  check_warn(cmeta_fs_write_file(destination, &old_bytes) == 0,
                   "write old destination");
  check_warn(cmeta_fs_write_file(staging, &new_bytes) == 0,
                   "write staging file");

  rc = cmeta_fs_replace_durable(staging, destination, &state);

  if (fail_call == 1) {
    check_warn(rc == -EIO,
                     "staging fsync fault must surface EIO");
    check_warn(state == SALTS_FS_REPLACE_NOT_PUBLISHED,
                     "staging fsync fault must remain NOT_PUBLISHED");
    check_warn(cmeta_fs_access(staging, SALTS_FS_ACCESS_EXISTS) == 0,
                     "unpublished staging path remains caller-owned");
    check_warn(cmeta_fs_read_file(destination, &actual) == 0,
                     "old destination remains readable");
    if (actual.base != NULL) {
      check_warn(actual.len == old_bytes.len &&
                           memcmp(actual.base, old_bytes.base,
                                  old_bytes.len) == 0,
                       "staging fsync fault leaves old destination authoritative");
      cmeta_fs_buf_free(&actual);
    }
  } else if (fail_call == 2) {
    char cross_stage[256];
    const char *cross_destination = "cmeta_fs_cross_device_destination.tmp";
    struct stat current_stat;
    struct stat shm_stat;

    check_warn(rc == -EIO,
                     "post-rename parent fsync fault must surface EIO");
    check_warn(state == SALTS_FS_REPLACE_DURABILITY_UNKNOWN,
                     "post-rename fsync fault must report DURABILITY_UNKNOWN");
    check_warn(cmeta_fs_access(staging, SALTS_FS_ACCESS_EXISTS) < 0,
                     "renamed staging path must be gone");
    check_warn(cmeta_fs_read_file(destination, &actual) == 0,
                     "published destination must be readable");
    if (actual.base != NULL) {
      check_warn(actual.len == new_bytes.len,
                       "published destination length");
      check_warn(actual.len == new_bytes.len &&
                           memcmp(actual.base, new_bytes.base,
                                  new_bytes.len) == 0,
                       "published destination contains complete staging bytes");
      cmeta_fs_buf_free(&actual);
    }

    check_warn(stat(".", &current_stat) == 0,
                     "stat current filesystem");
    check_warn(stat("/dev/shm", &shm_stat) == 0, "stat /dev/shm");
    check_warn(current_stat.st_dev != shm_stat.st_dev,
                     "canonical Linux CI requires /dev/shm to be a different filesystem");

    (void)snprintf(cross_stage, sizeof(cross_stage),
                   "/dev/shm/salts-fs-cross-device-%ld.tmp", (long)getpid());
    (void)cmeta_fs_unlink(cross_stage);
    (void)cmeta_fs_unlink(cross_destination);
    check_warn(cmeta_fs_write_file(cross_stage, &new_bytes) == 0,
                     "write cross-device staging file");
    check_warn(cmeta_fs_write_file(cross_destination, &old_bytes) == 0,
                     "write cross-device destination");
    state = SALTS_FS_REPLACE_PUBLISHED_DURABLE;
    rc = cmeta_fs_replace_durable(cross_stage, cross_destination, &state);
    check_warn(rc == -EXDEV,
                     "cross-filesystem publication must fail with EXDEV");
    check_warn(state == SALTS_FS_REPLACE_NOT_PUBLISHED,
                     "cross-filesystem failure must remain NOT_PUBLISHED");
    check_warn(cmeta_fs_access(cross_stage, SALTS_FS_ACCESS_EXISTS) == 0,
                     "cross-device staging file must remain caller-owned");
    actual = (cmeta_fs_buf_t){0};
    check_warn(cmeta_fs_read_file(cross_destination, &actual) == 0,
                     "old cross-device destination remains readable");
    if (actual.base != NULL) {
      check_warn(actual.len == old_bytes.len &&
                           memcmp(actual.base, old_bytes.base,
                                  old_bytes.len) == 0,
                       "cross-device failure leaves old destination authoritative");
      cmeta_fs_buf_free(&actual);
    }
    (void)cmeta_fs_unlink(cross_stage);
    (void)cmeta_fs_unlink(cross_destination);
  } else {
    check_warn(0, "unsupported fsync fault call requested");
  }

  (void)cmeta_fs_unlink(staging);
  (void)cmeta_fs_unlink(destination);

}

suite("Durable file replacement faults") {
  group("injected filesystem failures") {
    it("preserves publication state and authoritative file contents") {
      test_replace_fault();
    }
  }
}
