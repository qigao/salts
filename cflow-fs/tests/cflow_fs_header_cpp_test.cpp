#include <cflow/fs.h>
#include <cflow/fs_watch.h>
#include <tinytest.h>

#include <type_traits>

spec("CFlow filesystem C++ header") {
    it("preserves the C API function types") {
        static_assert(std::is_same_v<
            decltype(&cflow_fs_service_init),
            int (*)(cflow_fs_service *, const cflow_fs_config *)>);
        static_assert(std::is_same_v<
            decltype(&cflow_fs_try_rename),
            cflow_fs_submit_result (*)(cflow_fs_service *, const char *,
                                       const char *)>);
        static_assert(std::is_same_v<
            decltype(&cflow_fs_run_ready),
            int (*)(cflow_fs_service *, size_t, size_t *)>);
        cflow_fs_service service{};
        cflow_fs_watch watch{};
        check_null(service.impl);
        check_null(watch.impl);
    }
}
