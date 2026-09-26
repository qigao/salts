#ifndef SALTS_BINDINGS_QUICKJS_HPP
#define SALTS_BINDINGS_QUICKJS_HPP

#include <cmeta/object.hpp>
#include <salts/bindings/quickjs/cmeta.h>

#include <quickjs.h>

namespace Salts::QuickJS {

class Context {
 public:
  Context(JSContext *context, salts_quickjs_limits limits) noexcept
      : context_(context), limits_(limits) {}

  JSContext *native_handle() const noexcept { return context_; }
  salts_quickjs_limits limits() const noexcept { return limits_; }

  template <typename T>
  cmeta_status bind_global(
      const char *name, Borrowed<T> borrowed,
      const cmeta_data_desc *data,
      const cmeta_object_method_provider *provider = nullptr) const noexcept {
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    JSValue value = JS_UNDEFINED;
    JSValue global = JS_UNDEFINED;
    cmeta_status status;

    if (context_ == nullptr || name == nullptr || name[0] == '\0')
      return CMETA_INVALID_ARGUMENT;

    status = detail::make_object_ref(
        borrowed, data, provider, &object);
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
    if (JS_SetPropertyStr(context_, global, name, value) < 0) {
      JS_FreeValue(context_, global);
      return CMETA_CALLBACK_ERROR;
    }
    JS_FreeValue(context_, global);
    return CMETA_OK;
  }

 private:
  JSContext *context_;
  salts_quickjs_limits limits_;
};

}  // namespace Salts::QuickJS

#endif  // SALTS_BINDINGS_QUICKJS_HPP
