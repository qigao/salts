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

#if !defined(MSG_DONTWAIT) || !defined(MSG_NOSIGNAL)
#error "native readiness socket operations require per-call nonblocking and no-SIGPIPE flags"
#endif

enum {
    CFLOW_READINESS_LANE_READ = 0,
    CFLOW_READINESS_LANE_WRITE = 1,
    CFLOW_READINESS_LANE_COUNT = 2
};

#define CFLOW_READINESS_INDEX_NONE SIZE_MAX

typedef enum cflow_readiness_record_phase {
    CFLOW_READINESS_RECORD_FREE = 0,
    CFLOW_READINESS_RECORD_RESERVED,
    CFLOW_READINESS_RECORD_QUEUED,
    CFLOW_READINESS_RECORD_PROCESSING
} cflow_readiness_record_phase;

typedef struct cflow_readiness_impl cflow_readiness_impl;
typedef struct cflow_readiness_socket_record cflow_readiness_socket_record;
typedef struct cflow_readiness_lane cflow_readiness_lane;

typedef struct cflow_readiness_record {
    cflow_readiness_record_phase phase;
    size_t index;
    size_t next;
    cflow_io_request_id request_id;
    cflow_io_actor *actor;
    cflow_io_native_operation *operation;
    cflow_readiness_socket_record *socket_record;
    cflow_readiness_lane *lane;
    struct sockaddr_storage peer_address;
    socklen_t peer_address_length;
    bool connect_started;
    bool cancel_requested;
} cflow_readiness_record;

struct cflow_readiness_lane {
    cflow_readiness_impl *owner;
    cflow_readiness_socket_record *socket_record;
    turbo_readiness_registration registration;
    size_t head;
    size_t tail;
    int duplicated_fd;
    int terminal_status;
    unsigned kind;
    bool active;
    bool creating;
    bool armed;
    bool arm_pending;
    bool driving;
};

struct cflow_readiness_socket_record {
    uintptr_t socket_identity;
    size_t active_requests;
    cflow_readiness_lane lanes[CFLOW_READINESS_LANE_COUNT];
    bool active;
    bool closing;
};

struct cflow_readiness_impl {
    cflow_io_native_impl base;
    turbo_mutex_t gate;
    turbo_cond_t changed;
    turbo_readiness_reactor reactor;
    cflow_readiness_record *records;
    cflow_readiness_socket_record *sockets;
    size_t request_capacity;
    size_t completion_batch_capacity;
    size_t active_requests;
    uint64_t submitted;
    uint64_t completed;
    uint64_t cancelled;
    uint64_t rejected_full;
    uint64_t stale_native_completions;
    uint64_t native_submit_errors;
    uint64_t native_cancel_errors;
    bool admission_open;
    bool shutdown_inflight;
    bool shutdown_complete;
};

typedef struct cflow_readiness_delivery {
    cflow_io_actor *actor;
    cflow_io_request_id request_id;
    cflow_io_native_operation *operation;
    cflow_io_completion completion;
    int accepted_fd;
    bool valid;
} cflow_readiness_delivery;

static void readiness_counter_increment(uint64_t *counter) {
    if (*counter != UINT64_MAX)
        ++*counter;
}

static unsigned readiness_lane_kind(
    const cflow_io_native_operation *operation) {
    return operation->kind == CFLOW_IO_NATIVE_TCP_RECV ||
                   operation->kind == CFLOW_IO_NATIVE_UDP_RECV_FROM ||
                   operation->kind == CFLOW_IO_NATIVE_TCP_ACCEPT
               ? CFLOW_READINESS_LANE_READ
               : CFLOW_READINESS_LANE_WRITE;
}

static turbo_readiness_events readiness_lane_interest(
    const cflow_readiness_lane *lane) {
    return lane->kind == CFLOW_READINESS_LANE_READ
               ? TURBO_READINESS_EVENT_READ
               : TURBO_READINESS_EVENT_WRITE;
}

static void readiness_record_reset(cflow_readiness_record *record) {
    const size_t index = record->index;
    memset(record, 0, sizeof(*record));
    record->index = index;
    record->next = CFLOW_READINESS_INDEX_NONE;
}

