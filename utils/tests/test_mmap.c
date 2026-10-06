/**
 * @file test_mmap.c
 * @brief Tests for cmeta_mmap memory-mapped file I/O
 */

#include "cmeta_fs.h"
#include "cmeta_mmap.h"
#include "tinytest.h"
#include <stdio.h>
#include <string.h>

static char test_file_path[256];
static const char *test_data =
    "Hello, memory-mapped world! This is test data for mmap.";

spec("MMAP Tests") {

  before_all() {
    // Create test file
    cmeta_fs_get_tmpdir(test_file_path, sizeof(test_file_path) - 32);
    strcat(test_file_path, "/cmeta_mmap_test.txt");

    cmeta_fs_buf_t buf = cmeta_fs_buf_init((char *)test_data, strlen(test_data));
    int err = cmeta_fs_write_file(test_file_path, &buf);
    check_equal(err, 0);
  }

  after_all() { cmeta_fs_unlink(test_file_path); }

  it("should initialize mmap structure correctly") {
    cmeta_mmap_t mmap;
    cmeta_mmap_init(&mmap);

    check_null(mmap.data);
    check_equal(mmap.length, 0);
    check(!mmap.is_mapped);
  }

  it("should return a valid page size") {
    size_t page_size = cmeta_mmap_page_size();
    check_greater(page_size, 0);
    // Page size should be power of 2
    check_equal(page_size & (page_size - 1), 0);
  }

  it("should open file for reading") {
    cmeta_mmap_t mmap;
    cmeta_mmap_init(&mmap);

    int err = cmeta_mmap_open(&mmap, test_file_path, SALTS_MMAP_READ);
    check_equal(err, SALTS_MMAP_OK);
    check(cmeta_mmap_is_open(&mmap));
    check_not_null(cmeta_mmap_data(&mmap));
    check_equal(cmeta_mmap_size(&mmap), strlen(test_data));

    // Verify content
    check_equal(memcmp(test_data, cmeta_mmap_data(&mmap), strlen(test_data)),
                 0);

    cmeta_mmap_close(&mmap);
    check(!cmeta_mmap_is_open(&mmap));
  }

  it("should open file for writing and persist changes") {
    cmeta_mmap_t mmap;
    cmeta_mmap_init(&mmap);

    int err = cmeta_mmap_open(&mmap, test_file_path, SALTS_MMAP_WRITE);
    check_equal(err, SALTS_MMAP_OK);

    // Modify first byte
    char *data = (char *)cmeta_mmap_data(&mmap);
    char original = data[0];
    data[0] = 'X';

    // Sync to disk
    err = cmeta_mmap_sync(&mmap, false);
    check_equal(err, SALTS_MMAP_OK);

    cmeta_mmap_close(&mmap);

    // Verify change persisted
    cmeta_fs_buf_t buf;
    err = cmeta_fs_read_file(test_file_path, &buf);
    check_equal(err, 0);
    check_equal(buf.base[0], 'X');
    cmeta_fs_buf_free(&buf);

    // Restore original
    cmeta_mmap_init(&mmap);
    err = cmeta_mmap_open(&mmap, test_file_path, SALTS_MMAP_WRITE);
    check_equal(err, SALTS_MMAP_OK);
    ((char *)cmeta_mmap_data(&mmap))[0] = original;
    err = cmeta_mmap_sync(&mmap, false);
    check_equal(err, SALTS_MMAP_OK);
    cmeta_mmap_close(&mmap);
  }

  it("should open a specific range of a file") {
    cmeta_mmap_t mmap;
    cmeta_mmap_init(&mmap);

    // Map starting at offset 7 ("memory-mapped world...")
    int err =
        cmeta_mmap_open_range(&mmap, test_file_path, 7, 15, SALTS_MMAP_READ);
    check_equal(err, SALTS_MMAP_OK);
    check_equal(cmeta_mmap_size(&mmap), 15);

    // Verify content at offset
    check_equal(memcmp("memory-mapped w", cmeta_mmap_data(&mmap), 15), 0);

    cmeta_mmap_close(&mmap);
  }

  it("should provide byte accessors") {
    cmeta_mmap_t mmap;
    cmeta_mmap_init(&mmap);

    int err = cmeta_mmap_open(&mmap, test_file_path, SALTS_MMAP_READ);
    check_equal(err, SALTS_MMAP_OK);

    // Test get accessor
    check_equal(cmeta_mmap_get(&mmap, 0), 'H');
    check_equal(cmeta_mmap_get(&mmap, 1), 'e');
    check_equal(cmeta_mmap_get(&mmap, 2), 'l');

    cmeta_mmap_close(&mmap);
  }

  it("should fail when file is not found") {
    cmeta_mmap_t mmap;
    cmeta_mmap_init(&mmap);

    int err =
        cmeta_mmap_open(&mmap, "/nonexistent/path/file.txt", SALTS_MMAP_READ);
    check_equal(err, SALTS_MMAP_ENOENT);
    check(!cmeta_mmap_is_open(&mmap));
  }

  it("should fail on double open") {
    cmeta_mmap_t mmap;
    cmeta_mmap_init(&mmap);

    int err = cmeta_mmap_open(&mmap, test_file_path, SALTS_MMAP_READ);
    check_equal(err, SALTS_MMAP_OK);

    // Try to open again without closing
    err = cmeta_mmap_open(&mmap, test_file_path, SALTS_MMAP_READ);
    check_equal(err, SALTS_MMAP_EEXIST);

    cmeta_mmap_close(&mmap);
  }

  it("should return correct error strings") {
    check_equal(cmeta_mmap_strerror(SALTS_MMAP_OK), "Success");
    check_equal(cmeta_mmap_strerror(SALTS_MMAP_EINVAL), "Invalid argument");
    check_equal(cmeta_mmap_strerror(SALTS_MMAP_ENOENT), "File not found");
    check_equal(cmeta_mmap_strerror(SALTS_MMAP_EEMPTY), "File is empty");
  }

  it("should have idempotent close") {
    cmeta_mmap_t mmap;
    cmeta_mmap_init(&mmap);

    int err = cmeta_mmap_open(&mmap, test_file_path, SALTS_MMAP_READ);
    check_equal(err, SALTS_MMAP_OK);

    // Close multiple times should be safe
    cmeta_mmap_close(&mmap);
    cmeta_mmap_close(&mmap);
    cmeta_mmap_close(&mmap);

    check(!cmeta_mmap_is_open(&mmap));
  }

  it("should handle advise hints") {
    cmeta_mmap_t mmap;
    cmeta_mmap_init(&mmap);

    int err = cmeta_mmap_open(&mmap, test_file_path, SALTS_MMAP_READ);
    check_equal(err, SALTS_MMAP_OK);

    // Advise should succeed (or be no-op on Windows)
    err = cmeta_mmap_advise(&mmap, SALTS_MMAP_SEQUENTIAL);
    check_equal(err, SALTS_MMAP_OK);

    err = cmeta_mmap_advise(&mmap, SALTS_MMAP_RANDOM);
    check_equal(err, SALTS_MMAP_OK);

    cmeta_mmap_close(&mmap);
  }
}
