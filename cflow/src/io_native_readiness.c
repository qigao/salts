#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "io_native_internal.h"

#include <turbo/error_codes.h>
#include <turbo/readiness.h>
#include <turbo/thread.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef enum cflow_readiness_record_phase {
    CFLOW_READINESS_RECORD_FREE = 0,
    CFLOW_READINESS_RECORD_NOTIFIED,
    CFLOW_READINESS_RECORD_PROCESSING,
    CFLOW_READINESS_RECORD_WAITING,
    CFLOW_READINESS_RECORD_CLOSING
} cflow_readiness_record_phase;

#if !defined(MSG_DONTWAIT) || !defined(MSG_NOSIGNAL)
#error "native readiness socket operations require per-call nonblocking and no-SIGPIPE flags"
#endif

typedef struct cflow_readiness_impl cflow_readiness_impl;

typedef struct cflow_readiness_record {
    cflow_readiness_record_phase phase;
    turbo_readiness_registration registration;
    cflow_io_request_id request_id;
    cflow_readiness_impl *owner;
    cflow_io_actor *actor;
    cflow_io_native_operation *operation;
    cflow_io_completion completion;
    int duplicated_fd;
    int readiness_status;
    bool cancel_requested;
} cflow_readiness_record;

struct cflow_readiness_impl {
    cflow_io_native_impl base;
    turbo_mutex_t gate;
    turbo_cond_t changed;
    turbo_thread_t worker;
    turbo_readiness_reactor reactor;
    cflow_readiness_record *records;
    size_t request_capacity;
    size_t active_requests;
    uint64_t submitted;
    uint64_t completed;
    uint64_t cancelled;
    uint64_t rejected_full;
    uint64_t stale_native_completions;
    uint64_t native_submit_errors;
    uint64_t native_cancel_errors;
    bool admission_open;
    bool worker_running;
    bool stopping;
    bool shutdown_complete;
};

static void readiness_counter_increment(uint64_t *counter) {
    if (*counter != UINT64_MAX)
        ++*counter;
}

static cflow_readiness_record *readiness_find_free_locked(
    cflow_readiness_impl *impl) {
    size_t index;
    for (index = 0u; index < impl->request_capacity; ++index) {
        if (impl->records[index].phase == CFLOW_READINESS_RECORD_FREE)
            return &impl->records[index];
    }
    return NULL;
}

static cflow_readiness_record *readiness_find_request_locked(
    cflow_readiness_impl *impl, cflow_io_request_id request_id) {
    size_t index;
    for (index = 0u; index < impl->request_capacity; ++index) {
        if (impl->records[index].phase != CFLOW_READINESS_RECORD_FREE &&
            impl->records[index].request_id == request_id)
            return &impl->records[index];
    }
    return NULL;
}

static cflow_readiness_record *readiness_find_work_locked(
    cflow_readiness_impl *impl) {
    size_t index;
    for (index = 0u; index < impl->request_capacity; ++index) {
        if (impl->records[index].phase == CFLOW_READINESS_RECORD_NOTIFIED ||
            impl->records[index].phase == CFLOW_READINESS_RECORD_CLOSING)
            return &impl->records[index];
    }
    return NULL;
}

static int readiness_duplicate_socket(int fd) {
    int duplicate;
    do {
        duplicate = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    } while (duplicate < 0 && errno == EINTR);
    return duplicate < 0 ? -errno : duplicate;
}

static void readiness_native_callback(void *user,
                                      turbo_readiness_events events,
                                      int status) {
    cflow_readiness_record *record = (cflow_readiness_record *)user;
    cflow_readiness_impl *impl;
    (void)events;
    if (record == NULL || record->owner == NULL)
        return;
    impl = record->owner;
    turbo_mutex_lock(&impl->gate);
    if (record->phase == CFLOW_READINESS_RECORD_WAITING) {
        record->readiness_status = status;
        record->phase = CFLOW_READINESS_RECORD_NOTIFIED;
        turbo_cond_signal(&impl->changed);
    } else {
        readiness_counter_increment(&impl->stale_native_completions);
    }
    turbo_mutex_unlock(&impl->gate);
}

