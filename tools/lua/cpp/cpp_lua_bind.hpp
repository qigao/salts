#pragma once


#define CPP_LUA_EXPAND(x) x
#define CPP_LUA_CONCAT_INNER(a, b) a##b
#define CPP_LUA_CONCAT(a, b) CPP_LUA_CONCAT_INNER(a, b)

#define CPP_LUA_COUNT_ARGS(...)                                                                  \
  CPP_LUA_EXPAND(                                                                               \
      CPP_LUA_COUNT_ARGS_HELPER(__VA_ARGS__, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1))
#define CPP_LUA_COUNT_ARGS_HELPER(                                                              \
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, N, ...) N

#include <lua.hpp>

#include <array>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4702) /* if constexpr type dispatch can leave branch tails unreachable */
#endif

namespace cpp_lua_bind {

template <typename T>
struct always_false : std::false_type {};

template <typename T>
struct is_optional : std::false_type {};

template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};

template <typename T>
struct is_shared_ptr : std::false_type {};

template <typename T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

template <typename T>
struct is_std_array : std::false_type {};

template <typename T, size_t N>
struct is_std_array<std::array<T, N>> : std::true_type {};

template <typename T>
struct is_std_tuple : std::false_type {};

template <typename... Ts>
struct is_std_tuple<std::tuple<Ts...>> : std::true_type {};

template <typename T>
struct is_std_pair : std::false_type {};

template <typename A, typename B>
struct is_std_pair<std::pair<A, B>> : std::true_type {};

template <typename T>
struct is_std_variant : std::false_type {};

template <typename... Ts>
struct is_std_variant<std::variant<Ts...>> : std::true_type {};

template <typename Signature>
struct function_pointer;

template <typename R, typename... Args>
struct function_pointer<R(Args...)> {
  using type = R (*)(Args...);
};

template <typename Signature>
using function_pointer_t = typename function_pointer<Signature>::type;

template <typename T, typename = void>
struct is_map_like : std::false_type {};

template <typename T>
struct is_map_like<T, std::void_t<typename T::key_type, typename T::mapped_type>>
    : std::true_type {};

template <typename T, typename = void>
struct is_sequence_like : std::false_type {};

template <typename T>
struct is_sequence_like<
    T,
    std::void_t<typename T::value_type, decltype(std::declval<T &>().begin()),
                decltype(std::declval<T &>().end()),
                decltype(std::declval<T &>().insert(std::declval<T &>().end(),
                                                    std::declval<typename T::value_type>()))>>
    : std::true_type {};

using type_id = const void *;

template <typename T>
type_id type_id_of() {
  static const char tag = 0;
  return &tag;
}

inline std::mutex &type_base_registry_mutex() {
  static std::mutex mutex;
  return mutex;
}

inline std::unordered_map<type_id, std::vector<type_id>> &type_base_registry() {
  static std::unordered_map<type_id, std::vector<type_id>> registry;
  return registry;
}

template <typename Derived, typename Base>
void register_base_relationship() {
  std::lock_guard<std::mutex> lock(type_base_registry_mutex());
  auto &bases = type_base_registry()[type_id_of<Derived>()];
  if (std::find(bases.begin(), bases.end(), type_id_of<Base>()) == bases.end()) {
    bases.push_back(type_id_of<Base>());
  }
}

inline bool type_matches_unlocked(type_id current, type_id target,
                                  std::vector<type_id> &visited) {
  if (current == target) {
    return true;
  }
  if (std::find(visited.begin(), visited.end(), current) != visited.end()) {
    return false;
  }
  visited.push_back(current);

  auto &registry = type_base_registry();
  const auto it = registry.find(current);
  if (it == registry.end()) {
    return false;
  }
  for (type_id base : it->second) {
    if (type_matches_unlocked(base, target, visited)) {
      return true;
    }
  }
  return false;
}

inline bool type_matches(type_id current, type_id target) {
  std::lock_guard<std::mutex> lock(type_base_registry_mutex());
  std::vector<type_id> visited;
  return type_matches_unlocked(current, target, visited);
}

inline void push_type_id(lua_State *L, type_id id) {
  lua_pushlightuserdata(L, const_cast<void *>(id));
}

template <typename T>
bool is_same_type(lua_State *L, int index) {
  if (!lua_isuserdata(L, index) || !lua_getmetatable(L, index)) {
    return false;
  }

  lua_getfield(L, -1, "__cpp_typeid");
  const bool same = lua_touserdata(L, -1) == type_id_of<T>();
  lua_pop(L, 2);
  return same;
}

template <typename T>
bool is_instance_of(lua_State *L, int index) {
  if (!lua_isuserdata(L, index) || !lua_getmetatable(L, index)) {
    return false;
  }
  lua_getfield(L, -1, "__cpp_typeid");
  const type_id current = static_cast<type_id>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  const bool result = type_matches(current, type_id_of<T>());
  lua_pop(L, 1);
  return result;
}

template <typename T>
using decay_arg_t = std::decay_t<T>;

template <typename T>
decay_arg_t<T> stack_get(lua_State *L, int index);

template <typename T>
bool lua_arg_matches(lua_State *L, int index);

template <typename Variant, size_t... I>
Variant stack_get_variant(lua_State *L, int index, std::index_sequence<I...>) {
  Variant result;
  bool matched = false;
  ((!matched && lua_arg_matches<std::variant_alternative_t<I, Variant>>(L, index)
        ? (result = stack_get<std::variant_alternative_t<I, Variant>>(L, index),
           matched = true)
        : false),
   ...);
  if (!matched) {
    luaL_error(L, "cpp_lua_bind: no variant alternative matches the Lua value");
  }
  return result;
}

