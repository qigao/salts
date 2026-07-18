/**
 * @file test_turbo_fs.c
 * @brief TurboUtils FS unit tests - zero libuv, pure POSIX/Win32 backend
 */
#include "tinytest.h"
#include "turbo_coro.h"
#include "turbo_fs.h"
#include "turbo_thread.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifndef EOVERFLOW
  #define EOVERFLOW ERANGE
#endif

/* ── shared test state ────────────────────────────────────────────────────── */

static char g_file[1024];
static char g_dir[1024];
static char g_file2[1024]; /* rename target */
static char g_link[1024];  /* symlink path */
static const char g_async_write_file[] = "turbo_fs_async_writer.txt";
static const char g_async_read_file[] = "turbo_fs_async_reader.txt";

typedef struct {
  turbo_fs_async_t *req;
  int wait_result;
  int took_result;
  turbo_fs_buf_t output;
} async_coro_ctx_t;

static void async_read_wait_coro(coro_t *co, void *arg) {
  UNUSED(co);
  async_coro_ctx_t *ctx = (async_coro_ctx_t *)arg;
  ctx->wait_result = turbo_fs_async_wait(ctx->req);
  ctx->took_result = turbo_fs_async_take_buf(ctx->req, &ctx->output);
}

spec("Turbo FS Tests") {

  before_all() {
    strncpy(g_file,  "turbo_fs_test.txt",    sizeof(g_file));
    strncpy(g_dir,   "turbo_fs_test_dir",    sizeof(g_dir));
    strncpy(g_file2, "turbo_fs_renamed.txt", sizeof(g_file2));
    strncpy(g_link,  "turbo_fs_link.txt",    sizeof(g_link));
    /* clean slate */
    turbo_fs_unlink(g_link);
    turbo_fs_unlink(g_file);
    turbo_fs_unlink(g_file2);
    turbo_fs_rmdir(g_dir);
  }

  after_all() {
    turbo_fs_unlink(g_link);
    turbo_fs_unlink(g_file);
    turbo_fs_unlink(g_file2);
    turbo_fs_rmdir(g_dir);
    turbo_fs_unlink(g_async_write_file);
    turbo_fs_unlink(g_async_read_file);
  }

  /* ── Bulk read / write ──────────────────────────────────────────────────── */

  describe("Bulk read/write") {
    it("write_file + read_file round-trips data") {
      const char *data = "Hello, TurboUtils FS!";
      turbo_fs_buf_t wb = turbo_fs_buf_init((char *)data, strlen(data));

      check_int_eq(turbo_fs_write_file(g_file, &wb), 0);

      turbo_fs_buf_t rb = {0};
      check_int_eq(turbo_fs_read_file(g_file, &rb), 0);
      check_size_eq(rb.len, strlen(data));
      check_str_eq(rb.base, data);
      turbo_fs_buf_free(&rb);
    }

    it("read_file on missing path returns negative errno") {
      turbo_fs_buf_t rb = {0};
      int ret = turbo_fs_read_file("__nonexistent__.txt", &rb);
      check_int_lt(ret, 0);
    }

    it("write_file with NULL buf returns -EINVAL") {
      check_int_eq(turbo_fs_write_file(g_file, NULL), -EINVAL);
    }
  }

  /* ── stat ───────────────────────────────────────────────────────────────── */

  describe("stat") {
    it("stat on written file reports correct size and is_file") {
      const char *data = "stat me";
      turbo_fs_buf_t wb = turbo_fs_buf_init((char *)data, strlen(data));
      check_int_eq(turbo_fs_write_file(g_file, &wb), 0);

      turbo_fs_stat_t st = {0};
      check_int_eq(turbo_fs_stat(g_file, &st), 0);
      check_size_eq((size_t)st.size, strlen(data));
      check_int_eq((int)st.is_file, 1);
      check_int_eq((int)st.is_directory, 0);
    }

    it("stat on missing path returns negative errno") {
      turbo_fs_stat_t st = {0};
      check_int_lt(turbo_fs_stat("__ghost__", &st), 0);
    }

    it("chmod and access expose basic permission controls") {
      turbo_fs_buf_t wb = turbo_fs_buf_init((char *)"perm", 4);
      check_int_eq(turbo_fs_write_file(g_file, &wb), 0);

      check_int_eq(turbo_fs_access(g_file, TURBO_FS_ACCESS_EXISTS), 0);
      check_int_eq(turbo_fs_access(g_file, TURBO_FS_ACCESS_READ), 0);
      check_int_eq(turbo_fs_chmod(g_file, 0444), 0);
      check_int_eq(turbo_fs_access(g_file, TURBO_FS_ACCESS_READ), 0);
      check_int_eq(turbo_fs_chmod(g_file, 0644), 0);
    }
  }

  describe("symlink") {
    it("creates and inspects symlinks when the platform permits it") {
      turbo_fs_unlink(g_link);
      turbo_fs_buf_t wb = turbo_fs_buf_init((char *)"linked", 6);
      check_int_eq(turbo_fs_write_file(g_file, &wb), 0);

      int rc = turbo_fs_symlink(g_file, g_link, 0);
      if (rc == 0) {
        turbo_fs_stat_t st = {0};
        check_int_eq(turbo_fs_lstat(g_link, &st), 0);
        check_int_eq((int)st.is_symlink, 1);

        char target[1024] = {0};
        int n = turbo_fs_readlink(g_link, target, sizeof(target));
        check_int_gt(n, 0);
#ifndef _WIN32
        check_str_eq(target, g_file);
#endif

        check_int_eq(turbo_fs_unlink(g_link), 0);
      } else {
        check_int_lt(rc, 0);
      }
    }
  }

  /* ── mkdir / rmdir / unlink ─────────────────────────────────────────────── */

  describe("mkdir / rmdir / unlink") {
    it("mkdir creates a directory, stat sees it as directory") {
      check_int_eq(turbo_fs_mkdir(g_dir, 0755), 0);

      turbo_fs_stat_t st = {0};
      check_int_eq(turbo_fs_stat(g_dir, &st), 0);
      check_int_eq((int)st.is_directory, 1);
    }

    it("rmdir on empty dir succeeds") {
      turbo_fs_mkdir(g_dir, 0755);
      check_int_eq(turbo_fs_rmdir(g_dir), 0);
    }

    it("unlink removes a file") {
      turbo_fs_buf_t wb = turbo_fs_buf_init((char *)"x", 1);
      check_int_eq(turbo_fs_write_file(g_file, &wb), 0);
      check_int_eq(turbo_fs_unlink(g_file), 0);

      turbo_fs_stat_t st = {0};
      check_int_lt(turbo_fs_stat(g_file, &st), 0);
    }

    it("unlink rejects directories and leaves them intact") {
      turbo_fs_rmdir(g_dir);
      check_int_eq(turbo_fs_mkdir(g_dir, 0755), 0);
      check_int_lt(turbo_fs_unlink(g_dir), 0);

      turbo_fs_stat_t st = {0};
      check_int_eq(turbo_fs_stat(g_dir, &st), 0);
      check_int_eq((int)st.is_directory, 1);
      check_int_eq(turbo_fs_rmdir(g_dir), 0);
    }
  }

  describe("directory enumeration") {
    it("enumerates child names and available types") {
      char file_path[1024];
      char child_dir_path[1024];
      check_int_eq(turbo_fs_path_join(file_path, sizeof(file_path), g_dir, "child.txt"), 0);
      check_int_eq(turbo_fs_path_join(child_dir_path, sizeof(child_dir_path), g_dir, "nested"), 0);

      turbo_fs_unlink(file_path);
      turbo_fs_rmdir(child_dir_path);
      turbo_fs_rmdir(g_dir);
      check_int_eq(turbo_fs_mkdir(g_dir, 0755), 0);
      turbo_fs_buf_t wb = turbo_fs_buf_init((char *)"x", 1);
      check_int_eq(turbo_fs_write_file(file_path, &wb), 0);
      check_int_eq(turbo_fs_mkdir(child_dir_path, 0755), 0);

      turbo_fs_dir_t *dir = NULL;
      int open_rc = turbo_fs_opendir(g_dir, &dir);
      check_int_eq(open_rc, 0);
      if (open_rc == 0) {
        bool saw_file = false;
        bool saw_directory = false;
        int count = 0;
        int read_rc;
        turbo_fs_dirent_t entry;
        while ((read_rc = turbo_fs_readdir(dir, &entry)) > 0) {
          check_str_ne(entry.name, ".");
          check_str_ne(entry.name, "..");
          if (strcmp(entry.name, "child.txt") == 0) {
            saw_file = true;
            check_true(entry.type == TURBO_FS_DIRENT_FILE ||
                       entry.type == TURBO_FS_DIRENT_UNKNOWN);
          } else if (strcmp(entry.name, "nested") == 0) {
            saw_directory = true;
            check_true(entry.type == TURBO_FS_DIRENT_DIRECTORY ||
                       entry.type == TURBO_FS_DIRENT_UNKNOWN);
          }
          count++;
        }
        check_int_eq(read_rc, 0);
        check_int_eq(count, 2);
        check_true(saw_file);
        check_true(saw_directory);
        check_int_eq(turbo_fs_closedir(dir), 0);
      }

      check_int_eq(turbo_fs_unlink(file_path), 0);
      check_int_eq(turbo_fs_rmdir(child_dir_path), 0);
      check_int_eq(turbo_fs_rmdir(g_dir), 0);
    }

    it("reports end immediately for an empty directory") {
      turbo_fs_rmdir(g_dir);
      check_int_eq(turbo_fs_mkdir(g_dir, 0755), 0);

      turbo_fs_dir_t *dir = NULL;
      int open_rc = turbo_fs_opendir(g_dir, &dir);
      check_int_eq(open_rc, 0);
      if (open_rc == 0) {
        turbo_fs_dirent_t entry;
        check_int_eq(turbo_fs_readdir(dir, &entry), 0);
        check_int_eq(turbo_fs_closedir(dir), 0);
      }
      check_int_eq(turbo_fs_rmdir(g_dir), 0);
    }

    it("rejects invalid arguments and missing paths") {
      turbo_fs_dir_t *dir = NULL;
      turbo_fs_dirent_t entry;
      check_int_eq(turbo_fs_opendir(NULL, &dir), -EINVAL);
      check_null(dir);
      check_int_eq(turbo_fs_opendir(g_dir, NULL), -EINVAL);
      check_int_eq(turbo_fs_readdir(NULL, &entry), -EINVAL);
      check_int_eq(turbo_fs_closedir(NULL), -EINVAL);
      check_int_eq(turbo_fs_opendir("__missing_directory__", &dir), -ENOENT);
      check_null(dir);
    }
  }

  /* ── rename ─────────────────────────────────────────────────────────────── */

  describe("rename") {
    it("rename moves file to new path") {
      turbo_fs_buf_t wb = turbo_fs_buf_init((char *)"move", 4);
      check_int_eq(turbo_fs_write_file(g_file, &wb), 0);
      check_int_eq(turbo_fs_rename(g_file, g_file2), 0);

      turbo_fs_stat_t st = {0};
      check_int_lt(turbo_fs_stat(g_file, &st),  0); /* old gone */
      check_int_eq(turbo_fs_stat(g_file2, &st), 0); /* new exists */
    }

    it("rename replaces an existing destination file") {
      turbo_fs_buf_t source = turbo_fs_buf_init((char *)"replacement", 11);
      turbo_fs_buf_t destination = turbo_fs_buf_init((char *)"stale", 5);
      turbo_fs_buf_t actual = {0};

      check_int_eq(turbo_fs_write_file(g_file, &source), 0);
      check_int_eq(turbo_fs_write_file(g_file2, &destination), 0);
      check_int_eq(turbo_fs_rename(g_file, g_file2), 0);
      check_int_lt(turbo_fs_access(g_file, TURBO_FS_ACCESS_EXISTS), 0);
      check_int_eq(turbo_fs_read_file(g_file2, &actual), 0);
      check_size_eq(actual.len, source.len);
      check_mem_eq(actual.base, source.base, source.len);
      turbo_fs_buf_free(&actual);
    }
  }

  /* ── Streaming open/read/write/close ────────────────────────────────────── */

  describe("Streaming operations") {
    it("open/write/close then open/read/close round-trips") {
      const char *data = "Stream me!";
      size_t len = strlen(data);

      turbo_file_t fd = turbo_fs_open(g_file,
          TURBO_FS_O_WRONLY | TURBO_FS_O_CREAT | TURBO_FS_O_TRUNC, 0644);
      check_int_ne(fd, TURBO_INVALID_FILE);
      check_int_eq(turbo_fs_write(fd, data, len), (int)len);
      check_int_eq(turbo_fs_close(fd), 0);

      fd = turbo_fs_open(g_file, TURBO_FS_O_RDONLY, 0);
      check_int_ne(fd, TURBO_INVALID_FILE);

      char buf[64] = {0};
      check_int_eq(turbo_fs_read(fd, buf, sizeof(buf)), (int)len);
      check_str_eq(buf, data);
      check_int_eq(turbo_fs_close(fd), 0);
    }

    it("open on missing file without O_CREAT returns TURBO_INVALID_FILE") {
      turbo_file_t fd = turbo_fs_open("__never__", TURBO_FS_O_RDONLY, 0);
      check_int_eq(fd, TURBO_INVALID_FILE);
    }

    it("read/write on TURBO_INVALID_FILE return -EINVAL") {
      char buf[8] = {0};
      check_int_eq(turbo_fs_read(TURBO_INVALID_FILE, buf, 8),  -EINVAL);
      check_int_eq(turbo_fs_write(TURBO_INVALID_FILE, buf, 8), -EINVAL);
    }
  }

  /* ── pread / pwrite ─────────────────────────────────────────────────────── */

  describe("pread / pwrite") {
    it("pwrite at offset then pread retrieves correct slice") {
      /* create a known file: "AAAAABBBBB" */
      turbo_file_t fd = turbo_fs_open(g_file,
          TURBO_FS_O_RDWR | TURBO_FS_O_CREAT | TURBO_FS_O_TRUNC, 0644);
      check_int_ne(fd, TURBO_INVALID_FILE);

      check_int_eq(turbo_fs_pwrite(fd, "AAAAA", 5, 0), 5);
      check_int_eq(turbo_fs_pwrite(fd, "BBBBB", 5, 5), 5);

      char buf[6] = {0};
      check_int_eq(turbo_fs_pread(fd, buf, 5, 5), 5);
      check_str_eq(buf, "BBBBB");

      check_int_eq(turbo_fs_close(fd), 0);
    }

    it("pread and pwrite preserve the current file position") {
      turbo_file_t fd = turbo_fs_open(g_file,
          TURBO_FS_O_RDWR | TURBO_FS_O_CREAT | TURBO_FS_O_TRUNC, 0644);
      check_int_ne(fd, TURBO_INVALID_FILE);
      check_int_eq(turbo_fs_write(fd, "abcdef", 6), 6);
      check_int_eq((int)turbo_fs_seek(fd, 3, SEEK_SET), 3);

      char ch = 0;
      check_int_eq(turbo_fs_pread(fd, &ch, 1, 0), 1);
      check_int_eq(ch, 'a');
      check_int_eq((int)turbo_fs_tell(fd), 3);

      check_int_eq(turbo_fs_pwrite(fd, "Z", 1, 1), 1);
      check_int_eq((int)turbo_fs_tell(fd), 3);
      check_int_eq(turbo_fs_close(fd), 0);

      turbo_fs_buf_t rb = {0};
      check_int_eq(turbo_fs_read_file(g_file, &rb), 0);
      check_str_eq(rb.base, "aZcdef");
      turbo_fs_buf_free(&rb);
    }
  }

  describe("Async file operations") {
    it("submits whole-file read/write requests to a thread pool") {
      const char *writer_data = "Async write payload from turbo_fs";
      const char *reader_data = "Async source data ready for reader";
      turbo_fs_async_t *write_req = NULL;
      turbo_fs_async_t *read_req = NULL;
      turbo_fs_buf_t read_output = {0};
      turbo_fs_buf_t writer_input = turbo_fs_buf_init((char *)writer_data, strlen(writer_data));
      turbo_fs_buf_t reader_input = turbo_fs_buf_init((char *)reader_data, strlen(reader_data));
      turbo_threadpool_t *pool = turbo_threadpool_create(2);
      check_true(pool != NULL);

      check_int_eq(turbo_fs_write_file(g_async_read_file, &reader_input), 0);
      check_int_eq(turbo_fs_write_file_async(pool, g_async_write_file, &writer_input,
                                             &write_req), 0);
      check_int_eq(turbo_fs_read_file_async(pool, g_async_read_file, &read_req), 0);

      check_int_eq(turbo_fs_async_wait(write_req), 0);
      check_int_eq(turbo_fs_async_wait(read_req), 0);
      check_int_eq(turbo_fs_async_take_buf(read_req, &read_output), 0);
      check_size_eq(read_output.len, strlen(reader_data));
      check_str_eq(read_output.base, reader_data);

      turbo_fs_stat_t wrote_file_stat = {0};
      check_int_eq(turbo_fs_stat(g_async_write_file, &wrote_file_stat), 0);
      check_size_eq((size_t)wrote_file_stat.size, strlen(writer_data));

      turbo_fs_buf_free(&read_output);
      turbo_fs_async_destroy(read_req);
      turbo_fs_async_destroy(write_req);
      check_int_eq(turbo_fs_unlink(g_async_write_file), 0);
      check_int_eq(turbo_fs_unlink(g_async_read_file), 0);
      turbo_threadpool_destroy(pool);
    }

    it("can wait for async reads from a coroutine") {
      const char *reader_data = "Async coroutine reader payload";
      turbo_fs_async_t *read_req = NULL;
      turbo_fs_buf_t reader_input = turbo_fs_buf_init((char *)reader_data, strlen(reader_data));
      turbo_threadpool_t *pool = turbo_threadpool_create(1);
      async_coro_ctx_t ctx = {0};
      coro_t *co = NULL;

      check_true(pool != NULL);
      check_int_eq(turbo_fs_write_file(g_async_read_file, &reader_input), 0);
      check_int_eq(turbo_fs_read_file_async(pool, g_async_read_file, &read_req), 0);

      ctx.req = read_req;
      co = coro_create(async_read_wait_coro, &ctx, NULL);
      check_not_null(co);
      while (coro_alive(co)) {
        check_int_eq(coro_resume(co), 0);
      }

      check_int_eq(ctx.wait_result, 0);
      check_int_eq(ctx.took_result, 0);
      check_size_eq(ctx.output.len, strlen(reader_data));
      check_str_eq(ctx.output.base, reader_data);

      turbo_fs_buf_free(&ctx.output);
      coro_destroy(co);
      turbo_fs_async_destroy(read_req);
      check_int_eq(turbo_fs_unlink(g_async_read_file), 0);
      turbo_threadpool_destroy(pool);
    }
  }

  describe("streaming bounds") {
    it("rejects lengths that cannot be represented by the int return type") {
      turbo_file_t fd = turbo_fs_open(g_file,
          TURBO_FS_O_RDWR | TURBO_FS_O_CREAT | TURBO_FS_O_TRUNC, 0644);
      check_int_ne(fd, TURBO_INVALID_FILE);

      char byte = 'x';
      size_t too_large = (size_t)INT_MAX + 1u;
      check_int_eq(turbo_fs_write(fd, &byte, too_large), -EOVERFLOW);
      check_int_eq(turbo_fs_pwrite(fd, &byte, too_large, 0), -EOVERFLOW);
      check_int_eq(turbo_fs_read(fd, &byte, too_large), -EOVERFLOW);
      check_int_eq(turbo_fs_pread(fd, &byte, too_large, 0), -EOVERFLOW);

      check_int_eq(turbo_fs_close(fd), 0);
    }
  }

  describe("file locks") {
    it("locks and unlocks a byte range") {
      turbo_fs_buf_t wb = turbo_fs_buf_init((char *)"lock-data", 9);
      check_int_eq(turbo_fs_write_file(g_file, &wb), 0);

      turbo_file_t fd = turbo_fs_open(g_file, TURBO_FS_O_RDWR, 0);
      check_int_ne(fd, TURBO_INVALID_FILE);
      check_int_eq(turbo_fs_lock(fd, TURBO_FS_LOCK_EXCLUSIVE, 0, 0), 0);
      check_int_eq(turbo_fs_unlock(fd, 0, 0), 0);
      check_int_eq(turbo_fs_close(fd), 0);
    }

    it("rejects invalid lock arguments") {
      turbo_file_t fd = turbo_fs_open(g_file,
          TURBO_FS_O_RDWR | TURBO_FS_O_CREAT | TURBO_FS_O_TRUNC, 0644);
      check_int_ne(fd, TURBO_INVALID_FILE);
      check_int_eq(turbo_fs_lock(fd, TURBO_FS_LOCK_SHARED | TURBO_FS_LOCK_EXCLUSIVE, 0, 1),
                   -EINVAL);
      check_int_eq(turbo_fs_lock(fd, 0, 0, 1), -EINVAL);
      check_int_eq(turbo_fs_lock(fd, TURBO_FS_LOCK_SHARED, -1, 1), -EINVAL);
      check_int_eq(turbo_fs_close(fd), 0);
    }
  }

  /* ── seek / tell ────────────────────────────────────────────────────────── */

  describe("seek / tell") {
    it("seek to beginning, tell reports 0") {
      turbo_fs_buf_t wb = turbo_fs_buf_init((char *)"0123456789", 10);
      check_int_eq(turbo_fs_write_file(g_file, &wb), 0);

      turbo_file_t fd = turbo_fs_open(g_file, TURBO_FS_O_RDONLY, 0);
      check_int_ne(fd, TURBO_INVALID_FILE);

      check_int_eq((int)turbo_fs_seek(fd, 5, SEEK_SET), 5);
      check_int_eq((int)turbo_fs_tell(fd), 5);

      check_int_eq((int)turbo_fs_seek(fd, 0, SEEK_SET), 0);
      check_int_eq((int)turbo_fs_tell(fd), 0);

      turbo_fs_close(fd);
    }
  }

  /* ── ftruncate ──────────────────────────────────────────────────────────── */

  describe("ftruncate") {
    it("truncate shortens file to specified length") {
      turbo_fs_buf_t wb = turbo_fs_buf_init((char *)"Hello, World!", 13);
      check_int_eq(turbo_fs_write_file(g_file, &wb), 0);

      turbo_file_t fd = turbo_fs_open(g_file, TURBO_FS_O_RDWR, 0);
      check_int_ne(fd, TURBO_INVALID_FILE);
      check_int_eq(turbo_fs_ftruncate(fd, 5), 0);
      turbo_fs_close(fd);

      turbo_fs_stat_t st = {0};
      check_int_eq(turbo_fs_stat(g_file, &st), 0);
      check_size_eq((size_t)st.size, 5);
    }
  }

  /* ── fsync ──────────────────────────────────────────────────────────────── */

  describe("fsync") {
    it("fsync on valid fd returns 0") {
      turbo_file_t fd = turbo_fs_open(g_file,
          TURBO_FS_O_WRONLY | TURBO_FS_O_CREAT | TURBO_FS_O_TRUNC, 0644);
      check_int_ne(fd, TURBO_INVALID_FILE);
      turbo_fs_write(fd, "sync", 4);
      check_int_eq(turbo_fs_fsync(fd), 0);
      turbo_fs_close(fd);
    }
  }

  /* ── tmpdir ─────────────────────────────────────────────────────────────── */

  describe("tmpdir") {
    it("get_tmpdir fills buffer with non-empty path") {
      char tmp[512] = {0};
      check_int_eq(turbo_fs_get_tmpdir(tmp, sizeof(tmp)), 0);
      check_int_gt((int)strlen(tmp), 0);
    }

    it("get_tmpdir with zero-size buffer returns negative") {
      char tmp[4];
      check_int_lt(turbo_fs_get_tmpdir(tmp, 0), 0);
    }
  }

  /* ── path utilities ─────────────────────────────────────────────────────── */

  describe("Path utilities") {
    it("path_is_absolute detects absolute paths") {
#ifdef _WIN32
      check_int_eq((int)turbo_fs_path_is_absolute("C:\\foo"), 1);
      check_int_eq((int)turbo_fs_path_is_absolute("\\\\srv\\share"), 1);
#else
      check_int_eq((int)turbo_fs_path_is_absolute("/usr/bin"), 1);
#endif
      check_int_eq((int)turbo_fs_path_is_absolute("relative/path"), 0);
      check_int_eq((int)turbo_fs_path_is_absolute(""), 0);
    }

    it("path_join produces correct joined path") {
      char result[256];
#ifdef _WIN32
      check_int_eq(turbo_fs_path_join(result, sizeof(result), "C:\\foo", "bar.txt"), 0);
      check_str_eq(result, "C:\\foo\\bar.txt");
#else
      check_int_eq(turbo_fs_path_join(result, sizeof(result), "/foo", "bar.txt"), 0);
      check_str_eq(result, "/foo/bar.txt");
#endif
    }

    it("path_join with already-terminated base does not double-slash") {
      char result[256];
#ifdef _WIN32
      check_int_eq(turbo_fs_path_join(result, sizeof(result), "C:\\foo\\", "bar.txt"), 0);
      check_str_eq(result, "C:\\foo\\bar.txt");
#else
      check_int_eq(turbo_fs_path_join(result, sizeof(result), "/foo/", "bar.txt"), 0);
      check_str_eq(result, "/foo/bar.txt");
#endif
    }

    it("path_join buffer too small returns -ENAMETOOLONG") {
      char result[4];
      check_int_eq(turbo_fs_path_join(result, sizeof(result), "/very/long", "path.txt"),
                   -ENAMETOOLONG);
    }

    it("path_dirname extracts directory component") {
      char dir[256];
#ifdef _WIN32
      check_int_eq(turbo_fs_path_dirname("C:\\foo\\bar.txt", dir, sizeof(dir)), 0);
      check_str_eq(dir, "C:\\foo");
#else
      check_int_eq(turbo_fs_path_dirname("/foo/bar.txt", dir, sizeof(dir)), 0);
      check_str_eq(dir, "/foo");
#endif
    }

    it("path_dirname with no separator returns dot") {
      char dir[256];
      check_int_eq(turbo_fs_path_dirname("file.txt", dir, sizeof(dir)), 0);
      check_str_eq(dir, ".");
    }

    it("path_basename extracts filename component") {
      char base[256];
#ifdef _WIN32
      check_int_eq(turbo_fs_path_basename("C:\\foo\\bar.txt", base, sizeof(base)), 0);
#else
      check_int_eq(turbo_fs_path_basename("/foo/bar.txt", base, sizeof(base)), 0);
#endif
      check_str_eq(base, "bar.txt");
    }

    it("path_basename with no separator returns original") {
      char base[256];
      check_int_eq(turbo_fs_path_basename("file.txt", base, sizeof(base)), 0);
      check_str_eq(base, "file.txt");
    }
  }

}
