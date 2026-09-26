#ifndef SALTS_BINDINGS_OBJECT_HPP
#define SALTS_BINDINGS_OBJECT_HPP

#include <cmeta/object.h>

#include <type_traits>

namespace Salts {

template <typename T>
struct Borrowed {
  T *object;
};

template <typename T>
constexpr Borrowed<T> borrow(T &object) noexcept {
  static_assert(!std::is_const_v<T>,
                "Salts::borrow requires mutable native storage");
  return Borrowed<T>{&object};
}

namespace detail {

template <typename T>
inline cmeta_status bind_object_ref(
    cmeta_object_ref *out, Borrowed<T> borrowed,
    const cmeta_data_desc *data,
    const cmeta_object_field_provider *field_provider,
    const cmeta_object_method_provider *method_provider) noexcept {
  static_assert(!std::is_const_v<T>,
                "CMeta object references require mutable native storage");
  if (out == nullptr || borrowed.object == nullptr || data == nullptr)
    return CMETA_INVALID_ARGUMENT;
  if (field_provider != nullptr || method_provider != nullptr)
    return cmeta_object_borrow_with_providers(
        out, static_cast<void *>(borrowed.object), data,
        field_provider, method_provider);
  return cmeta_object_borrow(
      out, static_cast<void *>(borrowed.object), data, nullptr);
}

}  // namespace detail
}  // namespace Salts

#endif