static void readiness_lane_reset(cflow_readiness_lane *lane) {
    cflow_readiness_impl *owner = lane->owner;
    cflow_readiness_socket_record *socket_record = lane->socket_record;
    const unsigned kind = lane->kind;
    memset(lane, 0, sizeof(*lane));
    lane->owner = owner;
    lane->socket_record = socket_record;
    lane->kind = kind;
    lane->head = CFLOW_READINESS_INDEX_NONE;
    lane->tail = CFLOW_READINESS_INDEX_NONE;
    lane->duplicated_fd = -1;
}

static void readiness_socket_reset(cflow_readiness_impl *impl,
                                   cflow_readiness_socket_record *socket_record) {
    memset(socket_record, 0, sizeof(*socket_record));
    socket_record->socket_identity = UINTPTR_MAX;
    for (unsigned kind = 0u; kind < CFLOW_READINESS_LANE_COUNT; ++kind) {
        socket_record->lanes[kind].owner = impl;
        socket_record->lanes[kind].socket_record = socket_record;
        socket_record->lanes[kind].kind = kind;
        socket_record->lanes[kind].head = CFLOW_READINESS_INDEX_NONE;
        socket_record->lanes[kind].tail = CFLOW_READINESS_INDEX_NONE;
        socket_record->lanes[kind].duplicated_fd = -1;
    }
}

static cflow_readiness_record *readiness_find_free_locked(
    cflow_readiness_impl *impl) {
    for (size_t index = 0u; index < impl->request_capacity; ++index) {
        if (impl->records[index].phase == CFLOW_READINESS_RECORD_FREE)
            return &impl->records[index];
    }
    return NULL;
}

static cflow_readiness_record *readiness_find_request_locked(
    cflow_readiness_impl *impl, cflow_io_request_id request_id) {
    for (size_t index = 0u; index < impl->request_capacity; ++index) {
        if (impl->records[index].phase != CFLOW_READINESS_RECORD_FREE &&
            impl->records[index].request_id == request_id)
            return &impl->records[index];
    }
    return NULL;
}

static cflow_readiness_socket_record *readiness_find_socket_locked(
    cflow_readiness_impl *impl, uintptr_t socket_identity) {
    for (size_t index = 0u; index < impl->request_capacity; ++index) {
        if (impl->sockets[index].active &&
            impl->sockets[index].socket_identity == socket_identity)
            return &impl->sockets[index];
    }
    return NULL;
}

