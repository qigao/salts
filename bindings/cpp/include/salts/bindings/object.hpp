#ifndef SALTS_BINDINGS_OBJECT_HPP
#define SALTS_BINDINGS_OBJECT_HPP

#include <cmeta/object.h>

namespace Salts {

template <typename T>
struct Borrowed {
  T *object;
};

template <typename T>
constexpr Borrowed<T> borrow(T &object) noexcept {
  return Borrowed<T>{&object};
}

}  // namespace Salts

#endif