template <typename T>
decay_arg_t<T> stack_get(lua_State *L, int index) {
  using D = decay_arg_t<T>;
  if constexpr (std::is_same_v<D, bool>) {
    return lua_toboolean(L, index) != 0;
  } else if constexpr (std::is_integral_v<D>) {
    return static_cast<D>(lua_tointeger(L, index));
  } else if constexpr (std::is_floating_point_v<D>) {
    return static_cast<D>(lua_tonumber(L, index));
  } else if constexpr (std::is_same_v<D, const char *>) {
    return lua_tostring(L, index);
  } else if constexpr (std::is_same_v<D, char *>) {
    const char *value = lua_tostring(L, index);
    return const_cast<char *>(value);
  } else if constexpr (std::is_same_v<D, std::string>) {
    const char *value = lua_tostring(L, index);
    return value ? std::string(value) : std::string();
  } else if constexpr (std::is_same_v<D, std::string_view>) {
    size_t length = 0;
    const char *value = lua_tolstring(L, index, &length);
    return value ? std::string_view(value, length) : std::string_view();
  } else if constexpr (is_optional<D>::value) {
    if (lua_isnil(L, index)) {
      return D{};
    }
    return D(stack_get<typename D::value_type>(L, index));
  } else if constexpr (is_shared_ptr<D>::value) {
    if (lua_isnil(L, index)) {
      return D{};
    }
    return std::make_shared<typename D::element_type>(
        stack_get<typename D::element_type>(L, index));
  } else if constexpr (is_std_tuple<D>::value) {
    D result{};
    const int absolute_index = index > 0 ? index : lua_gettop(L) + index + 1;
    std::apply(
        [&](auto &...item) {
          size_t i = 0;
          ((lua_rawgeti(L, absolute_index, static_cast<lua_Integer>(++i)),
            item = stack_get<std::decay_t<decltype(item)>>(L, -1), lua_pop(L, 1)),
           ...);
        },
        result);
    return result;
  } else if constexpr (is_std_array<D>::value) {
    D result{};
    for (size_t i = 0; i < result.size(); ++i) {
      result[i] = stack_get<typename D::value_type>(L, index);
    }
    return result;
  } else if constexpr (is_std_pair<D>::value) {
    const int absolute_index = index > 0 ? index : lua_gettop(L) + index + 1;
    lua_rawgeti(L, absolute_index, 1);
    auto first = stack_get<typename D::first_type>(L, -1);
    lua_pop(L, 1);
    lua_rawgeti(L, absolute_index, 2);
    auto second = stack_get<typename D::second_type>(L, -1);
    lua_pop(L, 1);
    return D{first, second};
  } else if constexpr (is_std_variant<D>::value) {
    return stack_get_variant<D>(L, index,
                                std::make_index_sequence<std::variant_size_v<D>>{});
  } else if constexpr (is_map_like<D>::value) {
    D result;
    const int absolute_index = index > 0 ? index : lua_gettop(L) + index + 1;
    lua_pushnil(L);
    while (lua_next(L, absolute_index) != 0) {
      result.emplace(stack_get<typename D::key_type>(L, -2),
                     stack_get<typename D::mapped_type>(L, -1));
      lua_pop(L, 1);
    }
    return result;
  } else if constexpr (is_sequence_like<D>::value) {
    D result;
    const int absolute_index = index > 0 ? index : lua_gettop(L) + index + 1;
    const lua_Integer length = static_cast<lua_Integer>(lua_rawlen(L, absolute_index));
    for (lua_Integer i = 1; i <= length; ++i) {
      lua_rawgeti(L, absolute_index, i);
      result.insert(result.end(), stack_get<typename D::value_type>(L, -1));
      lua_pop(L, 1);
    }
    return result;
  } else if constexpr (std::is_pointer_v<D>) {
    return static_cast<D>(lua_touserdata(L, index));
  } else if constexpr (std::is_same_v<D, std::nullptr_t>) {
    return nullptr;
  } else {
    static_assert(always_false<D>::value,
                  "cpp_lua_bind: unsupported Lua-to-C++ argument type");
  }
}

template <typename T>
void push_value(lua_State *L, T &&value) {
  using D = decay_arg_t<T>;
  if constexpr (std::is_same_v<D, bool>) {
    lua_pushboolean(L, value ? 1 : 0);
  } else if constexpr (std::is_integral_v<D>) {
    lua_pushinteger(L, static_cast<lua_Integer>(value));
  } else if constexpr (std::is_floating_point_v<D>) {
    lua_pushnumber(L, static_cast<lua_Number>(value));
  } else if constexpr (std::is_same_v<D, const char *>) {
    lua_pushstring(L, value ? value : "");
  } else if constexpr (std::is_same_v<D, char *>) {
    lua_pushstring(L, value ? value : "");
  } else if constexpr (std::is_same_v<D, std::string>) {
    lua_pushlstring(L, value.data(), value.size());
  } else if constexpr (std::is_same_v<D, std::string_view>) {
    lua_pushlstring(L, value.data(), value.size());
  } else if constexpr (is_optional<D>::value) {
    if (value) {
      push_value(L, *value);
    } else {
      lua_pushnil(L);
    }
  } else if constexpr (is_shared_ptr<D>::value) {
    if (value) {
      push_value(L, *value);
    } else {
      lua_pushnil(L);
    }
  } else if constexpr (is_std_tuple<D>::value) {
    std::apply(
        [&](const auto &...item) {
          lua_newtable(L);
          size_t i = 0;
          ((lua_pushinteger(L, static_cast<lua_Integer>(++i)), push_value(L, item),
            lua_settable(L, -3)),
           ...);
        },
        value);
  } else if constexpr (is_std_array<D>::value) {
    lua_newtable(L);
    for (size_t i = 0; i < value.size(); ++i) {
      lua_pushinteger(L, static_cast<lua_Integer>(i + 1));
      push_value(L, value[i]);
      lua_settable(L, -3);
    }
  } else if constexpr (is_std_pair<D>::value) {
    lua_newtable(L);
    lua_pushinteger(L, 1);
    push_value(L, value.first);
    lua_settable(L, -3);
    lua_pushinteger(L, 2);
    push_value(L, value.second);
    lua_settable(L, -3);
  } else if constexpr (is_std_variant<D>::value) {
    std::visit([&](const auto &item) { push_value(L, item); }, value);
  } else if constexpr (is_map_like<D>::value) {
    lua_newtable(L);
    for (const auto &entry : value) {
      push_value(L, entry.first);
      push_value(L, entry.second);
      lua_settable(L, -3);
    }
  } else if constexpr (is_sequence_like<D>::value) {
    lua_newtable(L);
    lua_Integer i = 1;
    for (const auto &item : value) {
      lua_pushinteger(L, i++);
      push_value(L, item);
      lua_settable(L, -3);
    }
  } else if constexpr (std::is_pointer_v<D>) {
    lua_pushlightuserdata(L, const_cast<void *>(static_cast<const void *>(value)));
  } else if constexpr (std::is_same_v<D, std::nullptr_t>) {
    lua_pushnil(L);
  } else {
    static_assert(always_false<D>::value,
                  "cpp_lua_bind: unsupported C++-to-Lua return type");
  }
}

template <typename T>
int push_return_value(lua_State *L, T &&value) {
  using D = decay_arg_t<T>;
  if constexpr (is_std_tuple<D>::value) {
    constexpr size_t count = std::tuple_size_v<D>;
    std::apply([&](auto &&...item) { (push_value(L, item), ...); },
               std::forward<T>(value));
    return static_cast<int>(count);
  } else if constexpr (is_std_pair<D>::value) {
    push_value(L, value.first);
    push_value(L, value.second);
    return 2;
  } else {
    push_value(L, std::forward<T>(value));
    return 1;
  }
}

template <typename Fn>
struct free_function_traits;

template <typename R, typename... Args>
struct free_function_traits<R (*)(Args...)> {
  using return_type = R;
  using args = std::tuple<Args...>;
  static constexpr size_t arity = sizeof...(Args);
};

template <size_t I, typename Traits>
using free_arg_t = decay_arg_t<typename std::tuple_element<I, typename Traits::args>::type>;

template <auto Fn, size_t... I>
int invoke_free_function(lua_State *L, std::index_sequence<I...>) {
  using traits = free_function_traits<decltype(Fn)>;
  using return_type = typename traits::return_type;

  if constexpr (std::is_void_v<return_type>) {
    Fn(stack_get<free_arg_t<I, traits>>(L, I + 1)...);
    return 0;
  } else {
    decay_arg_t<return_type> result = Fn(stack_get<free_arg_t<I, traits>>(L, I + 1)...);
    return push_return_value(L, result);
  }
}

template <auto Fn>
int free_function_trampoline(lua_State *L) {
  try {
    using traits = free_function_traits<decltype(Fn)>;
    return invoke_free_function<Fn>(L, std::make_index_sequence<traits::arity>{});
  } catch (const std::exception &e) {
    return luaL_error(L, "C++ exception in bound function: %s", e.what());
  } catch (...) {
    return luaL_error(L, "C++ exception in bound function");
  }
}

template <auto Fn>
void bind_function(lua_State *L, const char *name) {
  lua_pushcclosure(L, &free_function_trampoline<Fn>, 0);
  lua_setglobal(L, name);
}

template <typename Traits, size_t... I>
bool free_args_match(lua_State *L, std::index_sequence<I...>) {
  return (lua_arg_matches<free_arg_t<I, Traits>>(L, I + 1) && ...);
}

