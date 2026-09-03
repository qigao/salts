/**
 * @file test_mmap_group.c
 * @brief Tests for vectorized (group) memory mapping
 */

#include "salts_fs.h"
#include "salts_mmap.h"
#include "tinytest.h"
#include <stdio.h>
#include <string.h>

static char test_file1[256];
static char test_file2[256];
static const char *data1 = "Part 1 of the contiguous mapping. ";
static const char *data2 = "Part 2 follows immediately!";

spec("MMAP Group Tests") {

  before_all() {
    salts_fs_get_tmpdir(test_file1, sizeof(test_file1) - 32);
    strcpy(test_file2, test_file1);
    strcat(test_file1, "/mmap_group_1.bin");
    strcat(test_file2, "/mmap_group_2.bin");

    salts_fs_buf_t buf1 = salts_fs_buf_init((char *)data1, strlen(data1));
    salts_fs_write_file(test_file1, &buf1);
    
    salts_fs_buf_t buf2 = salts_fs_buf_init((char *)data2, strlen(data2));
    salts_fs_write_file(test_file2, &buf2);
  }

  after_all() {
    salts_fs_unlink(test_file1);
    salts_fs_unlink(test_file2);
  }

  it("should map multiple files into a contiguous space") {
    const char *paths[] = { test_file1, test_file2 };
    salts_mmap_group_t group;
    salts_mmap_group_init(&group);

    int err = salts_mmap_group_open(&group, paths, 2, SALTS_MMAP_READ);
    check_equal(err, SALTS_MMAP_OK);
    check_not_null(group.data);
    check_greater(group.total_size, strlen(data1) + strlen(data2));
    check_equal(group.count, 2);

    // Verify first part
    check_equal(memcmp(group.data, data1, strlen(data1)), 0);

    // Verify second part is reachable at the correct offset
    // mappings[0].mapped_length is the offset where the 2nd file starts
    size_t offset = group.mappings[0].mapped_length;
    check_equal(memcmp((char *)group.data + offset, data2, strlen(data2)), 0);

    salts_mmap_group_close(&group);
  }

  it("should allow writing across contiguous mappings") {
    const char *paths[] = { test_file1, test_file2 };
    salts_mmap_group_t group;
    salts_mmap_group_init(&group);

    int err = salts_mmap_group_open(&group, paths, 2, SALTS_MMAP_WRITE);
    check_equal(err, SALTS_MMAP_OK);

    char *ptr = (char *)group.data;
    ptr[0] = 'Z'; // Modify part 1
    
    size_t offset = group.mappings[0].mapped_length;
    ((char *)group.data)[offset] = 'Q'; // Modify part 2

    salts_mmap_group_close(&group);

    // Verify persistence
    salts_fs_buf_t b1, b2;
    int err1 = salts_fs_read_file(test_file1, &b1);
    int err2 = salts_fs_read_file(test_file2, &b2);
    check_equal(err1, 0);
    check_equal(err2, 0);
    
    check_equal(b1.base[0], 'Z');
    check_equal(b2.base[0], 'Q');

    salts_fs_buf_free(&b1);
    salts_fs_buf_free(&b2);
  }
}
