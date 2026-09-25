#ifndef SALTS_BINDINGS_LUA_CMETA_H
#define SALTS_BINDINGS_LUA_CMETA_H

#include <cmeta/data.h>
#include <lua.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct salts_lua_limits {
  size_t max_depth;
  size_t max_items;
  size_t max_bytes;
} salts_lua_limits;

/* Push one canonical CMeta value. Borrowed native storage remains owned by the
 * caller; the Lua binding never takes native ownership implicitly. */
cmeta_status salts_lua_push_cmeta(
    lua_State *state, const cmeta_data_desc *data, const void *object,
    salts_lua_limits limits);

/* Read into caller-provided canonical storage. Provider lifecycle/rollback is
 * defined by CMeta; this binding does not invent a Lua-private ownership model. */
cmeta_status salts_lua_read_cmeta(
    lua_State *state, int index, const cmeta_data_desc *data, void *object,
    salts_lua_limits limits);

#ifdef __cplusplus
}
#endif

#endif
