#include <salts/plugin_decl.h>
#include "plugin_generic_graph_fixture.h"

int plugin_generic_graph_probe(plugin_generic_graph_value value) {
    return value.value + 1;
}

#define GENERIC_EXPORTS(X) \
    X(function, plugin_generic_graph_probe, "generic.probe", \
      "test.plugin.generic", 1u, 1u)

SALTS_PLUGIN_DECLARE(plugin_generic_graph, "test.loader.generic_graph", (1u,0u,0u),
    GENERIC_EXPORTS, SALTS_PLUGIN_PASSIVE());
