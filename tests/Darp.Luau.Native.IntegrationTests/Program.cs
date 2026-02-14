using Darp.Luau.Native;
using static Darp.Luau.Native.LuauNative;

unsafe
{
    var state = luaL_newstate();
    try
    {
        lua_pushinteger(state, 123);
        fixed(byte* pName = "name"u8)
            lua_setfield(state, LUA_GLOBALSINDEX, pName);

        fixed (byte* pName = "name"u8)
            _ = lua_getfield(state, LUA_GLOBALSINDEX, pName);
        var type = (lua_Type)lua_type(state, -1);
        ArgumentOutOfRangeException.ThrowIfNotEqual(type, lua_Type.LUA_TNUMBER);
    }
    finally
    {
        lua_close(state);
    }
    Console.WriteLine("Darp.Luau.Native integration sequence passed.");
}