static cflow_readiness_socket_record *readiness_find_free_socket_locked(
    cflow_readiness_impl *impl) {
    for (size_t index = 0u; index < impl->request_capacity; ++index) {
        if (!impl->sockets[index].active)
            return &impl->sockets[index];
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

static int readiness_normalize_accepted_fd(int accepted_fd) {
    int status_flags;
    int descriptor_flags;
    do {
        status_flags = fcntl(accepted_fd, F_GETFL);
    } while (status_flags < 0 && errno == EINTR);
    if (status_flags < 0)
        return -errno;
    while (fcntl(accepted_fd, F_SETFL,
                 status_flags | O_NONBLOCK) < 0) {
        if (errno != EINTR)
            return -errno;
    }
    do {
        descriptor_flags = fcntl(accepted_fd, F_GETFD);
    } while (descriptor_flags < 0 && errno == EINTR);
    if (descriptor_flags < 0)
        return -errno;
    while (fcntl(accepted_fd, F_SETFD,
                 descriptor_flags | FD_CLOEXEC) < 0) {
        if (errno != EINTR)
            return -errno;
    }
    return TURBO_OK;
}

static int readiness_attempt_connect(cflow_readiness_record *record) {
    cflow_io_native_operation *operation = record->operation;
    const int fd = record->lane->duplicated_fd;
    if (!record->connect_started) {
        int result;
        do {
            result = connect(fd,
                (const struct sockaddr *)operation->address,
                (socklen_t)operation->address_length);
        } while (result < 0 && errno == EINTR);
        if (result == 0)
            return TURBO_OK;
        if (errno != EINPROGRESS && errno != EALREADY &&
            errno != EWOULDBLOCK)
            return -errno;
        record->connect_started = true;
        return -EAGAIN;
    }
    {
        int socket_error = 0;
        socklen_t error_length = (socklen_t)sizeof(socket_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error,
                       &error_length) != 0)
            return -errno;
        if (socket_error == 0)
            return TURBO_OK;
        if (socket_error == EINPROGRESS || socket_error == EALREADY ||
            socket_error == EWOULDBLOCK)
            return -EAGAIN;
        return -socket_error;
    }
}

static int readiness_attempt(cflow_readiness_record *record,
                             size_t *bytes, int *accepted_fd) {
    cflow_io_native_operation *operation = record->operation;
    const int fd = record->lane->duplicated_fd;
    ssize_t result;
    do {
        switch (operation->kind) {
            case CFLOW_IO_NATIVE_TCP_RECV:
                result = recv(fd, operation->buffer, operation->length,
                              MSG_DONTWAIT);
                break;
            case CFLOW_IO_NATIVE_TCP_SEND:
                result = send(fd, operation->buffer, operation->length,
                              MSG_DONTWAIT | MSG_NOSIGNAL);
                break;
            case CFLOW_IO_NATIVE_UDP_RECV_FROM: {
                socklen_t address_length =
                    (socklen_t)operation->address_capacity;
                result = recvfrom(fd, operation->buffer, operation->length,
                                  MSG_DONTWAIT,
                                  (struct sockaddr *)operation->address,
                                  &address_length);
                if (result >= 0)
                    operation->address_length = (size_t)address_length;
                break;
            }
            case CFLOW_IO_NATIVE_UDP_SEND_TO:
                result = sendto(fd, operation->buffer, operation->length,
                                MSG_DONTWAIT | MSG_NOSIGNAL,
                                (const struct sockaddr *)operation->address,
                                (socklen_t)operation->address_length);
                break;
            case CFLOW_IO_NATIVE_TCP_ACCEPT:
                record->peer_address_length =
                    (socklen_t)sizeof(record->peer_address);
                result = accept(
                    fd, (struct sockaddr *)&record->peer_address,
                    &record->peer_address_length);
                if (result >= 0) {
                    const int normalized =
                        readiness_normalize_accepted_fd((int)result);
                    if (normalized != TURBO_OK) {
                        (void)close((int)result);
                        return normalized;
                    }
                    *accepted_fd = (int)result;
                    result = 0;
                }
                break;
            case CFLOW_IO_NATIVE_TCP_CONNECT:
                return readiness_attempt_connect(record);
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

static void readiness_lane_append_locked(
    cflow_readiness_impl *impl, cflow_readiness_lane *lane,
    cflow_readiness_record *record) {
    record->next = CFLOW_READINESS_INDEX_NONE;
    if (lane->tail == CFLOW_READINESS_INDEX_NONE)
        lane->head = record->index;
    else
        impl->records[lane->tail].next = record->index;
    lane->tail = record->index;
    record->phase = CFLOW_READINESS_RECORD_QUEUED;
}

static bool readiness_lane_remove_locked(
    cflow_readiness_impl *impl, cflow_readiness_lane *lane,
    cflow_readiness_record *record) {
    size_t previous = CFLOW_READINESS_INDEX_NONE;
    size_t current = lane->head;
    while (current != CFLOW_READINESS_INDEX_NONE) {
        cflow_readiness_record *candidate = &impl->records[current];
        if (candidate == record) {
            if (previous == CFLOW_READINESS_INDEX_NONE)
                lane->head = candidate->next;
            else
                impl->records[previous].next = candidate->next;
            if (lane->tail == current)
                lane->tail = previous;
            candidate->next = CFLOW_READINESS_INDEX_NONE;
            return true;
        }
        previous = current;
        current = candidate->next;
    }
    return false;
}

static cflow_readiness_record *readiness_find_cancelled_locked(
    cflow_readiness_impl *impl, cflow_readiness_lane *lane) {
    size_t current = lane->head;
    while (current != CFLOW_READINESS_INDEX_NONE) {
        cflow_readiness_record *record = &impl->records[current];
        if (record->cancel_requested &&
            record->phase == CFLOW_READINESS_RECORD_QUEUED)
            return record;
        current = record->next;
    }
    return NULL;
}

static cflow_io_completion readiness_completion_for(
    const cflow_readiness_record *record, int status, size_t bytes) {
    if (record->cancel_requested)
        return (cflow_io_completion){
            CFLOW_IO_COMPLETION_CANCELLED, 0u, TURBO_OK};
    if (status != TURBO_OK)
        return (cflow_io_completion){
            CFLOW_IO_COMPLETION_FAILED, 0u, status};
    if (record->operation->kind == CFLOW_IO_NATIVE_TCP_RECV && bytes == 0u)
        return (cflow_io_completion){
            CFLOW_IO_COMPLETION_EOF, 0u, TURBO_OK};
    return (cflow_io_completion){
        CFLOW_IO_COMPLETION_OK, bytes, TURBO_OK};
}

static cflow_readiness_delivery readiness_finish_record_locked(
    cflow_readiness_impl *impl, cflow_readiness_record *record,
    int status, size_t bytes, int accepted_fd) {
    cflow_readiness_delivery delivery;
    cflow_readiness_socket_record *socket_record = record->socket_record;
    cflow_readiness_lane *lane = record->lane;
    if (record->operation->kind == CFLOW_IO_NATIVE_TCP_ACCEPT &&
        !record->cancel_requested && status == TURBO_OK) {
        if (record->operation->address != NULL) {
            if ((size_t)record->peer_address_length >
                record->operation->address_capacity) {
                status = TURBO_ERANGE;
            } else {
                memcpy(record->operation->address,
                       &record->peer_address,
                       (size_t)record->peer_address_length);
                record->operation->address_length =
                    (size_t)record->peer_address_length;
            }
        }
        if (status == TURBO_OK)
            record->operation->result_socket = (uintptr_t)accepted_fd;
    }
    delivery = (cflow_readiness_delivery){
        record->actor, record->request_id, record->operation,
        readiness_completion_for(record, status, bytes), accepted_fd, true};
    (void)readiness_lane_remove_locked(impl, lane, record);
    if (delivery.completion.kind == CFLOW_IO_COMPLETION_CANCELLED)
        readiness_counter_increment(&impl->cancelled);
    readiness_counter_increment(&impl->completed);
    --impl->active_requests;
    --socket_record->active_requests;
    readiness_record_reset(record);
    return delivery;
}

static void readiness_deliver(cflow_readiness_delivery delivery) {
    cflow_io_complete_status status;
    if (!delivery.valid)
        return;
    status = cflow_io_actor_complete(delivery.actor, delivery.request_id,
                                     &delivery.completion);
    if (delivery.accepted_fd >= 0 &&
        (delivery.completion.kind != CFLOW_IO_COMPLETION_OK ||
         status != CFLOW_IO_COMPLETE_ACCEPTED)) {
        (void)close(delivery.accepted_fd);
        delivery.operation->result_socket =
            CFLOW_IO_NATIVE_INVALID_SOCKET;
        delivery.operation->address_length = 0u;
    }
}

static turbo_readiness_callback_result readiness_drive_lane(
    cflow_readiness_lane *lane, bool from_callback, int status);

static turbo_readiness_callback_result readiness_native_continuation(
    void *user, turbo_readiness_events events, int status) {
    cflow_readiness_lane *lane = (cflow_readiness_lane *)user;
    cflow_readiness_impl *impl;
    (void)events;
    if (lane == NULL || lane->owner == NULL)
        return (turbo_readiness_callback_result){
            TURBO_READINESS_COMPLETE, 0u};
    impl = lane->owner;
    turbo_mutex_lock(&impl->gate);
    if (!lane->active || lane->socket_record->closing) {
        turbo_mutex_unlock(&impl->gate);
        return (turbo_readiness_callback_result){
            TURBO_READINESS_COMPLETE, 0u};
    }
    lane->armed = false;
    lane->arm_pending = false;
    if (lane->driving) {
        if (status != TURBO_OK)
            lane->terminal_status = status;
        else
            readiness_counter_increment(&impl->stale_native_completions);
        turbo_mutex_unlock(&impl->gate);
        return (turbo_readiness_callback_result){
            TURBO_READINESS_COMPLETE, 0u};
    }
    lane->driving = true;
    turbo_mutex_unlock(&impl->gate);
    return readiness_drive_lane(lane, true, status);
}

static turbo_readiness_callback_result readiness_arm_lane(
    cflow_readiness_lane *lane) {
    cflow_readiness_impl *impl = lane->owner;
    int status;

    turbo_mutex_lock(&impl->gate);
    if (!lane->active || lane->head == CFLOW_READINESS_INDEX_NONE ||
        lane->socket_record->closing) {
        lane->driving = false;
        turbo_mutex_unlock(&impl->gate);
        return (turbo_readiness_callback_result){
            TURBO_READINESS_COMPLETE, 0u};
    }
    lane->driving = false;
    lane->arm_pending = true;
    turbo_mutex_unlock(&impl->gate);

    status = turbo_readiness_arm_continuation(
        &lane->registration, readiness_lane_interest(lane),
        readiness_native_continuation, lane);

    turbo_mutex_lock(&impl->gate);
    if (!lane->arm_pending) {
        turbo_mutex_unlock(&impl->gate);
        return (turbo_readiness_callback_result){
            TURBO_READINESS_COMPLETE, 0u};
    }
    lane->arm_pending = false;
    if (status == TURBO_OK || status == TURBO_EALREADY) {
        lane->armed = true;
        turbo_mutex_unlock(&impl->gate);
        return (turbo_readiness_callback_result){
            TURBO_READINESS_COMPLETE, 0u};
    }
    lane->driving = true;
    readiness_counter_increment(&impl->native_submit_errors);
    turbo_mutex_unlock(&impl->gate);
    return readiness_drive_lane(lane, false, status);
}

static turbo_readiness_callback_result readiness_drive_lane(
    cflow_readiness_lane *lane, bool from_callback, int status) {
    cflow_readiness_impl *impl = lane->owner;
    size_t processed = 0u;
    int terminal_status = status;

    for (;;) {
        cflow_readiness_record *record;
        cflow_readiness_delivery delivery = {
            .accepted_fd = -1};
        size_t bytes = 0u;
        int accepted_fd = -1;
        int attempt_status;

        turbo_mutex_lock(&impl->gate);
        if (lane->terminal_status != TURBO_OK) {
            terminal_status = lane->terminal_status;
            lane->terminal_status = TURBO_OK;
        }
        if (!lane->active || lane->socket_record->closing) {
            lane->driving = false;
            turbo_mutex_unlock(&impl->gate);
            return (turbo_readiness_callback_result){
                TURBO_READINESS_COMPLETE, 0u};
        }
        if (lane->head == CFLOW_READINESS_INDEX_NONE) {
            lane->driving = false;
            lane->armed = false;
            turbo_mutex_unlock(&impl->gate);
            return (turbo_readiness_callback_result){
                TURBO_READINESS_COMPLETE, 0u};
        }

        if (terminal_status != TURBO_OK) {
            record = &impl->records[lane->head];
            delivery = readiness_finish_record_locked(
                impl, record, terminal_status, 0u, -1);
            turbo_mutex_unlock(&impl->gate);
            readiness_deliver(delivery);
            continue;
        }

        record = readiness_find_cancelled_locked(impl, lane);
        if (record != NULL) {
            delivery = readiness_finish_record_locked(
                impl, record, TURBO_OK, 0u, -1);
            ++processed;
            turbo_mutex_unlock(&impl->gate);
            readiness_deliver(delivery);
            continue;
        }

        if (processed >= impl->completion_batch_capacity) {
            if (from_callback) {
                lane->driving = false;
                lane->armed = true;
                turbo_mutex_unlock(&impl->gate);
                return (turbo_readiness_callback_result){
                    TURBO_READINESS_REARM,
                    readiness_lane_interest(lane)};
            }
            turbo_mutex_unlock(&impl->gate);
            return readiness_arm_lane(lane);
        }

        record = &impl->records[lane->head];
        record->phase = CFLOW_READINESS_RECORD_PROCESSING;
        turbo_mutex_unlock(&impl->gate);

        attempt_status = readiness_attempt(record, &bytes, &accepted_fd);

        turbo_mutex_lock(&impl->gate);
        if (record->cancel_requested || attempt_status == TURBO_OK ||
            !readiness_would_block(attempt_status)) {
            delivery = readiness_finish_record_locked(
                impl, record, attempt_status, bytes, accepted_fd);
            ++processed;
            turbo_mutex_unlock(&impl->gate);
            readiness_deliver(delivery);
            continue;
        }
        record->phase = CFLOW_READINESS_RECORD_QUEUED;
        if (from_callback) {
            lane->driving = false;
            lane->armed = true;
            turbo_mutex_unlock(&impl->gate);
            return (turbo_readiness_callback_result){
                TURBO_READINESS_REARM,
                readiness_lane_interest(lane)};
        }
        turbo_mutex_unlock(&impl->gate);
        return readiness_arm_lane(lane);
    }
}

static int readiness_ensure_lane(cflow_readiness_lane *lane,
                                 int original_fd) {
    cflow_readiness_impl *impl = lane->owner;
    int duplicate;
    int status;

    turbo_mutex_lock(&impl->gate);
    while (lane->creating)
        turbo_cond_wait(&impl->changed, &impl->gate);
    if (lane->active) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_OK;
    }
    if (lane->socket_record->closing) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EBUSY;
    }
    lane->creating = true;
    turbo_mutex_unlock(&impl->gate);

    duplicate = readiness_duplicate_socket(original_fd);
    status = duplicate < 0 ? duplicate : turbo_readiness_register(
        &impl->reactor, duplicate, &lane->registration);
    if (status != TURBO_OK && duplicate >= 0)
        (void)close(duplicate);

    turbo_mutex_lock(&impl->gate);
    lane->creating = false;
    if (status == TURBO_OK) {
        lane->duplicated_fd = duplicate;
        lane->active = true;
    }
    turbo_cond_broadcast(&impl->changed);
    turbo_mutex_unlock(&impl->gate);
    return status;
}

static int readiness_submit(cflow_io_native_impl *base,
                            cflow_io_actor *actor,
                            cflow_io_request_id request_id,
                            cflow_io_native_operation *operation) {
    cflow_readiness_impl *impl = (cflow_readiness_impl *)base;
    cflow_readiness_record *record;
    cflow_readiness_socket_record *socket_record;
    cflow_readiness_lane *lane;
    bool start_drive = false;
    int status;

    if (operation->socket > (uintptr_t)INT_MAX)
        return TURBO_EINVAL;
    if (operation->kind == CFLOW_IO_NATIVE_TCP_ACCEPT ||
        operation->kind == CFLOW_IO_NATIVE_TCP_CONNECT) {
        int flags;
        do {
            flags = fcntl((int)operation->socket, F_GETFL);
        } while (flags < 0 && errno == EINTR);
        if (flags < 0)
            return -errno;
        if ((flags & O_NONBLOCK) == 0)
            return TURBO_EINVAL;
    }

    turbo_mutex_lock(&impl->gate);
    if (!impl->admission_open) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_ESHUTDOWN;
    }
    record = readiness_find_free_locked(impl);
    if (record == NULL) {
        readiness_counter_increment(&impl->rejected_full);
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EBUSY;
    }
    socket_record = readiness_find_socket_locked(impl, operation->socket);
    if (socket_record == NULL) {
        socket_record = readiness_find_free_socket_locked(impl);
        if (socket_record == NULL) {
            readiness_counter_increment(&impl->rejected_full);
            turbo_mutex_unlock(&impl->gate);
            return TURBO_EBUSY;
        }
        socket_record->socket_identity = operation->socket;
        socket_record->active = true;
    }
    if (socket_record->closing) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EBUSY;
    }
    lane = &socket_record->lanes[readiness_lane_kind(operation)];
    record->phase = CFLOW_READINESS_RECORD_RESERVED;
    record->request_id = request_id;
    record->actor = actor;
    record->operation = operation;
    record->socket_record = socket_record;
    record->lane = lane;
    record->connect_started = false;
    ++impl->active_requests;
    ++socket_record->active_requests;
    turbo_mutex_unlock(&impl->gate);

    status = readiness_ensure_lane(lane, (int)operation->socket);
    if (status != TURBO_OK) {
        turbo_mutex_lock(&impl->gate);
        --impl->active_requests;
        --socket_record->active_requests;
        readiness_record_reset(record);
        if (socket_record->active_requests == 0u &&
            !socket_record->lanes[CFLOW_READINESS_LANE_READ].active &&
            !socket_record->lanes[CFLOW_READINESS_LANE_WRITE].active)
            readiness_socket_reset(impl, socket_record);
        readiness_counter_increment(&impl->native_submit_errors);
        turbo_mutex_unlock(&impl->gate);
        return status;
    }

    turbo_mutex_lock(&impl->gate);
    readiness_lane_append_locked(impl, lane, record);
    readiness_counter_increment(&impl->submitted);
    if (!lane->armed && !lane->arm_pending && !lane->driving) {
        lane->driving = true;
        start_drive = true;
    }
    turbo_mutex_unlock(&impl->gate);

    if (start_drive)
        (void)readiness_drive_lane(lane, false, TURBO_OK);
    return TURBO_OK;
}

