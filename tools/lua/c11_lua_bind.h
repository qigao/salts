/**
 * @file c11_lua_bind.h
 * @brief Lightweight C11 Preprocessor and _Generic based Lua binding library.
 *
 * Provides type-generic value pushing/getting, zero-boilerplate function registration,
 * and X-Macro based struct reflection/serialization to/from Lua tables.
 */
#ifndef C11_LUA_BIND_H
#define C11_LUA_BIND_H

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>


#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Preprocessor Metaprogramming Helpers
 * ------------------------------------------------------------------------- */
#define C11_EXPAND(x) x
#define C11_CONCAT(a, b) C11_CONCAT_INNER(a, b)
#define C11_CONCAT_INNER(a, b) a##b
#define C11_STRINGIFY(x) #x

/* Count variadic macro arguments (up to 16) */
#define C11_COUNT_ARGS(...) C11_EXPAND(C11_COUNT_ARGS_HELPER(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1))
#define C11_COUNT_ARGS_HELPER(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N

/* -------------------------------------------------------------------------
 * Type-Generic Value Pushing (C11 _Generic)
 * ------------------------------------------------------------------------- */
static inline void c11_lua_push_bool(lua_State* L, bool v) { lua_pushboolean(L, v ? 1 : 0); }
static inline void c11_lua_push_int(lua_State* L, long long v) { lua_pushinteger(L, (lua_Integer)v); }
static inline void c11_lua_push_num(lua_State* L, double v) { lua_pushnumber(L, (lua_Number)v); }
static inline void c11_lua_push_str(lua_State* L, const char* v) { lua_pushstring(L, v ? v : ""); }
static inline void c11_lua_push_ptr(lua_State* L, void* v) { lua_pushlightuserdata(L, v); }

/**
 * @brief Pushes a typed C value onto the Lua stack using C11 _Generic selection.
 */
#define c11_lua_push(L, val) _Generic((val), \
    _Bool:               c11_lua_push_bool, \
    char:                c11_lua_push_int, \
    signed char:         c11_lua_push_int, \
    unsigned char:       c11_lua_push_int, \
    short:               c11_lua_push_int, \
    unsigned short:      c11_lua_push_int, \
    int:                 c11_lua_push_int, \
    unsigned int:        c11_lua_push_int, \
    long:                c11_lua_push_int, \
    unsigned long:       c11_lua_push_int, \
    long long:           c11_lua_push_int, \
    unsigned long long:  c11_lua_push_int, \
    float:               c11_lua_push_num, \
    double:              c11_lua_push_num, \
    const char*:         c11_lua_push_str, \
    char*:               c11_lua_push_str, \
    void*:               c11_lua_push_ptr  \
)(L, val)

/* -------------------------------------------------------------------------
 * Type-Generic Value Extraction (C11 _Generic)
 * ------------------------------------------------------------------------- */
static inline void c11_lua_get_bool(lua_State* L, int idx, bool* out) { if (out) *out = lua_toboolean(L, idx) != 0; }
static inline void c11_lua_get_int(lua_State* L, int idx, int* out) { if (out) *out = (int)lua_tointeger(L, idx); }
static inline void c11_lua_get_long(lua_State* L, int idx, long* out) { if (out) *out = (long)lua_tointeger(L, idx); }
static inline void c11_lua_get_llong(lua_State* L, int idx, long long* out) { if (out) *out = (long long)lua_tointeger(L, idx); }
static inline void c11_lua_get_float(lua_State* L, int idx, float* out) { if (out) *out = (float)lua_tonumber(L, idx); }
static inline void c11_lua_get_double(lua_State* L, int idx, double* out) { if (out) *out = (double)lua_tonumber(L, idx); }
static inline void c11_lua_get_str(lua_State* L, int idx, const char** out) { if (out) *out = lua_tostring(L, idx); }
static inline void c11_lua_get_ptr(lua_State* L, int idx, void** out) { if (out) *out = lua_touserdata(L, idx); }

/**
 * @brief Extracts a value from the Lua stack into a typed pointer using C11 _Generic selection.
 */
#define c11_lua_get(L, idx, ptr) _Generic((ptr), \
    _Bool*:              c11_lua_get_bool, \
    int*:                c11_lua_get_int, \
    long*:               c11_lua_get_long, \
    long long*:          c11_lua_get_llong, \
    float*:              c11_lua_get_float, \
    double*:             c11_lua_get_double, \
    const char**:        c11_lua_get_str, \
    void**:              c11_lua_get_ptr \
)(L, idx, ptr)


