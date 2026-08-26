#include "fs_watch_internal.h"

#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

typedef struct cflow_fs_watch_linux {
    cflow_fs_watch_impl *owner;
    int inotify_fd;
    int stop_pipe[2];
    int watch_descriptor;
    turbo_thread_t thread;
    unsigned char *buffer;
    size_t buffer_capacity;
    char *rename_old_path;
    size_t path_capacity;
    uint32_t rename_cookie;
    bool rename_pending;
} cflow_fs_watch_linux;

static void watch_linux_loss(cflow_fs_watch_linux *backend) {
    backend->rename_pending = false;
    backend->rename_cookie = 0u;
    cflow_fs_watch_publish_loss(backend->owner);
}

static void watch_linux_process(cflow_fs_watch_linux *backend,
                                ssize_t bytes) {
    size_t offset = 0u;
    while (offset + sizeof(struct inotify_event) <= (size_t)bytes) {
        const struct inotify_event *native =
            (const struct inotify_event *)(backend->buffer + offset);
        if ((size_t)native->len >
            (size_t)bytes - offset - sizeof(*native)) {
            watch_linux_loss(backend);
            return;
        }
        cflow_fs_watch_entry_type entry_type =
            (native->mask & IN_ISDIR) != 0u
                ? CFLOW_FS_WATCH_ENTRY_DIRECTORY
                : CFLOW_FS_WATCH_ENTRY_FILE;
        const char *path = native->len != 0u ? native->name : NULL;
        if ((native->mask & IN_Q_OVERFLOW) != 0u) {
            watch_linux_loss(backend);
        } else if ((native->mask & (IN_DELETE_SELF | IN_MOVE_SELF |
                                    IN_UNMOUNT)) != 0u) {
            (void)cflow_fs_watch_publish(
                backend->owner, CFLOW_FS_WATCH_ROOT_CHANGED,
                NULL, NULL, CFLOW_FS_WATCH_ENTRY_DIRECTORY);
        } else if ((native->mask & IN_MOVED_FROM) != 0u) {
            size_t length = path != NULL ? strlen(path) : 0u;
            if (path == NULL || length >= backend->path_capacity ||
                backend->rename_pending) {
                watch_linux_loss(backend);
            } else {
                memcpy(backend->rename_old_path, path, length + 1u);
                backend->rename_cookie = native->cookie;
                backend->rename_pending = true;
            }
        } else if ((native->mask & IN_MOVED_TO) != 0u) {
            if (!backend->rename_pending || native->cookie == 0u ||
                native->cookie != backend->rename_cookie) {
                watch_linux_loss(backend);
            } else {
                (void)cflow_fs_watch_publish(
                    backend->owner, CFLOW_FS_WATCH_RENAMED, path,
                    backend->rename_old_path, entry_type);
                backend->rename_pending = false;
                backend->rename_cookie = 0u;
            }
        } else if ((native->mask & IN_CREATE) != 0u) {
            (void)cflow_fs_watch_publish(
                backend->owner, CFLOW_FS_WATCH_CREATED, path, NULL,
                entry_type);
        } else if ((native->mask & IN_DELETE) != 0u) {
            (void)cflow_fs_watch_publish(
                backend->owner, CFLOW_FS_WATCH_REMOVED, path, NULL,
                entry_type);
        } else if ((native->mask & IN_ATTRIB) != 0u) {
            (void)cflow_fs_watch_publish(
                backend->owner, CFLOW_FS_WATCH_ATTRIBUTES, path, NULL,
                entry_type);
        } else if ((native->mask & (IN_MODIFY | IN_CLOSE_WRITE)) != 0u) {
            (void)cflow_fs_watch_publish(
                backend->owner, CFLOW_FS_WATCH_MODIFIED, path, NULL,
                entry_type);
        }
        offset += sizeof(*native) + native->len;
    }
    if (offset != (size_t)bytes)
        watch_linux_loss(backend);
}

