using Shouldly;
using Xunit;
using static Darp.Luau.Native.LuauNative;

namespace Darp.Luau.Native.IntegrationTests;

public sealed unsafe class LuaTests
{
    [Fact]
    public void SetFields()
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
            type.ShouldBe(lua_Type.LUA_TNUMBER);
        }
        finally
        {
            lua_close(state);
        }
    }

    [Fact]
    public void CompileAndFreeTest()
    {
        // Verify compile + free round-trip
        var source = "return 1"u8;
        fixed (byte* pSource = source)
        {
            nuint bytecodeSize;
            var bytecode = luau_compile(pSource, (nuint)source.Length, null, &bytecodeSize);
            try
            {
                bytecodeSize.ShouldBeGreaterThan((nuint)0);
                Assert.NotNull(bytecode);
            }
            finally
            {
                luau_free(bytecode);
            }
        }
    }

}
