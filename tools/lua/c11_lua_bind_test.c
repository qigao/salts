#include "c11_lua_bind.h"
#include "tinytest.h"

#include <stdbool.h>
#include <string.h>

static int l_add(lua_State* L) {
    double a = luaL_checknumber(L, 1);
    double b = luaL_checknumber(L, 2);
    lua_pushnumber(L, a + b);
    return 1;
}

static int l_concat(lua_State* L) {
    const char* s1 = luaL_checkstring(L, 1);
    const char* s2 = luaL_checkstring(L, 2);
    char buf[256];
    snprintf(buf, sizeof(buf), "%s%s", s1, s2);
    lua_pushstring(L, buf);
    return 1;
}

#define PLAYER_FIELDS(X) \
    X(int, id) \
    X(double, hp) \
    X(const char*, name) \
    X(bool, is_active)

C11_LUA_DEFINE_STRUCT(Player, PLAYER_FIELDS)

suite("c11 lua bind") {
    static lua_State* L;

    before_each() {
        L = luaL_newstate();
        luaL_openlibs(L);
    }

    after_each() {
        if (L) lua_close(L);
        L = NULL;
    }

    it("pushes and gets typed values") {
        c11_lua_push(L, 42);
        c11_lua_push(L, 3.14159);
        c11_lua_push(L, "Hello C11");
        c11_lua_push(L, (bool)true);

        bool b_val = false;
        const char* s_val = NULL;
        double d_val = 0.0;
        int i_val = 0;

        c11_lua_get(L, 4, &b_val);
        c11_lua_get(L, 3, &s_val);
        c11_lua_get(L, 2, &d_val);
        c11_lua_get(L, 1, &i_val);

        check_int_eq(i_val, 42);
        check_double_eq(d_val, 3.14159, 0.00001);
        check_str_eq(s_val, "Hello C11");
        check_true(b_val);
        lua_pop(L, 4);
    }

    it("registers C functions into a Lua table") {
        C11_LUA_BIND_FUNCS(L, l_add, l_concat);
        lua_setglobal(L, "MyLib");

        const char* script =
            "res_add = MyLib.l_add(15, 27)\n"
            "res_str = MyLib.l_concat('C11 ', 'Lua')\n";
        check_int_eq(luaL_dostring(L, script), LUA_OK);

        lua_getglobal(L, "res_add");
        check_int_eq((int)lua_tointeger(L, -1), 42);
        lua_pop(L, 1);

        lua_getglobal(L, "res_str");
        check_str_eq(lua_tostring(L, -1), "C11 Lua");
        lua_pop(L, 1);
    }

    it("serializes a struct to and from a Lua table") {
        Player p1 = { .id = 1001, .hp = 98.5, .name = "Warrior", .is_active = true };
        Player_to_lua(L, &p1);
        lua_setglobal(L, "player1");

        lua_getglobal(L, "player1");
        Player p2 = {0};
        Player_from_lua(L, -1, &p2);
        lua_pop(L, 1);

        check_int_eq(p2.id, 1001);
        check_double_eq(p2.hp, 98.5, 0.001);
        check_str_eq(p2.name, "Warrior");
        check_true(p2.is_active);
    }
}
