#ifndef SALTS_BINDINGS_QUICKJS_HPP
#define SALTS_BINDINGS_QUICKJS_HPP

#include <salts/bindings/object.hpp>
#include <salts/bindings/quickjs/cmeta.h>

namespace Salts::QuickJS {

class Context {
 public:
  Context(JSContext *context, salts_quickjs_limits limits) noexcept
      : context_(context), limits_(limits) {}

  JSContext *native_handle() const noexcept { return context_; }
  salts_quickjs_limits limits() const noexcept { return limits_; }

  template <typename T>
  cmeta_status bind_global(
      const char *name, Salts::Borrowed<T> borrowed,
      const cmeta_data_desc *data,
      const cmeta_object_field_provider *field_provider = nullptr,
      const cmeta_object_method_provider *method_provider = nullptr) const noexcept {
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    JSValue value = JS_UNDEFINED;
    JSValue global = JS_UNDEFINED;
    cmeta_status status;
    int set_status;

    if (context_ == nullptr || name == nullptr || name[0] == '\0')
      return CMETA_INVALID_ARGUMENT;

    status = Salts::detail::bind_object_ref(
        &object, borrowed, data, field_provider, method_provider);
    if (status != CMETA_OK)
      return status;

    status = salts_quickjs_push_object(
        context_, &object, limits_, &value);
    if (status != CMETA_OK) {
      cmeta_object_release(&object);
      return status;
    }

    global = JS_GetGlobalObject(context_);
    if (JS_IsException(global)) {
      JS_FreeValue(context_, value);
      return CMETA_CALLBACK_ERROR;
    }

    set_status = JS_SetPropertyStr(context_, global, name, value);
    JS_FreeValue(context_, global);
    return set_status < 0 ? CMETA_CALLBACK_ERROR : CMETA_OK;
  }

 private:
  JSContext *context_;
  salts_quickjs_limits limits_;
};

}  // namespace Salts::QuickJS

#endif
