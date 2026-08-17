#include "cpp_lua_bind.hpp"
#include "tinytest.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <variant>
#include <vector>

static int bound_value = 0;
static void set_value(int value) { bound_value = value; }
static int get_value() { return bound_value; }
static int throwing() { throw std::runtime_error("boom"); }
static std::tuple<int, std::string> get_tuple_value() { return {7, "seven"}; }
static std::pair<int, int> get_pair_value() { return {3, 4}; }
static int overloaded_add(int a, int b) { return a + b; }
static double overloaded_add(double a, double b) { return a + b; }

struct counter {
  counter() = default;
  explicit counter(int start) : value(start) {}
  int add(int delta) {
    value += delta;
    return value;
  }
  int value = 0;
};

struct counter_ctor {
  counter_ctor() = default;
  explicit counter_ctor(int start) : value(start) {}
  int add(int delta) {
    value += delta;
    return value;
  }
  int value = 0;
};

struct point {
  point() = default;
  point(float x, float y) : x(x), y(y) {}
  float get_x() const { return x; }
  float get_y() const { return y; }
  void set_x(float value) { x = value; }
  void set_y(float value) { y = value; }
  float x = 0.0f;
  float y = 0.0f;
};

struct reflected_vector {
  float x = 1.0f;
  float y = 2.0f;
};

CPP_LUA_REFLECT_WITH_NAME(reflected_vector, "vector", x, y)

struct reflected_base {
  int base_value = 5;
  int get_base() const { return base_value; }
};

struct reflected_derived : reflected_base {
  int derived_value = 9;
};

struct reflected_base2 {
  int extra_value = 11;
  int get_extra() const { return extra_value; }
};

struct reflected_multi : reflected_base, reflected_base2 {
  int multi_value = 13;
};

struct shared_point {
  shared_point() = default;
  shared_point(float x, float y) : x(x), y(y) {}
  float get_x() const { return x; }
  float get_y() const { return y; }
  void set_x(float value) { x = value; }
  void set_y(float value) { y = value; }
  float x = 0.0f;
  float y = 0.0f;
};

CPP_LUA_REFLECT(reflected_base, base_value, get_base)
CPP_LUA_REFLECT(reflected_base2, extra_value, get_extra)
CPP_LUA_REFLECT(reflected_derived, derived_value)
CPP_LUA_REFLECT(reflected_multi, multi_value)
CPP_LUA_REFLECT(shared_point, get_x, get_y, set_x, set_y, x, y)