/* -------------------------------------------------------------------------
 * Zero-Boilerplate Function Registration
 * ------------------------------------------------------------------------- */
#define C11_LUA_REG_PAIR(func) { #func, func }

#define C11_LUA_REG_1(f1) C11_LUA_REG_PAIR(f1)
#define C11_LUA_REG_2(f1, f2) C11_LUA_REG_PAIR(f1), C11_LUA_REG_PAIR(f2)
#define C11_LUA_REG_3(f1, f2, f3) C11_LUA_REG_2(f1, f2), C11_LUA_REG_PAIR(f3)
#define C11_LUA_REG_4(f1, f2, f3, f4) C11_LUA_REG_3(f1, f2, f3), C11_LUA_REG_PAIR(f4)
#define C11_LUA_REG_5(f1, f2, f3, f4, f5) C11_LUA_REG_4(f1, f2, f3, f4), C11_LUA_REG_PAIR(f5)
#define C11_LUA_REG_6(f1, f2, f3, f4, f5, f6) C11_LUA_REG_5(f1, f2, f3, f4, f5), C11_LUA_REG_PAIR(f6)
#define C11_LUA_REG_7(f1, f2, f3, f4, f5, f6, f7) C11_LUA_REG_6(f1, f2, f3, f4, f5, f6), C11_LUA_REG_PAIR(f7)
#define C11_LUA_REG_8(f1, f2, f3, f4, f5, f6, f7, f8) C11_LUA_REG_7(f1, f2, f3, f4, f5, f6, f7), C11_LUA_REG_PAIR(f8)
#define C11_LUA_REG_9(f1, f2, f3, f4, f5, f6, f7, f8, f9) C11_LUA_REG_8(f1, f2, f3, f4, f5, f6, f7, f8), C11_LUA_REG_PAIR(f9)
#define C11_LUA_REG_10(f1, f2, f3, f4, f5, f6, f7, f8, f9, f10) C11_LUA_REG_9(f1, f2, f3, f4, f5, f6, f7, f8, f9), C11_LUA_REG_PAIR(f10)

/**
 * @brief Batch registers C functions (lua_CFunction) into a new Lua table and leaves it on top of the stack.
 */
#define C11_LUA_BIND_FUNCS(L, ...) \
    do { \
        const luaL_Reg _c11_lua_funcs[] = { \
            C11_EXPAND(C11_CONCAT(C11_LUA_REG_, C11_COUNT_ARGS(__VA_ARGS__))(__VA_ARGS__)), \
            { NULL, NULL } \
        }; \
        lua_newtable(L); \
        luaL_setfuncs(L, _c11_lua_funcs, 0); \
    } while(0)

/* -------------------------------------------------------------------------
 * X-Macro Based C Struct Reflection & Lua Table Serialization
 * ------------------------------------------------------------------------- */
#define C11_STRUCT_FIELD_DECL(type, name) type name;

#define C11_STRUCT_FIELD_PUSH(type, name) \
    lua_pushstring(L, #name); \
    c11_lua_push(L, obj->name); \
    lua_settable(L, -3);

#define C11_STRUCT_FIELD_GET(type, name) \
    lua_getfield(L, idx, #name); \
    c11_lua_get(L, -1, &(obj->name)); \
    lua_pop(L, 1);

/**
 * @brief Defines a C struct and auto-generates StructName_to_lua and StructName_from_lua functions.
 *
 * @param StructName Name of the struct to define.
 * @param FIELDS An X-Macro list of fields formatted as: X(type, name) ...
 */
#define C11_LUA_DEFINE_STRUCT(StructName, FIELDS) \
    typedef struct StructName StructName; \
    struct StructName { \
        FIELDS(C11_STRUCT_FIELD_DECL) \
    }; \
    static inline void StructName##_to_lua(lua_State* L, const StructName* obj) { \
        if (!L || !obj) return; \
        lua_newtable(L); \
        FIELDS(C11_STRUCT_FIELD_PUSH) \
    } \
    static inline void StructName##_from_lua(lua_State* L, int idx, StructName* obj) { \
        if (!L || !obj) return; \
        int abs_idx = (idx < 0) ? (lua_gettop(L) + idx + 1) : idx; \
        FIELDS(C11_STRUCT_FIELD_GET) \
    }

#ifdef __cplusplus
}
#endif

#endif /* C11_LUA_BIND_H */
