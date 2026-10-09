#include <salts/component_plugin.h>
#include <type_traits>

static_assert(std::is_standard_layout<salts_component_plugin_scope>::value,
              "A Component scope remains a C-compatible public record");

int main() {
    salts_component_plugin_scope scope = SALTS_COMPONENT_PLUGIN_SCOPE_INIT;
    return scope.runtime == nullptr &&
                   scope.generation == nullptr &&
                   scope.generation_id == UINT64_C(0) &&
                   scope.owner_address == nullptr && !scope.live
               ? 0
               : 1;
}