static int readiness_cancel(cflow_io_native_impl *base,
                            cflow_io_request_id request_id) {
    cflow_readiness_impl *impl = (cflow_readiness_impl *)base;
    cflow_readiness_record *record;
    cflow_readiness_lane *lane;
    bool start_drive = false;
    bool unarm = false;
    int status = TURBO_OK;

    turbo_mutex_lock(&impl->gate);
    record = readiness_find_request_locked(impl, request_id);
    if (record == NULL) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_ENOENT;
    }
    record->cancel_requested = true;
    lane = record->lane;
    if (record->phase != CFLOW_READINESS_RECORD_PROCESSING &&
        !lane->driving) {
        unarm = lane->armed || lane->arm_pending;
        lane->armed = false;
        lane->arm_pending = false;
        lane->driving = true;
        start_drive = true;
    }
    turbo_mutex_unlock(&impl->gate);

    if (unarm) {
        status = turbo_readiness_unarm(&lane->registration);
        if (status != TURBO_OK && status != TURBO_EALREADY) {
            turbo_mutex_lock(&impl->gate);
            readiness_counter_increment(&impl->native_cancel_errors);
            turbo_mutex_unlock(&impl->gate);
        }
    }
    if (start_drive)
        (void)readiness_drive_lane(lane, false, TURBO_OK);
    return status == TURBO_EALREADY ? TURBO_OK : status;
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
        false,
        impl->shutdown_complete};
    turbo_mutex_unlock(&impl->gate);
    return true;
}

