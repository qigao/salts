#ifndef SALTS_BINDINGS_LUA_HPP
#define SALTS_BINDINGS_LUA_HPP

#include <salts/bindings/object.hpp>
#include <salts/bindings/lua/cmeta.h>

#include <lua.h>

namespace Salts::Lua {

class Context {
 public:
  Context(lua_State *state, salts_lua_limits limits) noexcept
      : state_(state), limits_(limits) {}

  lua_State *native_handle() const noexcept { return state_; }
  salts_lua_limits limits() const noexcept { return limits_; }

  template <typename T>
  cmeta_status bind_global(
      const char *name, Salts::Borrowed<T> borrowed,
      const cmeta_data_desc *data,
      const cmeta_object_field_provider *field_provider = nullptr,
      const cmeta_object_method_provider *method_provider = nullptr) const noexcept {
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    cmeta_status status;

    if (state_ == nullptr || name == nullptr || name[0] == '\0')
      return CMETA_INVALID_ARGUMENT;

    status = Salts::detail::bind_object_ref(
        &object, borrowed, data, field_provider, method_provider);
    if (status != CMETA_OK)
      return status;

    status = salts_lua_push_object(state_, &object, limits_);
    if (status != CMETA_OK) {
      cmeta_object_release(&object);
      return status;
    }

    lua_setglobal(state_, name);
    return CMETA_OK;
  }

 private:
  lua_State *state_;
  salts_lua_limits limits_;
};

}  // namespace Salts::Lua

#endif
