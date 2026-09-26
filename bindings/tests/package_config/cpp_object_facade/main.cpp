#include <salts/bindings/object.hpp>
#include <salts/bindings/lua.hpp>
#include <salts/bindings/quickjs.hpp>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<Salts::Lua::StackGuard>);
static_assert(std::is_move_constructible_v<Salts::QuickJS::Value>);

int main() {
  return 0;
}
