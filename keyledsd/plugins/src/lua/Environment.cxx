/* Keyleds -- Gaming keyboard tool
 * Copyright (C) 2017 Julien Hartmann, juli1.hartmann@gmail.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "lua/Environment.h"

#include "lua/lua_common.h"
#include <cassert>
#include <lua.hpp>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>


namespace keyleds::lua {

static void * const controllerToken = const_cast<void **>(&controllerToken);

/****************************************************************************/
// Global scope

static int luaPrint(lua_State * lua)        // (...) => ()
{
    int nargs = lua_gettop(lua);
    std::ostringstream buffer;

    for (int idx = 1; idx <= nargs; ++idx) {
        lua_getglobal(lua, "tostring");
        lua_pushvalue(lua, idx);
        lua_pcall(lua, 1, 1, 0);
        buffer <<lua_tostring(lua, -1);
        lua_pop(lua, 1);
    }

    auto * controller = Environment(lua).controller();
    if (!controller) { return luaL_error(lua, noEffectTokenErrorMessage); }

    controller->print(buffer.str());
    return 0;
}

static int luaToColor(lua_State * lua)      // (any) => (table)
{
    int nargs = lua_gettop(lua);
    if (nargs == 1) {
        // We are called as a conversion function
        if (lua_isstring(lua, 1)) {
            // On a string, parse it
            auto * controller = Environment(lua).controller();
            if (!controller) { return luaL_error(lua, noEffectTokenErrorMessage); }

            size_t size;
            const char * string = lua_tolstring(lua, 1, &size);
            auto color = controller->parseColor(std::string(string, size));
            if (color) {
                lua_push(lua, *color);
                return 1;
            }
        }
    } else if (3 <= nargs && nargs <= 4) {
        if (nargs == 3) { lua_pushnumber(lua, 1.0); }
        if (lua_isnumber(lua, 1) && lua_isnumber(lua, 2) &&
            lua_isnumber(lua, 3) && lua_isnumber(lua, 4)) {
            lua_createtable(lua, 4, 0);
            luaL_getmetatable(lua, metatable<RGBAColor>::name);
            lua_setmetatable(lua, -2);
            lua_insert(lua, 1);
            lua_rawseti(lua, 1, 4);
            lua_rawseti(lua, 1, 3);
            lua_rawseti(lua, 1, 2);
            lua_rawseti(lua, 1, 1);
            return 1;
        }
    }
    lua_pushnil(lua);
    return 1;
}

/// Yields the calling animation
static int luaWait(lua_State * lua)
{
    if (!lua_isnumber(lua, 1)) {
        return luaL_argerror(lua, 1, "Duration must be a number");
    }
    lua_pushlightuserdata(lua, const_cast<void *>(Environment::waitToken));
    lua_pushvalue(lua, 1);
    return lua_yield(lua, 2);
}


static int read_fifo(int fd, char* buffer, size_t size) {
    ssize_t bytes_read = read(fd, buffer, size);
    if (bytes_read < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("Failed to read from FIFO");
        }
        return -1;
    }
    return bytes_read;
}

static int process_data(lua_State* L) {
    const char* fifo_path = lua_tostring(L, 1);
    int fd = open(fifo_path, O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        perror("Failed to open FIFO");
        return luaL_error(L, "Failed to open FIFO");
    }

    printf("FIFO opened successfully. File descriptor: %d\n", fd);

    char buffer[1024];
    while (true) {
        ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
        if (bytes_read > 0) {
            printf("Read %zd bytes from FIFO\n", bytes_read);
            lua_pushlstring(L, buffer, bytes_read);
        } else if (bytes_read == -1 && errno != EAGAIN) {
            perror("Failed to read from FIFO");
            close(fd);
            return luaL_error(L, "Failed to read from FIFO");
        }
    }

    close(fd);
    return 0;
}

static const luaL_Reg keyledsGlobals[] = {
    { "fade",       luaNewInterpolator },
    { "print",      luaPrint    },
    { "thread",     luaNewThread },
    { "tocolor",    luaToColor  },
    { "wait",       luaWait     },
    { "process_data", process_data },
    { nullptr, nullptr }
};

/****************************************************************************/

const void * const Environment::waitToken = &Environment::waitToken;

void Environment::openKeyleds(Controller * controller)
{
    SAVE_TOP(m_lua);

    // Save controller pointer
    lua_pushlightuserdata(m_lua, controllerToken);
    lua_pushlightuserdata(m_lua, static_cast<void *>(controller));
    lua_rawset(m_lua, LUA_GLOBALSINDEX);

    // Register types
    registerType<Interpolator>(m_lua);
    registerType<const KeyDatabase *>(m_lua);
    registerType<const KeyDatabase::KeyGroup *>(m_lua);
    registerType<const KeyDatabase::Key *>(m_lua);
    registerType<RenderTarget *>(m_lua);
    registerType<RGBAColor>(m_lua);
    registerType<Thread>(m_lua);

    // Register globals
    lua_pushvalue(m_lua, LUA_GLOBALSINDEX);
    luaL_register(m_lua, nullptr, keyledsGlobals);
    lua_pop(m_lua, 1);

    luaL_openlibs(m_lua);

    CHECK_TOP(m_lua, 0);
}

Environment::Controller * Environment::controller() const
{
    SAVE_TOP(m_lua);

    lua_pushlightuserdata(m_lua, controllerToken);
    lua_rawget(m_lua, LUA_GLOBALSINDEX);
    auto * controller = static_cast<Controller *>(const_cast<void *>(lua_topointer(m_lua, -1)));
    lua_pop(m_lua, 1);

    CHECK_TOP(m_lua, 0);
    return controller;
}

/****************************************************************************/

} // namespace keyleds::lua
