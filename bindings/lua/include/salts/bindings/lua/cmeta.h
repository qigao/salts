#ifndef SALTS_BINDINGS_LUA_CMETA_H
#define SALTS_BINDINGS_LUA_CMETA_H

#include <cmeta/data.h>
#include <cmeta/invokable.h>
#include <stddef.h>

struct lua_State;
typedef struct lua_State lua_State;

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

/* Read into caller-provided canonical semantic-zero storage. On success the
 * value is populated; on failure aggregate/container/map readers leave the
 * destination unchanged. Provider lifecycle/rollback is defined by CMeta; this
 * binding does not invent a Lua-private ownership model. */
cmeta_status salts_lua_read_cmeta(
    lua_State *state, int index, const cmeta_data_desc *data, void *object,
    salts_lua_limits limits);

/**
 * Convert Lua arguments through an invokable's explicit FunctionData contract,
 * invoke only through CMeta's validated erased callable, and push zero or one
 * reflected return value. Only input parameters are admitted in this slice.
 */
cmeta_status salts_lua_call_invokable(
    lua_State *state, const cmeta_invokable *invokable,
    int first_argument, size_t argument_count, salts_lua_limits limits,
    int *out_result_count);

#ifdef __cplusplus
}
#endif

#endif