suite("cpp lua bind") {
  static lua_State *L;

  before_each() {
    bound_value = 0;
    L = luaL_newstate();
    check_not_null(L);
    luaL_openlibs(L);
  }

  after_each() {
    if (L) {
      lua_close(L);
      L = nullptr;
    }
  }

  it("binds a single function") {
    CPP_LUA_BIND_FUNCTION(L, get_value);

    check_int_eq(luaL_dostring(L, "result = get_value()"), 0);
    lua_getglobal(L, "result");
    check_int_eq(lua_tointeger(L, -1), 0);
    lua_pop(L, 1);
  }

  it("binds multiple functions") {
    CPP_LUA_BIND_FUNCTIONS(L, set_value, get_value);

    check_int_eq(luaL_dostring(L, "set_value(10); result = get_value()"), 0);
    lua_getglobal(L, "result");
    check_int_eq(lua_tointeger(L, -1), 10);
    lua_pop(L, 1);
  }

  it("converts C++ exceptions into Lua errors") {
    CPP_LUA_BIND_FUNCTION(L, throwing);

    check_int_ne(luaL_dostring(L, "throwing()"), 0);
  }

  it("binds a class with a default constructor") {
    CPP_LUA_BIND_CLASS(L, counter, add);

    lua_getglobal(L, "counter");
    check_true(lua_istable(L, -1));
    lua_pop(L, 1);

    check_int_eq(luaL_dostring(L, "c = counter.new(); result = c:add(5)"), 0);
    lua_getglobal(L, "result");
    check_int_eq(lua_tointeger(L, -1), 5);
    lua_pop(L, 1);
  }

  it("binds a class with a custom constructor") {
    CPP_LUA_BIND_CLASS_CTOR(L, counter_ctor, CPP_LUA_CTORS(int), add);

    lua_getglobal(L, "counter_ctor");
    check_true(lua_istable(L, -1));
    lua_pop(L, 1);

    check_int_eq(luaL_dostring(L, "c = counter_ctor.new(7); result = c:add(3)"), 0);
    lua_getglobal(L, "result");
    check_int_eq(lua_tointeger(L, -1), 10);
    lua_pop(L, 1);
  }

  it("binds member variables and multiple constructors") {
    CPP_LUA_BIND_CLASS_CTOR(
        L, point, CPP_LUA_CTORS(void(), void(float, float)), get_x, get_y, set_x, set_y, x, y);

    check_int_eq(luaL_dostring(
                     L,
                     "p = point.new(10, 20);"
                     "assert(p.x == 10 and p.y == 20);"
                     "p.x = 30;"
                     "assert(p.x == 30 and p.y == 20);"
                     "q = point.new();"
                     "assert(q.x == 0 and q.y == 0);"),
                 0);
  }

  it("serializes reflected members to and from Lua tables") {
    reflected_vector value;
    value.x = 4.0f;
    value.y = 8.0f;

    cpp_lua_reflection::to_lua(L, value);
    lua_setglobal(L, "source");

    check_int_eq(luaL_dostring(L, "assert(source.x == 4 and source.y == 8);"
                                 "target = { x = 10, y = 20 }"),
                 0);

    lua_getglobal(L, "target");
    reflected_vector loaded;
    cpp_lua_reflection::from_lua(L, -1, loaded);
    lua_pop(L, 1);

    check_float_eq(loaded.x, 10.0f, 0.001f);
    check_float_eq(loaded.y, 20.0f, 0.001f);
  }

  it("binds all reflected fields as Lua properties") {
    CPP_LUA_BIND_REFLECTED_CLASS(L, reflected_vector);

    check_int_eq(luaL_dostring(
                     L,
                     "v = vector.new();"
                     "assert(v.x == 1 and v.y == 2);"
                     "v.x = 7;"
                     "assert(v.x == 7 and v.y == 2);"),
                 0);
  }

  it("supports optional, tuple, vector, and map values") {
    std::optional<int> missing;
    std::optional<int> present = 42;
    std::tuple<int, std::string> tuple_value{1, "two"};
    std::vector<int> numbers{1, 2, 3};
    std::map<std::string, int> scores{{"a", 1}, {"b", 2}};

    cpp_lua_bind::push_value(L, missing);
    lua_setglobal(L, "missing");
    cpp_lua_bind::push_value(L, present);
    lua_setglobal(L, "present");
    cpp_lua_bind::push_value(L, tuple_value);
    lua_setglobal(L, "tuple_value");
    cpp_lua_bind::push_value(L, numbers);
    lua_setglobal(L, "numbers");
    cpp_lua_bind::push_value(L, scores);
    lua_setglobal(L, "scores");

    check_int_eq(luaL_dostring(
                     L,
                     "assert(missing == nil);"
                     "assert(present == 42);"
                     "assert(tuple_value[1] == 1 and tuple_value[2] == 'two');"
                     "assert(numbers[1] == 1 and numbers[2] == 2 and numbers[3] == 3);"
                     "assert(scores.a == 1 and scores.b == 2);"),
                 0);

    lua_getglobal(L, "present");
    check_true(cpp_lua_bind::stack_get<std::optional<int>>(L, -1) == 42);
    lua_pop(L, 1);

    lua_getglobal(L, "tuple_value");
    auto loaded_tuple = cpp_lua_bind::stack_get<std::tuple<int, std::string>>(L, -1);
    lua_pop(L, 1);
    check_int_eq(std::get<0>(loaded_tuple), 1);
    check_str_eq(std::get<1>(loaded_tuple).c_str(), "two");

    lua_getglobal(L, "numbers");
    auto loaded_numbers = cpp_lua_bind::stack_get<std::vector<int>>(L, -1);
    lua_pop(L, 1);
    check_size_eq(loaded_numbers.size(), 3);
    check_int_eq(loaded_numbers[2], 3);

    lua_getglobal(L, "scores");
    auto loaded_scores = cpp_lua_bind::stack_get<std::map<std::string, int>>(L, -1);
    lua_pop(L, 1);
    check_int_eq(loaded_scores.at("b"), 2);
  }

  it("supports multiple return values and variant values") {
    CPP_LUA_BIND_FUNCTIONS(L, get_tuple_value, get_pair_value);

    check_int_eq(luaL_dostring(
                     L,
                     "a, b = get_tuple_value();"
                     "c, d = get_pair_value();"
                     "assert(a == 7 and b == 'seven');"
                     "assert(c == 3 and d == 4);"),
                 0);

    std::variant<int, std::string> number = 42;
    std::variant<int, std::string> text = "hello";

    cpp_lua_bind::push_value(L, number);
    lua_setglobal(L, "variant_number");
    cpp_lua_bind::push_value(L, text);
    lua_setglobal(L, "variant_text");

    check_int_eq(luaL_dostring(
                     L,
                     "assert(variant_number == 42);"
                     "assert(variant_text == 'hello');"),
                 0);

    lua_getglobal(L, "variant_number");
    auto loaded = cpp_lua_bind::stack_get<std::variant<int, std::string>>(L, -1);
    lua_pop(L, 1);
    check_true(std::holds_alternative<int>(loaded));
    check_int_eq(std::get<int>(loaded), 42);
  }

  it("binds overloaded function sets") {
    CPP_LUA_BIND_OVERLOAD_SIGNATURES(
        L, overloaded_add, int(int, int), double(double, double));

    check_int_eq(luaL_dostring(
                     L,
                     "assert(overloaded_add(2, 3) == 5);"
                     "assert(overloaded_add(2.5, 3.5) == 6.0);"),
                 0);
  }

  it("binds std::function callables") {
    CPP_LUA_BIND_CALLABLE(
        L, callable_add,
        std::function<int(int, int)>([](int a, int b) { return a + b; }));

    check_int_eq(luaL_dostring(L, "assert(callable_add(2, 3) == 5);"), 0);
  }

  it("inherits reflected base members") {
    CPP_LUA_BIND_REFLECTED_CLASS(L, reflected_derived);
    CPP_LUA_BIND_BASE_CLASS(L, reflected_derived, reflected_base);

    check_int_eq(luaL_dostring(
                     L,
                     "d = reflected_derived.new();"
                     "assert(d.derived_value == 9);"
                     "assert(d.base_value == 5);"
                     "assert(d:get_base() == 5);"
                     "d.base_value = 7;"
                     "assert(d:get_base() == 7);"),
                 0);
  }

  it("runs scripts in a named Lua environment") {
    cpp_lua_bind::create_environment(L, "sandbox");
    check_int_eq(cpp_lua_bind::run_script_in_environment(L, "sandbox", "x = 5"), 0);

    lua_getglobal(L, "sandbox");
    lua_getfield(L, -1, "x");
    check_int_eq(lua_tointeger(L, -1), 5);
    lua_pop(L, 2);

    lua_getglobal(L, "x");
    check_true(lua_isnil(L, -1));
    lua_pop(L, 1);
  }

  it("converts shared_ptr values with shared ownership") {
    auto shared_number = std::make_shared<int>(7);
    cpp_lua_bind::push_value(L, shared_number);
    lua_setglobal(L, "shared_number");

    check_int_eq(luaL_dostring(L, "assert(shared_number == 7);"), 0);

    lua_getglobal(L, "shared_number");
    auto loaded = cpp_lua_bind::stack_get<std::shared_ptr<int>>(L, -1);
    lua_pop(L, 1);
    check_not_null(loaded.get());
    check_int_eq(*loaded, 7);
  }

  it("inherits multiple reflected base members") {
    CPP_LUA_BIND_REFLECTED_CLASS(L, reflected_multi);
    CPP_LUA_BIND_BASE_CLASSES(L, reflected_multi, reflected_base, reflected_base2);

    check_int_eq(luaL_dostring(
                     L,
                     "m = reflected_multi.new();"
                     "assert(m.multi_value == 13);"
                     "assert(m.base_value == 5);"
                     "assert(m.extra_value == 11);"
                     "assert(m:get_base() == 5);"
                     "assert(m:get_extra() == 11);"),
                 0);
  }

  it("supports inherited Lua environments") {
    cpp_lua_bind::create_environment(L, "parent_env");
    cpp_lua_bind::create_environment(L, "child_env", "parent_env");

    check_int_eq(
        cpp_lua_bind::run_script_in_environment(L, "parent_env", "shared_value = 8"), 0);
    check_int_eq(
        cpp_lua_bind::run_script_in_environment(L, "child_env", "x = shared_value + 1"), 0);

    lua_getglobal(L, "child_env");
    lua_getfield(L, -1, "x");
    check_int_eq(lua_tointeger(L, -1), 9);
    lua_pop(L, 2);

    lua_getglobal(L, "shared_value");
    check_true(lua_isnil(L, -1));
    lua_pop(L, 1);
  }

  it("reports the registered C++ type of userdata") {
    CPP_LUA_BIND_REFLECTED_CLASS(L, reflected_vector);
    check_int_eq(luaL_dostring(L, "v = vector.new()"), 0);

    lua_getglobal(L, "v");
    check_true(cpp_lua_bind::is_same_type<reflected_vector>(L, -1));
    check_false(cpp_lua_bind::is_same_type<reflected_base>(L, -1));
    lua_pop(L, 1);
  }

  it("queries base relationships for reflected userdata") {
    CPP_LUA_BIND_REFLECTED_CLASS(L, reflected_multi);
    CPP_LUA_BIND_BASE_CLASSES(L, reflected_multi, reflected_base, reflected_base2);

    check_int_eq(luaL_dostring(L, "m = reflected_multi.new()"), 0);
    lua_getglobal(L, "m");
    check_true(cpp_lua_bind::is_same_type<reflected_multi>(L, -1));
    check_true(cpp_lua_bind::is_instance_of<reflected_base>(L, -1));
    check_true(cpp_lua_bind::is_instance_of<reflected_base2>(L, -1));
    check_false(cpp_lua_bind::is_instance_of<reflected_vector>(L, -1));
    lua_pop(L, 1);
  }

  it("binds shared userdata with methods and properties") {
    CPP_LUA_BIND_SHARED_REFLECTED_CLASS_CTOR(
        L, shared_point, CPP_LUA_CTORS(void(), void(float, float)));

    check_int_eq(luaL_dostring(
                     L,
                     "p = shared_point.new(10, 20);"
                     "assert(p.x == 10 and p.y == 20);"
                     "p:set_y(30);"
                     "assert(p:get_y() == 30);"
                     "p.x = 40;"
                     "assert(p:get_x() == 40);"),
                 0);
  }

  it("isolates environments from global fallback") {
    cpp_lua_bind::create_isolated_environment(L, "isolated");

    check_int_eq(cpp_lua_bind::run_script_in_environment(L, "isolated", "x = 5"), 0);
    check_int_ne(cpp_lua_bind::run_script_in_environment(L, "isolated", "print(x)"), 0);

    lua_getglobal(L, "x");
    check_true(lua_isnil(L, -1));
    lua_pop(L, 1);
  }
}
