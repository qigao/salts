#include <salts/native_io.h>
#include <salts/native_io_ace_token.h>
#include <salts/native_io_sharded.h>
#include <tinytest.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>

NATIVE_IO_ACE_TOKEN_TYPE(ace_cpp_request_token, int);

suite("NativeIO C++ headers") {
  group("public handles and operations") {
    it("accepts C++ handles and preserves operation aliases") {
      const native_io_endpoint endpoint{1u, 1u};
      const native_io_request request{1u, 1u};
      const native_io_operation_kind pipe_read = NATIVE_IO_OPERATION_PIPE_READ;
      const auto attach_pipe = &native_io_backend_attach_pipe;
      const auto release_pipe = &native_io_backend_release_pipe;
      const auto wake_backend = &native_io_backend_wake;
      const auto prepare = &native_io_backend_prepare;
      const auto flush = &native_io_backend_flush;
      const auto await_prepared = &native_io_coroutine_await_prepared;
      const auto sharded_create = &native_io_sharded_create;
      const auto sharded_submit = &native_io_sharded_submit_to;
      const auto sharded_try_submit = &native_io_sharded_try_submit_to;
      const auto sharded_shutdown = &native_io_sharded_shutdown;
      const auto sharded_attach_pipe = &native_io_sharded_context_attach_pipe;
      const auto sharded_release_pipe = &native_io_sharded_context_release_pipe;
      const auto sharded_owner_submit = &native_io_sharded_context_submit;
      const auto sharded_owner_submit_owned = &native_io_sharded_context_submit_owned;
      const auto sharded_owner_prepare_owned = &native_io_sharded_context_prepare_owned;
      const auto sharded_submit_owned = &native_io_sharded_submit_owned;
      const auto sharded_try_submit_owned = &native_io_sharded_try_submit_owned;
      static_assert(std::is_standard_layout_v<native_io_endpoint>);
      static_assert(std::is_standard_layout_v<native_io_sharded_config>);
      static_assert(std::is_standard_layout_v<native_io_sharded_endpoint>);
      static_assert(std::is_standard_layout_v<native_io_sharded_request>);
      static_assert(std::is_standard_layout_v<native_io_sharded_operation>);
      static_assert(std::is_standard_layout_v<native_io_sharded_ownership>);
      static_assert(sizeof(native_io_endpoint) == sizeof(std::uint32_t) * 2u);
      static_assert(offsetof(native_io_endpoint, slot) == 0u);
      static_assert(offsetof(native_io_endpoint, generation) == sizeof(std::uint32_t));
      (void)attach_pipe;
      (void)release_pipe;
      (void)wake_backend;
      (void)prepare;
      (void)flush;
      (void)await_prepared;
      (void)sharded_create;
      (void)sharded_submit;
      (void)sharded_try_submit;
      (void)sharded_shutdown;
      (void)sharded_attach_pipe;
      (void)sharded_release_pipe;
      (void)sharded_owner_submit;
      (void)sharded_owner_submit_owned;
      (void)sharded_owner_prepare_owned;
      (void)sharded_submit_owned;
      (void)sharded_try_submit_owned;
      check_true(native_io_endpoint_valid(endpoint));
      check_true(native_io_request_valid(request));
      check_equal(pipe_read, NATIVE_IO_OPERATION_PIPE_READ);
      check_equal(NATIVE_IO_OPERATION_PIPE_WRITE, 6);
      check_equal(NATIVE_IO_OPERATION_STREAM_RECV, 1);
      check_equal(NATIVE_IO_OPERATION_STREAM_SEND, 2);
      check_equal(NATIVE_IO_OPERATION_STREAM_CONNECT, 7);
      check_equal(NATIVE_IO_OPERATION_TCP_RECV, NATIVE_IO_OPERATION_STREAM_RECV);
      check_equal(NATIVE_IO_OPERATION_TCP_SEND, NATIVE_IO_OPERATION_STREAM_SEND);
      check_equal(NATIVE_IO_OPERATION_TCP_CONNECT, NATIVE_IO_OPERATION_STREAM_CONNECT);
    }
  }

  group("POSA2 ACT typed native association") {
    it("settles one exact completion but rejects stale and double settlement") {
      const native_io_request request{1u, 3u};
      const native_io_endpoint endpoint{2u, 5u};
      native_io_completion completion{};
      ace_cpp_request_token token{};
      int context = 7;
      int *settled = nullptr;
      completion.request = request;
      completion.endpoint = endpoint;
      completion.kind = NATIVE_IO_COMPLETION_CANCELLED;
      completion.user_data = 22u;
      check_equal(ace_cpp_request_token_bind(&token, request, endpoint,
                                             22u, &context), SALTS_OK);
      completion.user_data = 23u;
      check_equal(ace_cpp_request_token_settle(&token, &completion, &settled),
                  SALTS_ENOENT);
      check_null(settled);
      completion.user_data = 22u;
      check_equal(ace_cpp_request_token_settle(&token, &completion, &settled),
                  SALTS_OK);
      check_true(settled == &context);
      check_equal(ace_cpp_request_token_settle(&token, &completion, &settled),
                  SALTS_EALREADY);
      check_null(settled);
    }
  }

  group("backend models") {
    it("maps IOCP and io_uring to completion") {
      check_equal(native_io_backend_kind_model(NATIVE_IO_BACKEND_IOCP),
                  NATIVE_IO_MODEL_COMPLETION);
      check_equal(native_io_backend_kind_model(NATIVE_IO_BACKEND_IO_URING),
                  NATIVE_IO_MODEL_COMPLETION);
    }

    it("maps epoll and kqueue to readiness") {
      check_equal(native_io_backend_kind_model(NATIVE_IO_BACKEND_EPOLL),
                  NATIVE_IO_MODEL_READINESS);
      check_equal(native_io_backend_kind_model(NATIVE_IO_BACKEND_KQUEUE),
                  NATIVE_IO_MODEL_READINESS);
    }
  }
}
