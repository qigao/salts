#include <salts/bindings/lua/cmeta.h>

/*
 * Implementation migrates from salts-utils/tools/lua in #404.
 * Keep this target fail-closed until scalar/struct/buffer/collection/map
 * conversion is moved behind these CMeta-only entry points.
 */
cmeta_status salts_lua_push_cmeta(
    lua_State *state, const cmeta_data_desc *data, const void *object,
    salts_lua_limits limits) {
  (void)state; (void)data; (void)object; (void)limits;
  return CMETA_TRAIT_MISSING;
}

cmeta_status salts_lua_read_cmeta(
    lua_State *state, int index, const cmeta_data_desc *data, void *object,
    salts_lua_limits limits) {
  (void)state; (void)index; (void)data; (void)object; (void)limits;
  return CMETA_TRAIT_MISSING;
}
