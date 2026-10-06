#include <salts/plugin_decl.h>

FunctionInvokeDecl(value, int, fixture_plugin_double,
    (int, value, CMETA_PARAM_IN));

int fixture_plugin_double(int value) {
    return value * 2;
}

#define FIXTURE_EXPORTS(X) \
    X(function, fixture_plugin_double, "test.loader.math.double", \
      "test.loader.math", 1u, 1u)

CMETA_PLUGIN_DECLARE(fixture, "test.loader.c", (1u,0u,0u),
    FIXTURE_EXPORTS, CMETA_PLUGIN_PASSIVE());