static int readiness_attempt(cflow_readiness_record *record,
                             size_t *bytes) {
    cflow_io_native_operation *operation = record->operation;
    ssize_t result;
    do {
        switch (operation->kind) {
            case CFLOW_IO_NATIVE_TCP_RECV:
                result = recv(record->duplicated_fd, operation->buffer,
                              operation->length, MSG_DONTWAIT);
                break;
            case CFLOW_IO_NATIVE_TCP_SEND:
                result = send(record->duplicated_fd, operation->buffer,
                              operation->length,
                              MSG_DONTWAIT | MSG_NOSIGNAL
                              );
                break;
            case CFLOW_IO_NATIVE_UDP_RECV_FROM: {
                socklen_t address_length =
                    (socklen_t)operation->address_capacity;
                result = recvfrom(record->duplicated_fd, operation->buffer,
                                  operation->length, MSG_DONTWAIT,
                                  (struct sockaddr *)operation->address,
                                  &address_length);
                if (result >= 0)
                    operation->address_length = (size_t)address_length;
                break;
            }
            case CFLOW_IO_NATIVE_UDP_SEND_TO:
                result = sendto(record->duplicated_fd, operation->buffer,
                                operation->length,
                                MSG_DONTWAIT | MSG_NOSIGNAL,
                                (const struct sockaddr *)operation->address,
                                (socklen_t)operation->address_length);
                break;
            default:
                return TURBO_EINVAL;
        }
    } while (result < 0 && errno == EINTR);
    if (result >= 0) {
        *bytes = (size_t)result;
        return TURBO_OK;
    }
    return -errno;
}

static bool readiness_would_block(int status) {
    return status == -EAGAIN || status == -EWOULDBLOCK;
}

static turbo_readiness_events readiness_interest(
    const cflow_io_native_operation *operation) {
    return operation->kind == CFLOW_IO_NATIVE_TCP_RECV ||
                   operation->kind == CFLOW_IO_NATIVE_UDP_RECV_FROM
               ? TURBO_READINESS_EVENT_READ
               : TURBO_READINESS_EVENT_WRITE;
}

static void readiness_prepare_completion(cflow_readiness_record *record,
                                         int status,
                                         size_t bytes) {
    if (record->cancel_requested) {
        record->completion = (cflow_io_completion){
            CFLOW_IO_COMPLETION_CANCELLED, 0u, TURBO_OK};
    } else if (status != TURBO_OK) {
        record->completion = (cflow_io_completion){
            CFLOW_IO_COMPLETION_FAILED, 0u, status};
    } else if (record->operation->kind == CFLOW_IO_NATIVE_TCP_RECV &&
               bytes == 0u) {
        record->completion = (cflow_io_completion){
            CFLOW_IO_COMPLETION_EOF, 0u, TURBO_OK};
    } else {
        record->completion = (cflow_io_completion){
            CFLOW_IO_COMPLETION_OK, bytes, TURBO_OK};
    }
    record->phase = CFLOW_READINESS_RECORD_CLOSING;
}

static void readiness_close_and_complete(cflow_readiness_impl *impl,
                                         cflow_readiness_record *record) {
    cflow_io_actor *actor;
    cflow_io_request_id request_id;
    cflow_io_completion completion;
    int close_status = turbo_readiness_close(&record->registration);
    if (close_status != TURBO_OK) {
        turbo_mutex_lock(&impl->gate);
        readiness_counter_increment(&impl->native_cancel_errors);
        record->phase = CFLOW_READINESS_RECORD_CLOSING;
        turbo_cond_signal(&impl->changed);
        turbo_mutex_unlock(&impl->gate);
        turbo_thread_yield();
        return;
    }
    (void)close(record->duplicated_fd);

    turbo_mutex_lock(&impl->gate);
    actor = record->actor;
    request_id = record->request_id;
    completion = record->completion;
    if (completion.kind == CFLOW_IO_COMPLETION_CANCELLED)
        readiness_counter_increment(&impl->cancelled);
    readiness_counter_increment(&impl->completed);
    --impl->active_requests;
    memset(record, 0, sizeof(*record));
    record->duplicated_fd = -1;
    turbo_mutex_unlock(&impl->gate);
    (void)cflow_io_actor_complete(actor, request_id, &completion);
}

static void readiness_process_record(cflow_readiness_impl *impl,
                                     cflow_readiness_record *record) {
    size_t bytes = 0u;
    int status;
    bool cancelled;

    turbo_mutex_lock(&impl->gate);
    if (record->phase == CFLOW_READINESS_RECORD_CLOSING) {
        turbo_mutex_unlock(&impl->gate);
        readiness_close_and_complete(impl, record);
        return;
    }
    record->phase = CFLOW_READINESS_RECORD_PROCESSING;
    cancelled = record->cancel_requested;
    status = record->readiness_status;
    record->readiness_status = TURBO_OK;
    turbo_mutex_unlock(&impl->gate);

    if (!cancelled && status == TURBO_OK)
        status = readiness_attempt(record, &bytes);

    turbo_mutex_lock(&impl->gate);
    cancelled = record->cancel_requested;
    if (cancelled || (status != TURBO_OK && !readiness_would_block(status))) {
        readiness_prepare_completion(record, status, bytes);
        turbo_mutex_unlock(&impl->gate);
        readiness_close_and_complete(impl, record);
        return;
    }
    if (status == TURBO_OK) {
        readiness_prepare_completion(record, TURBO_OK, bytes);
        turbo_mutex_unlock(&impl->gate);
        readiness_close_and_complete(impl, record);
        return;
    }
    record->phase = CFLOW_READINESS_RECORD_WAITING;
    turbo_mutex_unlock(&impl->gate);

    status = turbo_readiness_arm(&record->registration,
                                 readiness_interest(record->operation),
                                 readiness_native_callback, record);
    if (status != TURBO_OK) {
        turbo_mutex_lock(&impl->gate);
        readiness_prepare_completion(record, status, 0u);
        turbo_mutex_unlock(&impl->gate);
        readiness_close_and_complete(impl, record);
    }
}

