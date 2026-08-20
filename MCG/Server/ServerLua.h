#pragma once
#include <lua.hpp>

void register_lua_functions(lua_State* L);
void LoadLuaScripts(lua_State* L);