#ifndef SALTS_BINDINGS_QUICKJS_HPP
#define SALTS_BINDINGS_QUICKJS_HPP

#include <salts/bindings/object.hpp>
#include <salts/bindings/quickjs/cmeta.h>

#include <utility>

namespace Salts::QuickJS {

class Value {
 public:
  Value() noexcept : context_(nullptr), value_(JS_UNDEFINED) {}

  Value(JSContext *context, JSValue value) noexcept
      : context_(context), value_(value) {}

  ~Value() { reset(); }

  Value(const Value &) = delete;
  Value &operator=(const Value &) = delete;

  Value(Value &&other) noexcept
      : context_(other.context_), value_(other.value_) {
    other.context_ = nullptr;
    other.value_ = JS_UNDEFINED;
  }

  Value &operator=(Value &&other) noexcept {
    if (this != &other) {
      reset();
      context_ = other.context_;
      value_ = other.value_;
      other.context_ = nullptr;
      other.value_ = JS_UNDEFINED;
    }
    return *this;
  }

  JSValueConst get() const noexcept { return value_; }
  bool is_exception() const noexcept { return JS_IsException(value_); }

  JSValue release() noexcept {
    JSValue value = value_;
    context_ = nullptr;
    value_ = JS_UNDEFINED;
    return value;
  }

  void reset() noexcept {
    if (context_ != nullptr)
      JS_FreeValue(context_, value_);
    context_ = nullptr;
    value_ = JS_UNDEFINED;
  }

 private:
  JSContext *context_;
  JSValue value_;
};

class Context {
 public:
  Context(JSContext *context, salts_quickjs_limits limits) noexcept
      : context_(context), limits_(limits) {}

  JSContext *native_handle() const noexcept { return context_; }
  salts_quickjs_limits limits() const noexcept { return limits_; }
  Value adopt(JSValue value) const noexcept { return Value{context_, value}; }

  template <typename T>
  cmeta_status bind_global(
      const char *name, Salts::Borrowed<T> borrowed,
      const cmeta_data_desc *data,
      const cmeta_object_field_provider *field_provider = nullptr,
      const cmeta_object_method_provider *method_provider = nullptr) const noexcept {
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    JSValue raw_value = JS_UNDEFINED;
    cmeta_status status;

    if (context_ == nullptr || name == nullptr || name[0] == '\0')
      return CMETA_INVALID_ARGUMENT;

    status = Salts::detail::bind_object_ref(
        &object, borrowed, data, field_provider, method_provider);
    if (status != CMETA_OK)
      return status;

    status = salts_quickjs_push_object(
        context_, &object, limits_, &raw_value);
    if (status != CMETA_OK) {
      cmeta_object_release(&object);
      return status;
    }

    Value value{context_, raw_value};
    Value global{context_, JS_GetGlobalObject(context_)};
    if (global.is_exception())
      return CMETA_CALLBACK_ERROR;

    const int set_status =
        JS_SetPropertyStr(context_, global.get(), name, value.release());
    return set_status < 0 ? CMETA_CALLBACK_ERROR : CMETA_OK;
  }

 private:
  JSContext *context_;
  salts_quickjs_limits limits_;
};

}  // namespace Salts::QuickJS

#endif