static int readiness_close_lane(cflow_readiness_impl *impl,
                                cflow_readiness_lane *lane) {
    int fd;
    int status;
    turbo_mutex_lock(&impl->gate);
    if (!lane->active) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_OK;
    }
    fd = lane->duplicated_fd;
    turbo_mutex_unlock(&impl->gate);

    status = turbo_readiness_close(&lane->registration);
    if (status != TURBO_OK)
        return status;
    (void)close(fd);

    turbo_mutex_lock(&impl->gate);
    readiness_lane_reset(lane);
    turbo_mutex_unlock(&impl->gate);
    return TURBO_OK;
}

static int readiness_forget_socket(cflow_io_native_impl *base,
                                   uintptr_t closed_socket) {
    cflow_readiness_impl *impl = (cflow_readiness_impl *)base;
    cflow_readiness_socket_record *socket_record;
    int status = TURBO_OK;

    turbo_mutex_lock(&impl->gate);
    socket_record = readiness_find_socket_locked(impl, closed_socket);
    if (socket_record == NULL) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_ENOENT;
    }
    if (socket_record->closing || socket_record->active_requests != 0u) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EBUSY;
    }
    for (unsigned kind = 0u; kind < CFLOW_READINESS_LANE_COUNT; ++kind) {
        cflow_readiness_lane *lane = &socket_record->lanes[kind];
        if (lane->creating || lane->driving || lane->arm_pending) {
            turbo_mutex_unlock(&impl->gate);
            return TURBO_EBUSY;
        }
    }
    socket_record->closing = true;
    turbo_mutex_unlock(&impl->gate);

    for (unsigned kind = 0u; kind < CFLOW_READINESS_LANE_COUNT; ++kind) {
        status = readiness_close_lane(impl, &socket_record->lanes[kind]);
        if (status != TURBO_OK)
            break;
    }

    turbo_mutex_lock(&impl->gate);
    if (status == TURBO_OK)
        readiness_socket_reset(impl, socket_record);
    else
        socket_record->closing = false;
    turbo_mutex_unlock(&impl->gate);
    return status;
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
    if (impl->shutdown_inflight) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EBUSY;
    }
    impl->shutdown_inflight = true;
    for (size_t index = 0u; index < impl->request_capacity; ++index) {
        cflow_readiness_socket_record *socket_record = &impl->sockets[index];
        if (!socket_record->active)
            continue;
        socket_record->closing = true;
    }
    turbo_mutex_unlock(&impl->gate);

    for (size_t index = 0u; index < impl->request_capacity; ++index) {
        cflow_readiness_socket_record *socket_record = &impl->sockets[index];
        if (!socket_record->active)
            continue;
        for (unsigned kind = 0u; kind < CFLOW_READINESS_LANE_COUNT; ++kind) {
            status = readiness_close_lane(impl, &socket_record->lanes[kind]);
            if (status != TURBO_OK) {
                turbo_mutex_lock(&impl->gate);
                impl->shutdown_inflight = false;
                turbo_mutex_unlock(&impl->gate);
                return status;
            }
        }
    }

    status = turbo_readiness_reactor_shutdown(&impl->reactor);
    if (status != TURBO_OK && status != TURBO_EALREADY) {
        turbo_mutex_lock(&impl->gate);
        impl->shutdown_inflight = false;
        turbo_mutex_unlock(&impl->gate);
        return status;
    }
    status = turbo_readiness_reactor_destroy(&impl->reactor);
    if (status != TURBO_OK) {
        turbo_mutex_lock(&impl->gate);
        impl->shutdown_inflight = false;
        turbo_mutex_unlock(&impl->gate);
        return status;
    }
    turbo_mutex_lock(&impl->gate);
    impl->shutdown_inflight = false;
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
    free(impl->sockets);
    free(impl->records);
    free(impl);
    return TURBO_OK;
}