template <auto Fn>
int try_call_free_function(lua_State *L) {
  using traits = free_function_traits<decltype(Fn)>;
  if (lua_gettop(L) != static_cast<int>(traits::arity)) {
    return -1;
  }
  if (!free_args_match<traits>(L, std::make_index_sequence<traits::arity>{})) {
    return -1;
  }
  return invoke_free_function<Fn>(L, std::make_index_sequence<traits::arity>{});
}

template <auto Head, auto... Tail>
int overload_dispatcher(lua_State *L) {
  const int result = try_call_free_function<Head>(L);
  if (result >= 0) {
    return result;
  }
  if constexpr (sizeof...(Tail) > 0) {
    return overload_dispatcher<Tail...>(L);
  }
  return luaL_error(L, "cpp_lua_bind: no overload matches the Lua arguments");
}

template <auto... Fn>
void bind_function_overloads(lua_State *L, const char *name) {
  lua_pushcclosure(L, &overload_dispatcher<Fn...>, 0);
  lua_setglobal(L, name);
}

struct callable_upvalue_base {
  virtual ~callable_upvalue_base() = default;
};

template <typename R, typename... Args>
struct callable_upvalue final : callable_upvalue_base {
  explicit callable_upvalue(std::function<R(Args...)> value)
      : function(std::move(value)) {}
  std::function<R(Args...)> function;
};

int callable_gc(lua_State *L) {
  auto *base = static_cast<callable_upvalue_base *>(lua_touserdata(L, 1));
  if (base) {
    base->~callable_upvalue_base();
  }
  return 0;
}

template <typename R, typename... Args>
struct callable_traits {
  using return_type = R;
  using args = std::tuple<Args...>;
};

template <size_t I, typename Traits>
using callable_arg_t = decay_arg_t<typename std::tuple_element<I, typename Traits::args>::type>;

template <typename R, typename... Args, size_t... I>
int invoke_callable(lua_State *L, std::function<R(Args...)> &function,
                    std::index_sequence<I...>) {
  using traits = callable_traits<R, Args...>;
  using return_type = typename traits::return_type;

  if constexpr (std::is_void_v<return_type>) {
    function(stack_get<callable_arg_t<I, traits>>(L, I + 1)...);
    return 0;
  } else {
    decay_arg_t<return_type> result =
        function(stack_get<callable_arg_t<I, traits>>(L, I + 1)...);
    return push_return_value(L, result);
  }
}

template <typename R, typename... Args>
int callable_trampoline(lua_State *L) {
  try {
    auto *upvalue =
        static_cast<callable_upvalue<R, Args...> *>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!upvalue) {
      return luaL_error(L, "std::function upvalue is missing");
    }
    return invoke_callable(L, upvalue->function,
                           std::make_index_sequence<sizeof...(Args)>{});
  } catch (const std::exception &e) {
    return luaL_error(L, "C++ exception in std::function: %s", e.what());
  } catch (...) {
    return luaL_error(L, "C++ exception in std::function");
  }
}

template <typename R, typename... Args>
void bind_callable(lua_State *L, const char *name, std::function<R(Args...)> function) {
  void *memory = lua_newuserdata(L, sizeof(callable_upvalue<R, Args...>));
  new (memory) callable_upvalue<R, Args...>{std::move(function)};

  luaL_newmetatable(L, "cpp_lua_callable_upvalue");
  lua_pushcclosure(L, &callable_gc, 0);
  lua_setfield(L, -2, "__gc");
  lua_setmetatable(L, -2);

  lua_pushcclosure(L, &callable_trampoline<R, Args...>, 1);
  lua_setglobal(L, name);
}

template <typename Method>
struct member_function_traits;

template <typename C, typename R, typename... Args>
struct member_function_traits<R (C::*)(Args...)> {
  using class_type = C;
  using return_type = R;
  using args = std::tuple<Args...>;
  static constexpr size_t arity = sizeof...(Args);
};

template <typename C, typename R, typename... Args>
struct member_function_traits<R (C::*)(Args...) const> {
  using class_type = C;
  using return_type = R;
  using args = std::tuple<Args...>;
  static constexpr size_t arity = sizeof...(Args);
};

template <size_t I, typename Traits>
using member_arg_t = decay_arg_t<typename std::tuple_element<I, typename Traits::args>::type>;

template <typename T, auto Method, size_t... I>
int invoke_member_function(lua_State *L, T *self, std::index_sequence<I...>) {
  using traits = member_function_traits<decltype(Method)>;
  using return_type = typename traits::return_type;

  if constexpr (std::is_void_v<return_type>) {
    (self->*Method)(stack_get<member_arg_t<I, traits>>(L, I + 2)...);
    return 0;
  } else {
    decay_arg_t<return_type> result =
        (self->*Method)(stack_get<member_arg_t<I, traits>>(L, I + 2)...);
    return push_return_value(L, result);
  }
}

template <typename T, auto Method>
int member_function_trampoline(lua_State *L) {
  try {
    using traits = member_function_traits<decltype(Method)>;
    static_assert(std::is_base_of_v<typename traits::class_type, T>,
                  "cpp_lua_bind: class type must derive from method owner");

    T *self = static_cast<T *>(lua_touserdata(L, 1));
    if (!self) {
      return luaL_error(L, "method called on null userdata");
    }
    return invoke_member_function<T, Method>(L, self, std::make_index_sequence<traits::arity>{});
  } catch (const std::exception &e) {
    return luaL_error(L, "C++ exception in bound method: %s", e.what());
  } catch (...) {
    return luaL_error(L, "C++ exception in bound method");
  }
}

template <typename T, auto Method>
void bind_method(lua_State *L, const char *class_name, const char *method_name) {
  luaL_getmetatable(L, class_name);
  lua_pushcclosure(L, &member_function_trampoline<T, Method>, 0);
  lua_setfield(L, -2, method_name);
  lua_pop(L, 1);
}

template <typename Member>
struct member_object_traits;

template <typename C, typename V>
struct member_object_traits<V C::*> {
  using class_type = C;
  using value_type = V;
};

template <typename T, auto Member>
int property_getter(lua_State *L) {
  using traits = member_object_traits<decltype(Member)>;
  static_assert(std::is_base_of_v<typename traits::class_type, T>,
                "cpp_lua_bind: class type must derive from member owner");

  T *self = static_cast<T *>(lua_touserdata(L, 1));
  if (!self) {
    return luaL_error(L, "property accessed on null userdata");
  }
  push_value(L, self->*Member);
  return 1;
}

template <typename T, auto Member>
int property_setter(lua_State *L) {
  using traits = member_object_traits<decltype(Member)>;
  static_assert(std::is_base_of_v<typename traits::class_type, T>,
                "cpp_lua_bind: class type must derive from member owner");

  T *self = static_cast<T *>(lua_touserdata(L, 1));
  if (!self) {
    return luaL_error(L, "property assigned on null userdata");
  }
  self->*Member = stack_get<typename traits::value_type>(L, 2);
  return 0;
}

template <typename T, auto Member>
void bind_property(lua_State *L, const char *class_name, const char *member_name) {
  luaL_getmetatable(L, class_name);

  lua_getfield(L, -1, "__property_getters");
  lua_pushcclosure(L, &property_getter<T, Member>, 0);
  lua_setfield(L, -2, member_name);
  lua_pop(L, 1);

  lua_getfield(L, -1, "__property_setters");
  lua_pushcclosure(L, &property_setter<T, Member>, 0);
  lua_setfield(L, -2, member_name);
  lua_pop(L, 1);

  lua_pop(L, 1);
}

