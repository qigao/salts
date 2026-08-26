#include <cflow/usb.h>
#include <tinytest.h>

#include <type_traits>

spec("CFlow USB C++ header") {
    it("preserves the C enumeration API") {
        static_assert(std::is_same_v<
            decltype(&cflow_usb_context_init),
            int (*)(cflow_usb_context *, const cflow_usb_context_config *)>);
        static_assert(std::is_same_v<
            decltype(&cflow_usb_enumerate),
            int (*)(cflow_usb_context *, cflow_usb_device_info *,
                    size_t, size_t *)>);
        cflow_usb_context context{};
        check_null(context.impl);
    }
}
