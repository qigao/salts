#ifndef CMETA_OBJECT_HPP
#define CMETA_OBJECT_HPP

#include <cmeta/object.h>

#include <cstddef>
#include <type_traits>

namespace Salts {

template <typename T>
class Borrowed {
 public:
  static_assert(!std::is_const_v<T>,
                "canonical native object binding requires mutable storage");

  explicit Borrowed(T &object) noexcept : object_(&object) {}

  T *get() const noexcept { return object_; }

 private:
  T *object_;
};

template <typename T>
Borrowed<T> borrow(T &object) noexcept {
  return Borrowed<T>(object);
}

namespace detail {

template <typename T>
cmeta_status make_object_ref(
    Borrowed<T> borrowed, const cmeta_data_desc *data,
    const cmeta_object_method_provider *provider,
    cmeta_object_ref *out) noexcept {
  if (out == nullptr || data == nullptr || data->storage_type == nullptr)
    return CMETA_INVALID_ARGUMENT;
  if (data->storage_type->kind != CMETA_T_OBJECT ||
      data->storage_type->size != sizeof(T) ||
      data->storage_type->align != alignof(T))
    return CMETA_TYPE_MISMATCH;

  return provider != nullptr
             ? cmeta_object_borrow_with_provider(
                   out, static_cast<void *>(borrowed.get()), data, provider)
             : cmeta_object_borrow(
                   out, static_cast<void *>(borrowed.get()), data, nullptr);
}

}  // namespace detail
}  // namespace Salts

#endif  // CMETA_OBJECT_HPP