static void readiness_worker(void *user) {
    cflow_readiness_impl *impl = (cflow_readiness_impl *)user;
    for (;;) {
        cflow_readiness_record *record;
        turbo_mutex_lock(&impl->gate);
        while ((record = readiness_find_work_locked(impl)) == NULL &&
               !impl->stopping)
            turbo_cond_wait(&impl->changed, &impl->gate);
        if (record == NULL && impl->stopping) {
            impl->worker_running = false;
            turbo_mutex_unlock(&impl->gate);
            return;
        }
        turbo_mutex_unlock(&impl->gate);
        readiness_process_record(impl, record);
    }
}

static int readiness_submit(cflow_io_native_impl *base,
                            cflow_io_actor *actor,
                            cflow_io_request_id request_id,
                            cflow_io_native_operation *operation) {
    cflow_readiness_impl *impl = (cflow_readiness_impl *)base;
    cflow_readiness_record *record;
    int duplicate;
    int status;

    if (operation->socket > (uintptr_t)INT_MAX)
        return TURBO_EINVAL;
    duplicate = readiness_duplicate_socket((int)operation->socket);
    if (duplicate < 0)
        return duplicate;

    turbo_mutex_lock(&impl->gate);
    if (!impl->admission_open) {
        turbo_mutex_unlock(&impl->gate);
        (void)close(duplicate);
        return TURBO_ESHUTDOWN;
    }
    record = readiness_find_free_locked(impl);
    if (record == NULL) {
        readiness_counter_increment(&impl->rejected_full);
        turbo_mutex_unlock(&impl->gate);
        (void)close(duplicate);
        return TURBO_EBUSY;
    }
    record->phase = CFLOW_READINESS_RECORD_PROCESSING;
    record->request_id = request_id;
    record->owner = impl;
    record->actor = actor;
    record->operation = operation;
    record->duplicated_fd = duplicate;
    ++impl->active_requests;
    turbo_mutex_unlock(&impl->gate);

    status = turbo_readiness_register(&impl->reactor, duplicate,
                                      &record->registration);
    if (status != TURBO_OK) {
        turbo_mutex_lock(&impl->gate);
        memset(record, 0, sizeof(*record));
        record->duplicated_fd = -1;
        --impl->active_requests;
        readiness_counter_increment(&impl->native_submit_errors);
        turbo_mutex_unlock(&impl->gate);
        (void)close(duplicate);
        return status;
    }

    turbo_mutex_lock(&impl->gate);
    record->phase = CFLOW_READINESS_RECORD_NOTIFIED;
    readiness_counter_increment(&impl->submitted);
    turbo_cond_signal(&impl->changed);
    turbo_mutex_unlock(&impl->gate);
    return TURBO_OK;
}

static int readiness_cancel(cflow_io_native_impl *base,
                            cflow_io_request_id request_id) {
    cflow_readiness_impl *impl = (cflow_readiness_impl *)base;
    cflow_readiness_record *record;
    turbo_mutex_lock(&impl->gate);
    record = readiness_find_request_locked(impl, request_id);
    if (record == NULL) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_ENOENT;
    }
    record->cancel_requested = true;
    if (record->phase == CFLOW_READINESS_RECORD_WAITING) {
        record->phase = CFLOW_READINESS_RECORD_NOTIFIED;
        turbo_cond_signal(&impl->changed);
    }
    turbo_mutex_unlock(&impl->gate);
    return TURBO_OK;
}

static bool readiness_get_stats(const cflow_io_native_impl *base,
                                cflow_io_native_backend_stats *out) {
    cflow_readiness_impl *impl = (cflow_readiness_impl *)base;
    turbo_mutex_lock(&impl->gate);
    *out = (cflow_io_native_backend_stats){
        impl->request_capacity,
        impl->active_requests,
        impl->submitted,
        impl->completed,
        impl->cancelled,
        impl->rejected_full,
        impl->stale_native_completions,
        impl->native_submit_errors,
        impl->native_cancel_errors,
        impl->admission_open,
        impl->worker_running,
        impl->shutdown_complete};
    turbo_mutex_unlock(&impl->gate);
    return true;
}

