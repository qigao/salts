/**
 * @file fs_example.c
 * @brief Salts FS example - demonstrates all major file I/O operations.
 *
 * Exercises: bulk read/write, streaming, pread/pwrite, seek/tell,
 * ftruncate, rename, mkdir, directory enumeration, stat, and path utilities.
 * No libuv - pure POSIX / Win32 under the hood.
 */
#include "salts_fs.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr, msg)                                    \
  do {                                                      \
    int _r = (int)(expr);                                   \
    if (_r < 0) {                                           \
      fprintf(stderr, "FAIL  %s -> %d (%s)\n",             \
              (msg), _r, strerror(-_r));                    \
      return 1;                                             \
    }                                                       \
    printf("OK    %s\n", (msg));                            \
  } while (0)

static int example_bulk_rw(void) {
  puts("\n--- Bulk Read / Write ---");

  const char *data = "Salts FS: zero-dep, pure-C, cross-platform.";
  salts_fs_buf_t wb = salts_fs_buf_init((char *)data, strlen(data));

  CHECK(salts_fs_write_file("example_out.txt", &wb), "write_file");

  salts_fs_buf_t rb = {0};
  CHECK(salts_fs_read_file("example_out.txt", &rb), "read_file");

  printf("      content: %s\n", rb.base);
  printf("      length : %zu bytes\n", rb.len);
  salts_fs_buf_free(&rb);
  return 0;
}

static int example_stat(void) {
  puts("\n--- Stat ---");

  salts_fs_stat_t st = {0};
  CHECK(salts_fs_stat("example_out.txt", &st), "stat(example_out.txt)");
  printf("      size      : %llu bytes\n", (unsigned long long)st.size);
  printf("      is_file   : %d\n", (int)st.is_file);
  printf("      is_dir    : %d\n", (int)st.is_directory);
  return 0;
}

static int example_mkdir(void) {
  puts("\n--- mkdir / rmdir ---");

  /* ignore error if it already exists */
  salts_fs_mkdir("example_dir", 0755);

  salts_fs_stat_t st = {0};
  CHECK(salts_fs_stat("example_dir", &st), "stat(example_dir)");
  printf("      is_directory: %d\n", (int)st.is_directory);

  CHECK(salts_fs_rmdir("example_dir"), "rmdir(example_dir)");
  return 0;
}

static const char *dirent_type_name(salts_fs_dirent_type_t type) {
  switch (type) {
  case SALTS_FS_DIRENT_FILE: return "file";
  case SALTS_FS_DIRENT_DIRECTORY: return "directory";
  case SALTS_FS_DIRENT_SYMLINK: return "symlink";
  case SALTS_FS_DIRENT_OTHER: return "other";
  default: return "unknown";
  }
}

static int example_directory_enumeration(void) {
  puts("\n--- Directory enumeration ---");

  salts_fs_dir_t *dir = NULL;
  CHECK(salts_fs_opendir(".", &dir), "opendir(.)");

  salts_fs_dirent_t entry;
  int rc;
  while ((rc = salts_fs_readdir(dir, &entry)) > 0) {
    printf("      %-12s %s\n", dirent_type_name(entry.type), entry.name);
  }

  int close_rc = salts_fs_closedir(dir);
  CHECK(rc, "readdir(.)");
  CHECK(close_rc, "closedir(.)");
  return 0;
}

static int example_streaming(void) {
  puts("\n--- Streaming open/write/read/close ---");

  const char *data = "streaming data chunk";
  size_t len = strlen(data);

  salts_file_t fd = salts_fs_open("example_stream.txt",
      SALTS_FS_O_WRONLY | SALTS_FS_O_CREAT | SALTS_FS_O_TRUNC, 0644);
  if (fd == SALTS_INVALID_FILE) {
    fprintf(stderr, "FAIL  open for write\n");
    return 1;
  }
  printf("OK    open for write\n");

  int n = salts_fs_write(fd, data, len);
  if (n != (int)len) {
    fprintf(stderr, "FAIL  write (%d)\n", n);
    salts_fs_close(fd);
    return 1;
  }
  printf("OK    write %d bytes\n", n);
  salts_fs_close(fd);

  fd = salts_fs_open("example_stream.txt", SALTS_FS_O_RDONLY, 0);
  if (fd == SALTS_INVALID_FILE) {
    fprintf(stderr, "FAIL  open for read\n");
    return 1;
  }

  char buf[64] = {0};
  n = salts_fs_read(fd, buf, sizeof(buf) - 1);
  printf("OK    read %d bytes: %s\n", n, buf);
  salts_fs_close(fd);

  salts_fs_unlink("example_stream.txt");
  return 0;
}