template <typename T, auto Member>
void bind_member(lua_State *L, const char *class_name, const char *member_name) {
  if constexpr (std::is_member_function_pointer_v<decltype(Member)>) {
    bind_method<T, Member>(L, class_name, member_name);
  } else {
    bind_property<T, Member>(L, class_name, member_name);
  }
}

inline int class_index(lua_State *L) {
  lua_getfield(L, lua_upvalueindex(2), lua_tostring(L, 2));
  if (!lua_isnil(L, -1)) {
    lua_pushvalue(L, 1);
    lua_call(L, 1, 1);
    return 1;
  }
  lua_pop(L, 1);

  lua_getfield(L, lua_upvalueindex(1), lua_tostring(L, 2));
  return 1;
}

inline int class_newindex(lua_State *L) {
  lua_getfield(L, lua_upvalueindex(1), lua_tostring(L, 2));
  if (lua_iscfunction(L, -1)) {
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 3);
    lua_call(L, 2, 0);
    return 0;
  }

  lua_pop(L, 1);
  return luaL_error(L, "attempt to assign to unknown property '%s'", lua_tostring(L, 2));
}

template <typename T>
int object_gc(lua_State *L) {
  T *object = static_cast<T *>(lua_touserdata(L, 1));
  if (object) {
    object->~T();
  }
  return 0;
}

template <typename Signature>
struct constructor_signature_traits;

template <typename R, typename... Args>
struct constructor_signature_traits<R(Args...)> {
  using args = std::tuple<Args...>;
  static constexpr size_t arity = sizeof...(Args);
};

template <typename T>
bool lua_arg_matches(lua_State *L, int index) {
  using D = decay_arg_t<T>;
  if constexpr (std::is_same_v<D, bool>) {
    return lua_isboolean(L, index) != 0;
  } else if constexpr (std::is_integral_v<D>) {
    return lua_isinteger(L, index) != 0;
  } else if constexpr (std::is_floating_point_v<D>) {
    return lua_isnumber(L, index) != 0;
  } else if constexpr (std::is_same_v<D, const char *> || std::is_same_v<D, char *> ||
                       std::is_same_v<D, std::string> || std::is_same_v<D, std::string_view>) {
    return lua_isstring(L, index) != 0;
  } else if constexpr (std::is_pointer_v<D>) {
    return lua_isuserdata(L, index) != 0 || lua_islightuserdata(L, index) != 0;
  } else {
    return false;
  }
}

template <typename Tuple, size_t... I>
bool all_lua_args_match(lua_State *L, std::index_sequence<I...>) {
  return (lua_arg_matches<std::tuple_element_t<I, Tuple>>(L, I + 1) && ...);
}

template <typename T, typename Tuple, size_t... I>
void construct_from_tuple(lua_State *L, void *memory, std::index_sequence<I...>) {
  new (memory) T(stack_get<decay_arg_t<std::tuple_element_t<I, Tuple>>>(L, I + 1)...);
}

template <typename T, typename Signature>
bool try_construct(lua_State *L, const char *class_name) {
  using traits = constructor_signature_traits<Signature>;
  using args = typename traits::args;
  if (lua_gettop(L) != static_cast<int>(traits::arity)) {
    return false;
  }
  if (!all_lua_args_match<args>(L, std::make_index_sequence<traits::arity>{})) {
    return false;
  }

  void *memory = lua_newuserdata(L, sizeof(T));
  try {
    construct_from_tuple<T, args>(L, memory, std::make_index_sequence<traits::arity>{});
  } catch (const std::exception &e) {
    lua_pop(L, 1);
    luaL_error(L, "C++ exception in constructor: %s", e.what());
    return false;
  } catch (...) {
    lua_pop(L, 1);
    luaL_error(L, "C++ exception in constructor");
    return false;
  }

  luaL_setmetatable(L, class_name);
  return true;
}

template <typename T>
bool try_construct_dispatch(lua_State *L, const char *class_name) {
  (void)L;
  (void)class_name;
  return false;
}

template <typename T, typename Head, typename... Tail>
bool try_construct_dispatch(lua_State *L, const char *class_name) {
  if (try_construct<T, Head>(L, class_name)) {
    return true;
  }
  return try_construct_dispatch<T, Tail...>(L, class_name);
}

template <typename T, typename... Signatures>
int constructor_dispatcher(lua_State *L) {
  const char *class_name = lua_tostring(L, lua_upvalueindex(1));
  if (try_construct_dispatch<T, Signatures...>(L, class_name)) {
    return 1;
  }
  return luaL_error(L, "no matching constructor for %s", class_name);
}

template <typename T, typename... Signatures>
void register_class(lua_State *L, const char *name) {
  luaL_newmetatable(L, name);
  lua_pushstring(L, name);
  lua_setfield(L, -2, "__name");
  push_type_id(L, type_id_of<T>());
  lua_setfield(L, -2, "__cpp_typeid");
  lua_newtable(L);
  lua_setfield(L, -2, "__cpp_bases");
  lua_pushcclosure(L, &object_gc<T>, 0);
  lua_setfield(L, -2, "__gc");

  lua_newtable(L);
  lua_newtable(L);

  lua_pushvalue(L, -2);
  lua_setfield(L, -4, "__property_getters");
  lua_pushvalue(L, -1);
  lua_setfield(L, -4, "__property_setters");

  lua_pushvalue(L, -3);
  lua_pushvalue(L, -3);
  lua_pushcclosure(L, &class_index, 2);
  lua_setfield(L, -4, "__index");

  lua_pushvalue(L, -1);
  lua_pushcclosure(L, &class_newindex, 1);
  lua_setfield(L, -4, "__newindex");

  lua_pop(L, 3);

  lua_newtable(L);
  lua_pushstring(L, name);
  lua_pushcclosure(L, &constructor_dispatcher<T, Signatures...>, 1);
  lua_setfield(L, -2, "new");
  lua_setglobal(L, name);
}

template <typename... Signatures>
struct constructors {};

template <typename Param>
struct normalize_constructor_signature {
  using type = void(Param);
};

template <typename R, typename... Args>
struct normalize_constructor_signature<R(Args...)> {
  using type = R(Args...);
};

template <typename T, typename Ctors>
struct class_ctor_dispatcher;

template <typename T, typename... Params>
struct class_ctor_dispatcher<T, constructors<Params...>> {
  static void apply(lua_State *L, const char *name) {
    register_class<T, typename normalize_constructor_signature<Params>::type...>(L, name);
  }
};

template <typename T, typename Ctors>
void register_class_ctors(lua_State *L, const char *name) {
  class_ctor_dispatcher<T, Ctors>::apply(L, name);
}

inline void create_environment(lua_State *L, const char *name,
                               const char *parent_name = nullptr,
                               bool allow_global_fallback = true) {
  lua_newtable(L);
  lua_newtable(L);
  if (parent_name && parent_name[0] != '\0') {
    lua_getglobal(L, parent_name);
  } else if (allow_global_fallback) {
    lua_getglobal(L, "_G");
  } else {
    lua_newtable(L);
  }
  lua_setfield(L, -2, "__index");
  lua_setmetatable(L, -2);
  lua_setglobal(L, name);
}

inline void create_isolated_environment(lua_State *L, const char *name,
                                        const char *parent_name = nullptr) {
  create_environment(L, name, parent_name, false);
}