static int readiness_forget_socket(cflow_io_native_impl *base,
                                   uintptr_t closed_socket) {
    cflow_readiness_impl *impl = (cflow_readiness_impl *)base;
    (void)closed_socket;
    turbo_mutex_lock(&impl->gate);
    if (impl->active_requests != 0u) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EBUSY;
    }
    turbo_mutex_unlock(&impl->gate);
    return TURBO_OK;
}

static int readiness_shutdown(cflow_io_native_impl *base) {
    cflow_readiness_impl *impl = (cflow_readiness_impl *)base;
    int status;
    turbo_mutex_lock(&impl->gate);
    if (impl->shutdown_complete) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EALREADY;
    }
    impl->admission_open = false;
    if (impl->active_requests != 0u) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EBUSY;
    }
    impl->stopping = true;
    turbo_cond_signal(&impl->changed);
    turbo_mutex_unlock(&impl->gate);

    status = turbo_thread_join(&impl->worker);
    if (status != TURBO_OK)
        return status;
    turbo_thread_destroy(&impl->worker);
    status = turbo_readiness_reactor_shutdown(&impl->reactor);
    if (status != TURBO_OK && status != TURBO_EALREADY)
        return status;
    status = turbo_readiness_reactor_destroy(&impl->reactor);
    if (status != TURBO_OK)
        return status;
    turbo_mutex_lock(&impl->gate);
    impl->shutdown_complete = true;
    turbo_mutex_unlock(&impl->gate);
    return TURBO_OK;
}

static int readiness_destroy(cflow_io_native_impl *base) {
    cflow_readiness_impl *impl = (cflow_readiness_impl *)base;
    turbo_mutex_lock(&impl->gate);
    if (!impl->shutdown_complete) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EBUSY;
    }
    turbo_mutex_unlock(&impl->gate);
    turbo_cond_destroy(&impl->changed);
    turbo_mutex_destroy(&impl->gate);
    free(impl->records);
    free(impl);
    return TURBO_OK;
}

static const cflow_io_native_impl_ops readiness_ops = {
    readiness_submit, readiness_cancel, readiness_get_stats,
    readiness_forget_socket, readiness_shutdown, readiness_destroy};

int cflow_io_native_readiness_init(
    cflow_io_native_backend *backend,
    const cflow_io_native_backend_config *config) {
    cflow_readiness_impl *impl;
    turbo_readiness_config reactor_config = {
        config->request_capacity, config->completion_batch_capacity};
    int status;

#if defined(CFLOW_HAS_NATIVE_EPOLL)
    if (config->kind != CFLOW_IO_NATIVE_EPOLL)
        return TURBO_ENOTSUP;
#elif defined(CFLOW_HAS_NATIVE_KQUEUE)
    if (config->kind != CFLOW_IO_NATIVE_KQUEUE)
        return TURBO_ENOTSUP;
#endif
    if (config->request_capacity > SIZE_MAX / sizeof(cflow_readiness_record))
        return TURBO_ERANGE;
    impl = (cflow_readiness_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return TURBO_ENOMEM;
    impl->records = (cflow_readiness_record *)calloc(
        config->request_capacity, sizeof(*impl->records));
    if (impl->records == NULL) {
        free(impl);
        return TURBO_ENOMEM;
    }
    for (size_t index = 0u; index < config->request_capacity; ++index)
        impl->records[index].duplicated_fd = -1;
    impl->base.ops = &readiness_ops;
    impl->base.kind = config->kind;
    impl->request_capacity = config->request_capacity;
    impl->admission_open = true;
    turbo_mutex_init(&impl->gate);
    turbo_cond_init(&impl->changed);
    if (impl->gate == NULL || impl->changed == NULL) {
        turbo_cond_destroy(&impl->changed);
        turbo_mutex_destroy(&impl->gate);
        free(impl->records);
        free(impl);
        return TURBO_ENOMEM;
    }
    status = turbo_readiness_reactor_init(&impl->reactor, &reactor_config);
    if (status != TURBO_OK) {
        turbo_cond_destroy(&impl->changed);
        turbo_mutex_destroy(&impl->gate);
        free(impl->records);
        free(impl);
        return status;
    }
    status = turbo_thread_create(&impl->worker, readiness_worker, impl);
    if (status != TURBO_OK) {
        (void)turbo_readiness_reactor_shutdown(&impl->reactor);
        (void)turbo_readiness_reactor_destroy(&impl->reactor);
        turbo_cond_destroy(&impl->changed);
        turbo_mutex_destroy(&impl->gate);
        free(impl->records);
        free(impl);
        return status;
    }
    impl->worker_running = true;
    backend->impl = impl;
    return TURBO_OK;
}