static int example_pread_pwrite(void) {
  puts("\n--- pread / pwrite ---");

  salts_file_t fd = salts_fs_open("example_prw.txt",
      SALTS_FS_O_RDWR | SALTS_FS_O_CREAT | SALTS_FS_O_TRUNC, 0644);
  if (fd == SALTS_INVALID_FILE) {
    fprintf(stderr, "FAIL  open\n");
    return 1;
  }

  /* Write two non-overlapping regions */
  int a = salts_fs_pwrite(fd, "HELLO", 5, 0);
  int b = salts_fs_pwrite(fd, "WORLD", 5, 5);
  printf("OK    pwrite HELLO@0 (%d bytes), WORLD@5 (%d bytes)\n", a, b);

  char r0[6] = {0}, r5[6] = {0};
  salts_fs_pread(fd, r0, 5, 0);
  salts_fs_pread(fd, r5, 5, 5);
  printf("OK    pread @0='%s' @5='%s'\n", r0, r5);

  salts_fs_close(fd);
  salts_fs_unlink("example_prw.txt");
  return 0;
}

static int example_seek_tell(void) {
  puts("\n--- seek / tell ---");

  salts_fs_buf_t wb = salts_fs_buf_init((char *)"0123456789", 10);
  salts_fs_write_file("example_seek.txt", &wb);

  salts_file_t fd = salts_fs_open("example_seek.txt", SALTS_FS_O_RDONLY, 0);
  if (fd == SALTS_INVALID_FILE) { return 1; }

  int64_t pos = salts_fs_seek(fd, 5, SEEK_SET);
  int64_t told = salts_fs_tell(fd);
  printf("OK    seek(5) -> %lld, tell -> %lld\n",
         (long long)pos, (long long)told);

  char c[2] = {0};
  salts_fs_read(fd, c, 1);
  printf("OK    char at offset 5 = '%s'\n", c);

  pos = salts_fs_seek(fd, -3, SEEK_END);
  printf("OK    seek(-3, END) -> pos %lld\n", (long long)pos);

  salts_fs_close(fd);
  salts_fs_unlink("example_seek.txt");
  return 0;
}

static int example_ftruncate(void) {
  puts("\n--- ftruncate ---");

  salts_fs_buf_t wb = salts_fs_buf_init((char *)"Hello, World!", 13);
  salts_fs_write_file("example_trunc.txt", &wb);

  salts_file_t fd = salts_fs_open("example_trunc.txt", SALTS_FS_O_RDWR, 0);
  if (fd == SALTS_INVALID_FILE) { return 1; }

  CHECK(salts_fs_ftruncate(fd, 5), "ftruncate to 5 bytes");
  salts_fs_close(fd);

  salts_fs_stat_t st = {0};
  salts_fs_stat("example_trunc.txt", &st);
  printf("      size after truncate: %llu\n", (unsigned long long)st.size);

  salts_fs_unlink("example_trunc.txt");
  return 0;
}

static int example_rename(void) {
  puts("\n--- rename ---");

  salts_fs_buf_t wb = salts_fs_buf_init((char *)"rename me", 9);
  salts_fs_write_file("example_old.txt", &wb);

  CHECK(salts_fs_rename("example_old.txt", "example_new.txt"), "rename");

  salts_fs_stat_t st = {0};
  int exists_old = salts_fs_stat("example_old.txt", &st);
  int exists_new = salts_fs_stat("example_new.txt", &st);
  printf("      old exists: %s  new exists: %s\n",
         exists_old == 0 ? "yes" : "no",
         exists_new == 0 ? "yes" : "no");

  salts_fs_unlink("example_new.txt");
  return 0;
}

static int example_tmpdir(void) {
  puts("\n--- tmpdir ---");

  char tmp[512] = {0};
  CHECK(salts_fs_get_tmpdir(tmp, sizeof(tmp)), "get_tmpdir");
  printf("      tmpdir: %s\n", tmp);
  return 0;
}

static int example_path_utils(void) {
  puts("\n--- Path utilities ---");

#ifdef _WIN32
  const char *abs_path = "C:\\Users\\foo\\bar.txt";
  const char *rel_path = "relative\\path.txt";
#else
  const char *abs_path = "/home/foo/bar.txt";
  const char *rel_path = "relative/path.txt";
#endif

  printf("      is_absolute('%s') = %d\n", abs_path,
         (int)salts_fs_path_is_absolute(abs_path));
  printf("      is_absolute('%s') = %d\n", rel_path,
         (int)salts_fs_path_is_absolute(rel_path));

  char joined[256], dir[256], base[256];

#ifdef _WIN32
  salts_fs_path_join(joined, sizeof(joined), "C:\\projects", "Salts");
#else
  salts_fs_path_join(joined, sizeof(joined), "/projects", "Salts");
#endif
  printf("      join      = %s\n", joined);

  salts_fs_path_dirname(abs_path, dir, sizeof(dir));
  printf("      dirname   = %s\n", dir);

  salts_fs_path_basename(abs_path, base, sizeof(base));
  printf("      basename  = %s\n", base);

  return 0;
}

int main(void) {
  puts("=== Salts FS Example ===");

  int rc = 0;
  rc |= example_bulk_rw();
  rc |= example_stat();
  rc |= example_mkdir();
  rc |= example_directory_enumeration();
  rc |= example_streaming();
  rc |= example_pread_pwrite();
  rc |= example_seek_tell();
  rc |= example_ftruncate();
  rc |= example_rename();
  rc |= example_tmpdir();
  rc |= example_path_utils();

  /* cleanup */
  salts_fs_unlink("example_out.txt");

  puts(rc == 0 ? "\nAll examples passed." : "\nSome examples FAILED.");
  return rc;
}
