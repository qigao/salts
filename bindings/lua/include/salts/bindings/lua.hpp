#ifndef SALTS_BINDINGS_LUA_HPP
#define SALTS_BINDINGS_LUA_HPP

#include <cmeta/object.hpp>
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
      const char *name, Borrowed<T> borrowed,
      const cmeta_data_desc *data,
      const cmeta_object_method_provider *provider = nullptr) const noexcept {
    cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
    cmeta_status status;

    if (state_ == nullptr || name == nullptr || name[0] == '\0')
      return CMETA_INVALID_ARGUMENT;

    status = ::Salts::detail::make_object_ref(
        borrowed, data, provider, &object);
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

#endif  // SALTS_BINDINGS_LUA_HPP