inline int run_script_in_environment(lua_State *L, const char *environment_name,
                                     const char *code) {
  const int load_status = luaL_loadstring(L, code);
  if (load_status != 0) {
    return load_status;
  }

  lua_getglobal(L, environment_name);
  if (!lua_istable(L, -1)) {
    lua_pop(L, 1);
    return LUA_ERRRUN;
  }
  lua_setupvalue(L, -2, 1);
  return lua_pcall(L, 0, LUA_MULTRET, 0);
}

template <typename T>
T *shared_userdata_get(lua_State *L, int index) {
  auto *storage = static_cast<std::shared_ptr<T> *>(lua_touserdata(L, index));
  return storage ? storage->get() : nullptr;
}

template <typename T>
int shared_object_gc(lua_State *L) {
  auto *storage = static_cast<std::shared_ptr<T> *>(lua_touserdata(L, 1));
  if (storage) {
    storage->~shared_ptr<T>();
  }
  return 0;
}

template <typename T, typename Tuple, size_t... I>
std::shared_ptr<T> make_shared_from_tuple(lua_State *L, std::index_sequence<I...>) {
  return std::make_shared<T>(
      stack_get<decay_arg_t<std::tuple_element_t<I, Tuple>>>(L, I + 1)...);
}

template <typename T, typename Signature>
bool try_shared_construct(lua_State *L, const char *class_name) {
  using traits = constructor_signature_traits<Signature>;
  using args = typename traits::args;
  if (lua_gettop(L) != static_cast<int>(traits::arity)) {
    return false;
  }
  if (!all_lua_args_match<args>(L, std::make_index_sequence<traits::arity>{})) {
    return false;
  }

  void *memory = lua_newuserdata(L, sizeof(std::shared_ptr<T>));
  try {
    new (memory) std::shared_ptr<T>(
        make_shared_from_tuple<T, args>(L, std::make_index_sequence<traits::arity>{}));
  } catch (const std::exception &e) {
    lua_pop(L, 1);
    luaL_error(L, "C++ exception in shared constructor: %s", e.what());
    return false;
  } catch (...) {
    lua_pop(L, 1);
    luaL_error(L, "C++ exception in shared constructor");
    return false;
  }

  luaL_setmetatable(L, class_name);
  return true;
}

template <typename T>
bool try_shared_construct_dispatch(lua_State *L, const char *class_name) {
  (void)L;
  (void)class_name;
  return false;
}

template <typename T, typename Head, typename... Tail>
bool try_shared_construct_dispatch(lua_State *L, const char *class_name) {
  if (try_shared_construct<T, Head>(L, class_name)) {
    return true;
  }
  return try_shared_construct_dispatch<T, Tail...>(L, class_name);
}

template <typename T, typename... Signatures>
int shared_constructor_dispatcher(lua_State *L) {
  const char *class_name = lua_tostring(L, lua_upvalueindex(1));
  if (try_shared_construct_dispatch<T, Signatures...>(L, class_name)) {
    return 1;
  }
  return luaL_error(L, "no matching shared constructor for %s", class_name);
}

template <typename T, typename... Signatures>
void register_shared_class(lua_State *L, const char *name) {
  luaL_newmetatable(L, name);
  lua_pushstring(L, name);
  lua_setfield(L, -2, "__name");
  push_type_id(L, type_id_of<T>());
  lua_setfield(L, -2, "__cpp_typeid");
  lua_newtable(L);
  lua_setfield(L, -2, "__cpp_bases");
  lua_pushcclosure(L, &shared_object_gc<T>, 0);
  lua_setfield(L, -2, "__gc");

  lua_newtable(L);
  lua_newtable(L);

  lua_pushvalue(L, -2);
  lua_setfield(L, -4, "__property_getters");
  lua_pushvalue(L, -1);
  lua_setfield(L, -4, "__property_setters");

  lua_pushvalue(L, -3);
  lua_pushvalue(L, -3);
  lua_pushcclosure(L, &class_index, 2);
  lua_setfield(L, -4, "__index");

  lua_pushvalue(L, -1);
  lua_pushcclosure(L, &class_newindex, 1);
  lua_setfield(L, -4, "__newindex");

  lua_pop(L, 3);

  lua_newtable(L);
  lua_pushstring(L, name);
  lua_pushcclosure(L, &shared_constructor_dispatcher<T, Signatures...>, 1);
  lua_setfield(L, -2, "new");
  lua_setglobal(L, name);
}

template <typename T, auto Method>
int shared_member_function_trampoline(lua_State *L) {
  try {
    using traits = member_function_traits<decltype(Method)>;
    static_assert(std::is_base_of_v<typename traits::class_type, T>,
                  "cpp_lua_bind: shared class type must derive from method owner");
    T *self = shared_userdata_get<T>(L, 1);
    if (!self) {
      return luaL_error(L, "method called on null shared userdata");
    }
    return invoke_member_function<T, Method>(L, self,
                                             std::make_index_sequence<traits::arity>{});
  } catch (const std::exception &e) {
    return luaL_error(L, "C++ exception in shared method: %s", e.what());
  } catch (...) {
    return luaL_error(L, "C++ exception in shared method");
  }
}

template <typename T, auto Member>
int shared_property_getter(lua_State *L) {
  using traits = member_object_traits<decltype(Member)>;
  static_assert(std::is_base_of_v<typename traits::class_type, T>,
                "cpp_lua_bind: shared class type must derive from member owner");
  T *self = shared_userdata_get<T>(L, 1);
  if (!self) {
    return luaL_error(L, "property accessed on null shared userdata");
  }
  push_value(L, self->*Member);
  return 1;
}

template <typename T, auto Member>
int shared_property_setter(lua_State *L) {
  using traits = member_object_traits<decltype(Member)>;
  static_assert(std::is_base_of_v<typename traits::class_type, T>,
                "cpp_lua_bind: shared class type must derive from member owner");
  T *self = shared_userdata_get<T>(L, 1);
  if (!self) {
    return luaL_error(L, "property assigned on null shared userdata");
  }
  self->*Member = stack_get<typename traits::value_type>(L, 2);
  return 0;
}

template <typename T, auto Member>
void bind_shared_property(lua_State *L, const char *class_name, const char *member_name) {
  luaL_getmetatable(L, class_name);

  lua_getfield(L, -1, "__property_getters");
  lua_pushcclosure(L, &shared_property_getter<T, Member>, 0);
  lua_setfield(L, -2, member_name);
  lua_pop(L, 1);

  lua_getfield(L, -1, "__property_setters");
  lua_pushcclosure(L, &shared_property_setter<T, Member>, 0);
  lua_setfield(L, -2, member_name);
  lua_pop(L, 1);

  lua_pop(L, 1);
}

template <typename T, auto Member>
void bind_shared_member(lua_State *L, const char *class_name, const char *member_name) {
  if constexpr (std::is_member_function_pointer_v<decltype(Member)>) {
    luaL_getmetatable(L, class_name);
    lua_pushcclosure(L, &shared_member_function_trampoline<T, Member>, 0);
    lua_setfield(L, -2, member_name);
    lua_pop(L, 1);
  } else {
    bind_shared_property<T, Member>(L, class_name, member_name);
  }
}

template <typename T, typename Ctors>
struct shared_class_ctor_dispatcher;

template <typename T, typename... Params>
struct shared_class_ctor_dispatcher<T, constructors<Params...>> {
  static void apply(lua_State *L, const char *name) {
    register_shared_class<T, typename normalize_constructor_signature<Params>::type...>(L, name);
  }
};