static void watch_linux_thread(void *user) {
    cflow_fs_watch_linux *backend = (cflow_fs_watch_linux *)user;
    struct pollfd descriptors[2] = {
        {backend->stop_pipe[0], POLLIN, 0},
        {backend->inotify_fd, POLLIN, 0},
    };
    while (!cflow_fs_watch_close_requested(backend->owner)) {
        int ready;
        do {
            ready = poll(descriptors, 2u, -1);
        } while (ready < 0 && errno == EINTR);
        if (ready <= 0)
            break;
        if ((descriptors[0].revents & POLLIN) != 0)
            break;
        if ((descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            if (!cflow_fs_watch_close_requested(backend->owner))
                (void)cflow_fs_watch_publish(
                    backend->owner, CFLOW_FS_WATCH_ROOT_CHANGED,
                    NULL, NULL, CFLOW_FS_WATCH_ENTRY_DIRECTORY);
            break;
        }
        if ((descriptors[1].revents & POLLIN) != 0) {
            ssize_t bytes;
            do {
                bytes = read(backend->inotify_fd, backend->buffer,
                             backend->buffer_capacity);
            } while (bytes < 0 && errno == EINTR);
            if (bytes > 0)
                watch_linux_process(backend, bytes);
            else if (bytes < 0 && errno != EAGAIN)
                break;
        }
    }
    cflow_fs_watch_backend_mark_done(backend->owner);
}

int cflow_fs_watch_backend_open(cflow_fs_watch_impl *impl,
                                const char *path,
                                const cflow_fs_watch_config *config) {
    cflow_fs_watch_linux *backend;
    uint32_t mask;
    int status;
    if (config->recursive)
        return TURBO_ENOTSUP;
    backend = (cflow_fs_watch_linux *)calloc(1u, sizeof(*backend));
    if (backend == NULL)
        return TURBO_ENOMEM;
    backend->inotify_fd = -1;
    backend->stop_pipe[0] = -1;
    backend->stop_pipe[1] = -1;
    backend->inotify_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (backend->inotify_fd < 0) {
        status = -errno;
        free(backend);
        return status;
    }
    if (pipe(backend->stop_pipe) != 0) {
        status = -errno;
        close(backend->inotify_fd);
        free(backend);
        return status;
    }
    backend->buffer = (unsigned char *)malloc(config->native_buffer_capacity);
    backend->rename_old_path = (char *)malloc(config->path_capacity);
    if (backend->buffer == NULL || backend->rename_old_path == NULL) {
        close(backend->stop_pipe[1]);
        close(backend->stop_pipe[0]);
        close(backend->inotify_fd);
        free(backend->rename_old_path);
        free(backend->buffer);
        free(backend);
        return TURBO_ENOMEM;
    }
    if (fcntl(backend->stop_pipe[0], F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(backend->stop_pipe[1], F_SETFD, FD_CLOEXEC) != 0) {
        status = -errno;
        close(backend->stop_pipe[1]);
        close(backend->stop_pipe[0]);
        close(backend->inotify_fd);
        free(backend->rename_old_path);
        free(backend->buffer);
        free(backend);
        return status;
    }
    mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB |
           IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF |
           IN_UNMOUNT | IN_ONLYDIR;
    backend->watch_descriptor =
        inotify_add_watch(backend->inotify_fd, path, mask);
    if (backend->watch_descriptor < 0) {
        status = -errno;
        close(backend->stop_pipe[1]);
        close(backend->stop_pipe[0]);
        close(backend->inotify_fd);
        free(backend->rename_old_path);
        free(backend->buffer);
        free(backend);
        return status;
    }
    backend->owner = impl;
    backend->buffer_capacity = config->native_buffer_capacity;
    backend->path_capacity = config->path_capacity;
    cflow_fs_watch_backend_set(impl, backend);
    status = turbo_thread_create(&backend->thread,
                                 watch_linux_thread, backend);
    if (status != TURBO_OK) {
        cflow_fs_watch_backend_set(impl, NULL);
        (void)inotify_rm_watch(backend->inotify_fd,
                               backend->watch_descriptor);
        close(backend->stop_pipe[1]);
        close(backend->stop_pipe[0]);
        close(backend->inotify_fd);
        free(backend->rename_old_path);
        free(backend->buffer);
        free(backend);
        return status;
    }
    return TURBO_OK;
}

void cflow_fs_watch_backend_request_close(cflow_fs_watch_impl *impl) {
    cflow_fs_watch_linux *backend =
        (cflow_fs_watch_linux *)cflow_fs_watch_backend_get(impl);
    const unsigned char stop = 1u;
    if (backend != NULL)
        (void)write(backend->stop_pipe[1], &stop, sizeof(stop));
}

int cflow_fs_watch_backend_destroy(cflow_fs_watch_impl *impl) {
    cflow_fs_watch_linux *backend =
        (cflow_fs_watch_linux *)cflow_fs_watch_backend_get(impl);
    int status = TURBO_OK;
    if (backend == NULL)
        return TURBO_EINVAL;
    if (turbo_thread_join(&backend->thread) != TURBO_OK)
        status = TURBO_EIO;
    if (backend->watch_descriptor >= 0)
        (void)inotify_rm_watch(backend->inotify_fd,
                               backend->watch_descriptor);
    if (close(backend->stop_pipe[1]) != 0 && status == TURBO_OK)
        status = -errno;
    if (close(backend->stop_pipe[0]) != 0 && status == TURBO_OK)
        status = -errno;
    if (close(backend->inotify_fd) != 0 && status == TURBO_OK)
        status = -errno;
    free(backend->rename_old_path);
    free(backend->buffer);
    free(backend);
    cflow_fs_watch_backend_set(impl, NULL);
    return status;
}