static const cflow_io_native_impl_ops readiness_ops = {
    readiness_submit, NULL, readiness_cancel, readiness_get_stats,
    readiness_forget_socket, NULL, readiness_shutdown, readiness_destroy};

int cflow_io_native_readiness_init(
    cflow_io_native_backend *backend,
    const cflow_io_native_backend_config *config) {
    cflow_readiness_impl *impl;
    turbo_readiness_config reactor_config;
    turbo_readiness_backend_kind reactor_kind;
    size_t reactor_capacity;
    int status;

    switch (config->kind) {
#if defined(CFLOW_HAS_NATIVE_EPOLL)
        case CFLOW_IO_NATIVE_EPOLL:
            reactor_kind = TURBO_READINESS_BACKEND_EPOLL;
            break;
#endif
#if defined(CFLOW_HAS_NATIVE_KQUEUE)
        case CFLOW_IO_NATIVE_KQUEUE:
            reactor_kind = TURBO_READINESS_BACKEND_KQUEUE;
            break;
#endif
#if defined(CFLOW_HAS_NATIVE_POLL)
        case CFLOW_IO_NATIVE_POLL:
            reactor_kind = TURBO_READINESS_BACKEND_POLL;
            break;
#endif
        default:
            return TURBO_ENOTSUP;
    }
    if (config->request_capacity > ((size_t)UINT32_MAX - 1u) /
                                       CFLOW_READINESS_LANE_COUNT ||
        config->request_capacity > SIZE_MAX / sizeof(cflow_readiness_record) ||
        config->request_capacity >
            SIZE_MAX / sizeof(cflow_readiness_socket_record))
        return TURBO_ERANGE;
    reactor_capacity = config->request_capacity *
                       CFLOW_READINESS_LANE_COUNT;
    reactor_config = (turbo_readiness_config){
        reactor_capacity, config->completion_batch_capacity};

    impl = (cflow_readiness_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return TURBO_ENOMEM;
    impl->records = (cflow_readiness_record *)calloc(
        config->request_capacity, sizeof(*impl->records));
    impl->sockets = (cflow_readiness_socket_record *)calloc(
        config->request_capacity, sizeof(*impl->sockets));
    if (impl->records == NULL || impl->sockets == NULL) {
        free(impl->sockets);
        free(impl->records);
        free(impl);
        return TURBO_ENOMEM;
    }
    impl->base.ops = &readiness_ops;
    impl->base.kind = config->kind;
    impl->request_capacity = config->request_capacity;
    impl->completion_batch_capacity = config->completion_batch_capacity;
    impl->admission_open = true;
    for (size_t index = 0u; index < config->request_capacity; ++index) {
        impl->records[index].index = index;
        impl->records[index].next = CFLOW_READINESS_INDEX_NONE;
        readiness_socket_reset(impl, &impl->sockets[index]);
    }

    turbo_mutex_init(&impl->gate);
    turbo_cond_init(&impl->changed);
    if (impl->gate == NULL || impl->changed == NULL) {
        turbo_cond_destroy(&impl->changed);
        turbo_mutex_destroy(&impl->gate);
        free(impl->sockets);
        free(impl->records);
        free(impl);
        return TURBO_ENOMEM;
    }
    status = turbo_readiness_reactor_init_kind(
        &impl->reactor, &reactor_config, reactor_kind);
    if (status != TURBO_OK) {
        turbo_cond_destroy(&impl->changed);
        turbo_mutex_destroy(&impl->gate);
        free(impl->sockets);
        free(impl->records);
        free(impl);
        return status;
    }
    backend->impl = impl;
    return TURBO_OK;
}