template <typename T, typename Ctors>
void register_shared_class_ctors(lua_State *L, const char *name) {
  shared_class_ctor_dispatcher<T, Ctors>::apply(L, name);
}

} // namespace cpp_lua_bind

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#define CPP_LUA_BIND_FUNCTION(L, name) ::cpp_lua_bind::bind_function<&(name)>(L, #name)

#define CPP_LUA_BIND_FUNCTION_1(L, a) CPP_LUA_BIND_FUNCTION(L, a)
#define CPP_LUA_BIND_FUNCTION_2(L, a, b) CPP_LUA_BIND_FUNCTION_1(L, a); CPP_LUA_BIND_FUNCTION(L, b)
#define CPP_LUA_BIND_FUNCTION_3(L, a, b, c)                                                      \
  CPP_LUA_BIND_FUNCTION_2(L, a, b); CPP_LUA_BIND_FUNCTION(L, c)
#define CPP_LUA_BIND_FUNCTION_4(L, a, b, c, d)                                                   \
  CPP_LUA_BIND_FUNCTION_3(L, a, b, c); CPP_LUA_BIND_FUNCTION(L, d)
#define CPP_LUA_BIND_FUNCTION_5(L, a, b, c, d, e)                                                \
  CPP_LUA_BIND_FUNCTION_4(L, a, b, c, d); CPP_LUA_BIND_FUNCTION(L, e)
#define CPP_LUA_BIND_FUNCTION_6(L, a, b, c, d, e, f)                                             \
  CPP_LUA_BIND_FUNCTION_5(L, a, b, c, d, e); CPP_LUA_BIND_FUNCTION(L, f)
#define CPP_LUA_BIND_FUNCTION_7(L, a, b, c, d, e, f, g)                                          \
  CPP_LUA_BIND_FUNCTION_6(L, a, b, c, d, e, f); CPP_LUA_BIND_FUNCTION(L, g)
#define CPP_LUA_BIND_FUNCTION_8(L, a, b, c, d, e, f, g, h)                                       \
  CPP_LUA_BIND_FUNCTION_7(L, a, b, c, d, e, f, g); CPP_LUA_BIND_FUNCTION(L, h)
#define CPP_LUA_BIND_FUNCTION_9(L, a, b, c, d, e, f, g, h, i)                                    \
  CPP_LUA_BIND_FUNCTION_8(L, a, b, c, d, e, f, g, h); CPP_LUA_BIND_FUNCTION(L, i)

#define CPP_LUA_BIND_FUNCTIONS(L, ...)                                                          \
  CPP_LUA_EXPAND(CPP_LUA_CONCAT(CPP_LUA_BIND_FUNCTION_, CPP_LUA_COUNT_ARGS(__VA_ARGS__))       \
                     (L, __VA_ARGS__))

