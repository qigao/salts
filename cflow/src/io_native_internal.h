#ifndef CFLOW_IO_NATIVE_INTERNAL_H
#define CFLOW_IO_NATIVE_INTERNAL_H

#include <cflow/io_native.h>

typedef struct cflow_io_native_impl cflow_io_native_impl;

typedef struct cflow_io_native_impl_ops {
    int (*submit)(cflow_io_native_impl *impl,
                  cflow_io_actor *actor,
                  cflow_io_request_id request_id,
                  cflow_io_native_operation *operation);
    int (*submit_vector)(cflow_io_native_impl *impl,
                         cflow_io_actor *actor,
                         cflow_io_request_id request_id,
                         cflow_io_native_vector_operation *operation);
    int (*submit_pipe)(cflow_io_native_impl *impl,
                       cflow_io_actor *actor,
                       cflow_io_request_id request_id,
                       cflow_io_native_pipe_operation *operation);
    int (*submit_file)(cflow_io_native_impl *impl,
                       cflow_io_actor *actor,
                       cflow_io_request_id request_id,
                       cflow_io_native_file_operation *operation);
    int (*cancel)(cflow_io_native_impl *impl,
                  cflow_io_request_id request_id);
    bool (*get_stats)(const cflow_io_native_impl *impl,
                      cflow_io_native_backend_stats *out);
    int (*forget_socket)(cflow_io_native_impl *impl, uintptr_t closed_socket);
    int (*forget_pipe)(cflow_io_native_impl *impl, uintptr_t closed_handle);
    int (*forget_file)(cflow_io_native_impl *impl, uintptr_t closed_handle);
    int (*shutdown)(cflow_io_native_impl *impl);
    int (*destroy)(cflow_io_native_impl *impl);
} cflow_io_native_impl_ops;

struct cflow_io_native_impl {
    const cflow_io_native_impl_ops *ops;
    cflow_io_native_backend_kind kind;
};

bool cflow_io_native_operation_valid(const cflow_io_native_operation *operation);
bool cflow_io_native_vector_operation_valid(
    const cflow_io_native_vector_operation *operation);
bool cflow_io_native_pipe_operation_valid(
    const cflow_io_native_pipe_operation *operation);
bool cflow_io_native_file_operation_valid(
    const cflow_io_native_file_operation *operation);

#if defined(CFLOW_HAS_NATIVE_IOCP)
int cflow_io_native_iocp_init(cflow_io_native_backend *backend,
                              const cflow_io_native_backend_config *config);
#endif

#if defined(CFLOW_HAS_NATIVE_EPOLL) || defined(CFLOW_HAS_NATIVE_KQUEUE) || \
    defined(CFLOW_HAS_NATIVE_POLL)
int cflow_io_native_readiness_init(
    cflow_io_native_backend *backend,
    const cflow_io_native_backend_config *config);
#endif

#if defined(CFLOW_HAS_NATIVE_IO_URING)
int cflow_io_native_io_uring_init(
    cflow_io_native_backend *backend,
    const cflow_io_native_backend_config *config);
#endif

#endif /* CFLOW_IO_NATIVE_INTERNAL_H */