#define CPP_LUA_BIND_OVERLOAD(L, name, Signature)                                               \
  ::cpp_lua_bind::bind_function<static_cast<Signature>(&(name))>(L, #name)

#define CPP_LUA_BIND_OVERLOAD_SET(L, name, ...)                                                 \
  ::cpp_lua_bind::bind_function_overloads<__VA_ARGS__>(L, #name)

#define CPP_LUA_OVERLOAD_CAST(name, signature)                                                  \
  static_cast<::cpp_lua_bind::function_pointer_t<signature> >(&(name))
#define CPP_LUA_OVERLOAD_CAST_1(name, a) CPP_LUA_OVERLOAD_CAST(name, a)
#define CPP_LUA_OVERLOAD_CAST_2(name, a, b)                                                      \
  CPP_LUA_OVERLOAD_CAST_1(name, a), CPP_LUA_OVERLOAD_CAST(name, b)
#define CPP_LUA_OVERLOAD_CAST_3(name, a, b, c)                                                   \
  CPP_LUA_OVERLOAD_CAST_2(name, a, b), CPP_LUA_OVERLOAD_CAST(name, c)
#define CPP_LUA_OVERLOAD_CAST_4(name, a, b, c, d)                                                \
  CPP_LUA_OVERLOAD_CAST_3(name, a, b, c), CPP_LUA_OVERLOAD_CAST(name, d)
#define CPP_LUA_OVERLOAD_CAST_5(name, a, b, c, d, e)                                             \
  CPP_LUA_OVERLOAD_CAST_4(name, a, b, c, d), CPP_LUA_OVERLOAD_CAST(name, e)
#define CPP_LUA_OVERLOAD_CAST_6(name, a, b, c, d, e, f)                                          \
  CPP_LUA_OVERLOAD_CAST_5(name, a, b, c, d, e), CPP_LUA_OVERLOAD_CAST(name, f)
#define CPP_LUA_OVERLOAD_CAST_7(name, a, b, c, d, e, f, g)                                       \
  CPP_LUA_OVERLOAD_CAST_6(name, a, b, c, d, e, f), CPP_LUA_OVERLOAD_CAST(name, g)
#define CPP_LUA_OVERLOAD_CAST_8(name, a, b, c, d, e, f, g, h)                                    \
  CPP_LUA_OVERLOAD_CAST_7(name, a, b, c, d, e, f, g), CPP_LUA_OVERLOAD_CAST(name, h)
#define CPP_LUA_OVERLOAD_CAST_9(name, a, b, c, d, e, f, g, h, i)                                 \
  CPP_LUA_OVERLOAD_CAST_8(name, a, b, c, d, e, f, g, h), CPP_LUA_OVERLOAD_CAST(name, i)

#define CPP_LUA_OVERLOAD_CASTS(name, ...)                                                        \
  CPP_LUA_EXPAND(CPP_LUA_CONCAT(CPP_LUA_OVERLOAD_CAST_, CPP_LUA_COUNT_ARGS(__VA_ARGS__))         \
                     (name, __VA_ARGS__))

#define CPP_LUA_BIND_OVERLOAD_SIGNATURES(L, name, ...)                                           \
  ::cpp_lua_bind::bind_function_overloads<CPP_LUA_OVERLOAD_CASTS(name, __VA_ARGS__)>(L, #name)

#define CPP_LUA_BIND_CALLABLE(L, name, callable)                                                 \
  ::cpp_lua_bind::bind_callable(L, #name, callable)

#define CPP_LUA_BIND_METHOD(L, Class, method)                                                    \
  ::cpp_lua_bind::bind_member<Class, &Class::method>(L, #Class, #method)

#define CPP_LUA_BIND_METHOD_1(L, Class, a) CPP_LUA_BIND_METHOD(L, Class, a)
#define CPP_LUA_BIND_METHOD_2(L, Class, a, b) CPP_LUA_BIND_METHOD_1(L, Class, a); CPP_LUA_BIND_METHOD(L, Class, b)
#define CPP_LUA_BIND_METHOD_3(L, Class, a, b, c)                                                 \
  CPP_LUA_BIND_METHOD_2(L, Class, a, b); CPP_LUA_BIND_METHOD(L, Class, c)
#define CPP_LUA_BIND_METHOD_4(L, Class, a, b, c, d)                                              \
  CPP_LUA_BIND_METHOD_3(L, Class, a, b, c); CPP_LUA_BIND_METHOD(L, Class, d)
#define CPP_LUA_BIND_METHOD_5(L, Class, a, b, c, d, e)                                           \
  CPP_LUA_BIND_METHOD_4(L, Class, a, b, c, d); CPP_LUA_BIND_METHOD(L, Class, e)
#define CPP_LUA_BIND_METHOD_6(L, Class, a, b, c, d, e, f)                                        \
  CPP_LUA_BIND_METHOD_5(L, Class, a, b, c, d, e); CPP_LUA_BIND_METHOD(L, Class, f)
#define CPP_LUA_BIND_METHOD_7(L, Class, a, b, c, d, e, f, g)                                     \
  CPP_LUA_BIND_METHOD_6(L, Class, a, b, c, d, e, f); CPP_LUA_BIND_METHOD(L, Class, g)
#define CPP_LUA_BIND_METHOD_8(L, Class, a, b, c, d, e, f, g, h)                                  \
  CPP_LUA_BIND_METHOD_7(L, Class, a, b, c, d, e, f, g); CPP_LUA_BIND_METHOD(L, Class, h)
#define CPP_LUA_BIND_METHOD_9(L, Class, a, b, c, d, e, f, g, h, i)                               \
  CPP_LUA_BIND_METHOD_8(L, Class, a, b, c, d, e, f, g, h); CPP_LUA_BIND_METHOD(L, Class, i)

#define CPP_LUA_BIND_METHODS(L, Class, ...)                                                     \
  CPP_LUA_EXPAND(CPP_LUA_CONCAT(CPP_LUA_BIND_METHOD_, CPP_LUA_COUNT_ARGS(__VA_ARGS__))          \
                     (L, Class, __VA_ARGS__))

#define CPP_LUA_CTORS(...) ::cpp_lua_bind::constructors<__VA_ARGS__>

#define CPP_LUA_BIND_CLASS(L, Class, ...)                                                        \
  do {                                                                                          \
    ::cpp_lua_bind::register_class<Class, void()>(L, #Class);                                   \
    CPP_LUA_BIND_METHODS(L, Class, __VA_ARGS__);                                                \
  } while (0)

#define CPP_LUA_BIND_CLASS_CTOR(L, Class, Ctors, ...)                                            \
  do {                                                                                          \
    ::cpp_lua_bind::register_class_ctors<Class, Ctors>(L, #Class);                              \
    CPP_LUA_BIND_METHODS(L, Class, __VA_ARGS__);                                                \
  } while (0)


#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cpp_lua_reflection {

template <size_t N>
constexpr std::array<std::string_view, N> split_names(std::string_view text) {
  std::array<std::string_view, N> names{};
  size_t begin = 0;

  for (size_t i = 0; i < N; ++i) {
    const size_t comma = text.find(',', begin);
    const size_t end = comma == std::string_view::npos ? text.size() : comma;
    std::string_view token = text.substr(begin, end - begin);

    while (!token.empty() && token.front() == ' ') {
      token.remove_prefix(1);
    }
    while (!token.empty() && token.back() == ' ') {
      token.remove_suffix(1);
    }

    names[i] = token;
    if (comma == std::string_view::npos) {
      break;
    }
    begin = comma + 1;
  }

  return names;
}

template <typename T, typename = void>
struct is_reflected : std::false_type {};

template <typename T>
struct is_reflected<T,
                    std::void_t<decltype(cpp_lua_reflect_members_adl(std::declval<T>())
                                             .names())>> : std::true_type {};

template <typename T>
inline constexpr bool is_reflected_v = is_reflected<T>::value;

template <typename T>
using metadata_t = decltype(cpp_lua_reflect_members_adl(std::declval<T>()));

template <typename T, typename F>
void for_each_member_impl(F &&callback, std::index_sequence<>);

template <typename T, typename F, size_t... I>
void for_each_member_impl(F &&callback, std::index_sequence<I...>) {
  using metadata = metadata_t<T>;
  constexpr auto members = metadata::members();
  constexpr auto names = metadata::names();
  (std::forward<F>(callback)(std::get<I>(members), names[I],
                             std::integral_constant<size_t, I>{}),
   ...);
}

template <typename T, typename F>
void for_each_member(F &&callback) {
  using metadata = metadata_t<T>;
  for_each_member_impl<T>(std::forward<F>(callback),
                          std::make_index_sequence<metadata::size()>{});
}

template <typename T>
void to_lua(lua_State *L, const T &object) {
  static_assert(is_reflected_v<T>,
                "cpp_lua_reflection: T is not declared with CPP_LUA_REFLECT");

  lua_newtable(L);
  for_each_member<T>([&](auto member, std::string_view name, auto) {
    using field_type = std::decay_t<decltype(object.*member)>;

    lua_pushlstring(L, name.data(), name.size());
    if constexpr (is_reflected_v<field_type>) {
      to_lua(L, object.*member);
    } else {
      cpp_lua_bind::push_value(L, object.*member);
    }
    lua_settable(L, -3);
  });
}

template <typename T>
void from_lua(lua_State *L, int index, T &object) {
  static_assert(is_reflected_v<T>,
                "cpp_lua_reflection: T is not declared with CPP_LUA_REFLECT");

  const int absolute_index = index > 0 ? index : lua_gettop(L) + index + 1;
  for_each_member<T>([&](auto member, std::string_view name, auto) {
    using field_type = std::decay_t<decltype(object.*member)>;

    lua_getfield(L, absolute_index, std::string(name).c_str());
    if constexpr (is_reflected_v<field_type>) {
      from_lua(L, -1, object.*member);
    } else {
      object.*member = cpp_lua_bind::stack_get<field_type>(L, -1);
    }
    lua_pop(L, 1);
  });
}

template <typename T, size_t... I>
void bind_reflected_properties_impl(lua_State *L, const char *class_name,
                                    std::index_sequence<I...>) {
  using metadata = metadata_t<T>;
  constexpr auto members = metadata::members();
  constexpr auto names = metadata::names();

  (cpp_lua_bind::bind_property<T, std::get<I>(members)>(
       L, class_name, std::string(names[I]).c_str()),
   ...);
}

template <typename T>
void bind_reflected_properties(lua_State *L, const char *class_name) {
  using metadata = metadata_t<T>;
  bind_reflected_properties_impl<T>(L, class_name,
                                    std::make_index_sequence<metadata::size()>{});
}

template <typename Derived, typename Base, size_t... I>
void bind_reflected_base_impl(lua_State *L, const char *class_name,
                              std::index_sequence<I...>) {
  using metadata = metadata_t<Base>;
  constexpr auto members = metadata::members();
  constexpr auto names = metadata::names();

  (cpp_lua_bind::bind_member<Derived, std::get<I>(members)>(
       L, class_name, std::string(names[I]).c_str()),
   ...);
}

template <typename Derived, typename Base>
void bind_reflected_base(lua_State *L) {
  using metadata = metadata_t<Derived>;
  using base_metadata = metadata_t<Base>;
  const std::string class_name(metadata::name());
  bind_reflected_base_impl<Derived, Base>(
      L, class_name.c_str(), std::make_index_sequence<base_metadata::size()>{});
  cpp_lua_bind::register_base_relationship<Derived, Base>();
}

template <typename Derived, typename... Bases>
void bind_reflected_bases(lua_State *L) {
  (bind_reflected_base<Derived, Bases>(L), ...);
}

template <typename T>
void bind_reflected_class(lua_State *L) {
  using metadata = metadata_t<T>;
  const std::string class_name(metadata::name());
  cpp_lua_bind::register_class<T, void()>(L, class_name.c_str());
  bind_reflected_properties<T>(L, class_name.c_str());
}

template <typename T, typename Ctors>
void bind_reflected_class_ctors(lua_State *L) {
  using metadata = metadata_t<T>;
  const std::string class_name(metadata::name());
  cpp_lua_bind::register_class_ctors<T, Ctors>(L, class_name.c_str());
  bind_reflected_properties<T>(L, class_name.c_str());
}

template <typename T, size_t... I>
void bind_shared_reflected_properties_impl(lua_State *L, const char *class_name,
                                           std::index_sequence<I...>) {
  using metadata = metadata_t<T>;
  constexpr auto members = metadata::members();
  constexpr auto names = metadata::names();

  (cpp_lua_bind::bind_shared_member<T, std::get<I>(members)>(
       L, class_name, std::string(names[I]).c_str()),
   ...);
}

template <typename T>
void bind_shared_reflected_properties(lua_State *L, const char *class_name) {
  using metadata = metadata_t<T>;
  bind_shared_reflected_properties_impl<T>(
      L, class_name, std::make_index_sequence<metadata::size()>{});
}

template <typename T>
void bind_shared_reflected_class(lua_State *L) {
  using metadata = metadata_t<T>;
  const std::string class_name(metadata::name());
  cpp_lua_bind::register_shared_class<T, void()>(L, class_name.c_str());
  bind_shared_reflected_properties<T>(L, class_name.c_str());
}

template <typename T, typename Ctors>
void bind_shared_reflected_class_ctors(lua_State *L) {
  using metadata = metadata_t<T>;
  const std::string class_name(metadata::name());
  cpp_lua_bind::register_shared_class_ctors<T, Ctors>(L, class_name.c_str());
  bind_shared_reflected_properties<T>(L, class_name.c_str());
}

} // namespace cpp_lua_reflection

#define CPP_LUA_REFLECT_DETAIL_MEMBER(Type, field) &Type::field
#define CPP_LUA_REFLECT_DETAIL_LIST_1(Type, a) CPP_LUA_REFLECT_DETAIL_MEMBER(Type, a)
#define CPP_LUA_REFLECT_DETAIL_LIST_2(Type, a, b)                                              \
  CPP_LUA_REFLECT_DETAIL_LIST_1(Type, a), CPP_LUA_REFLECT_DETAIL_MEMBER(Type, b)
#define CPP_LUA_REFLECT_DETAIL_LIST_3(Type, a, b, c)                                           \
  CPP_LUA_REFLECT_DETAIL_LIST_2(Type, a, b), CPP_LUA_REFLECT_DETAIL_MEMBER(Type, c)
#define CPP_LUA_REFLECT_DETAIL_LIST_4(Type, a, b, c, d)                                        \
  CPP_LUA_REFLECT_DETAIL_LIST_3(Type, a, b, c), CPP_LUA_REFLECT_DETAIL_MEMBER(Type, d)
#define CPP_LUA_REFLECT_DETAIL_LIST_5(Type, a, b, c, d, e)                                     \
  CPP_LUA_REFLECT_DETAIL_LIST_4(Type, a, b, c, d), CPP_LUA_REFLECT_DETAIL_MEMBER(Type, e)
#define CPP_LUA_REFLECT_DETAIL_LIST_6(Type, a, b, c, d, e, f)                                  \
  CPP_LUA_REFLECT_DETAIL_LIST_5(Type, a, b, c, d, e), CPP_LUA_REFLECT_DETAIL_MEMBER(Type, f)
#define CPP_LUA_REFLECT_DETAIL_LIST_7(Type, a, b, c, d, e, f, g)                               \
  CPP_LUA_REFLECT_DETAIL_LIST_6(Type, a, b, c, d, e, f),                                       \
      CPP_LUA_REFLECT_DETAIL_MEMBER(Type, g)
#define CPP_LUA_REFLECT_DETAIL_LIST_8(Type, a, b, c, d, e, f, g, h)                            \
  CPP_LUA_REFLECT_DETAIL_LIST_7(Type, a, b, c, d, e, f, g),                                    \
      CPP_LUA_REFLECT_DETAIL_MEMBER(Type, h)
#define CPP_LUA_REFLECT_DETAIL_LIST_9(Type, a, b, c, d, e, f, g, h, i)                         \
  CPP_LUA_REFLECT_DETAIL_LIST_8(Type, a, b, c, d, e, f, g, h),                                 \
      CPP_LUA_REFLECT_DETAIL_MEMBER(Type, i)
#define CPP_LUA_REFLECT_DETAIL_LIST_10(Type, a, b, c, d, e, f, g, h, i, j)                     \
  CPP_LUA_REFLECT_DETAIL_LIST_9(Type, a, b, c, d, e, f, g, h, i),                              \
      CPP_LUA_REFLECT_DETAIL_MEMBER(Type, j)
#define CPP_LUA_REFLECT_DETAIL_LIST_11(Type, a, b, c, d, e, f, g, h, i, j, k)                  \
  CPP_LUA_REFLECT_DETAIL_LIST_10(Type, a, b, c, d, e, f, g, h, i, j),                          \
      CPP_LUA_REFLECT_DETAIL_MEMBER(Type, k)
#define CPP_LUA_REFLECT_DETAIL_LIST_12(Type, a, b, c, d, e, f, g, h, i, j, k, l)               \
  CPP_LUA_REFLECT_DETAIL_LIST_11(Type, a, b, c, d, e, f, g, h, i, j, k),                       \
      CPP_LUA_REFLECT_DETAIL_MEMBER(Type, l)

#define CPP_LUA_REFLECT_DETAIL_MEMBER_LIST(Type, ...)                                          \
  CPP_LUA_EXPAND(                                                                             \
      CPP_LUA_CONCAT(CPP_LUA_REFLECT_DETAIL_LIST_,                                             \
                     CPP_LUA_COUNT_ARGS(__VA_ARGS__))                                         \
          (Type, __VA_ARGS__))

#define CPP_LUA_REFLECT_WITH_NAME(Type, LuaName, ...)                                           \
  inline auto cpp_lua_reflect_members_adl(Type const &) {                                       \
    struct metadata {                                                                          \
      static constexpr auto members() {                                                        \
        return std::make_tuple(CPP_LUA_REFLECT_DETAIL_MEMBER_LIST(Type, __VA_ARGS__));          \
      }                                                                                        \
      static constexpr std::string_view name() { return LuaName; }                              \
      static constexpr std::string_view fields() { return #__VA_ARGS__; }                      \
      static constexpr size_t size() { return std::tuple_size_v<decltype(members())>; }         \
      static constexpr auto names() {                                                          \
        return ::cpp_lua_reflection::split_names<size()>(fields());                            \
      }                                                                                        \
    };                                                                                         \
    return metadata{};                                                                         \
  }

#define CPP_LUA_REFLECT(Type, ...) CPP_LUA_REFLECT_WITH_NAME(Type, #Type, __VA_ARGS__)

#define CPP_LUA_BIND_REFLECTED_CLASS(L, Type)                                                  \
  ::cpp_lua_reflection::bind_reflected_class<Type>(L)

#define CPP_LUA_BIND_REFLECTED_CLASS_CTOR(L, Type, Ctors)                                      \
  ::cpp_lua_reflection::bind_reflected_class_ctors<Type, Ctors>(L)

#define CPP_LUA_BIND_SHARED_REFLECTED_CLASS(L, Type)                                            \
  ::cpp_lua_reflection::bind_shared_reflected_class<Type>(L)

#define CPP_LUA_BIND_SHARED_REFLECTED_CLASS_CTOR(L, Type, Ctors)                                \
  ::cpp_lua_reflection::bind_shared_reflected_class_ctors<Type, Ctors>(L)

#define CPP_LUA_BIND_BASE_CLASS(L, Derived, Base)                                               \
  ::cpp_lua_reflection::bind_reflected_base<Derived, Base>(L)

#define CPP_LUA_BIND_BASE_CLASSES(L, Derived, ...)                                              \
  ::cpp_lua_reflection::bind_reflected_bases<Derived, __VA_ARGS__>(L)
