using Shouldly;
using System.Text;
using Xunit;
using static Darp.Luau.Native.LuauNative;

namespace Darp.Luau.Native.IntegrationTests;

public sealed unsafe class TableAccessTests : IDisposable
{
    private readonly lua_State* _state = luaL_newstate();

    public TableAccessTests() => luaL_openlibs(_state);

    public void Dispose() => lua_close(_state);

    [Fact]
    public void GetReplacesTheKeyWithTheValueAndReturnsItsType()
    {
        PushTable("return { answer = 42 }"u8);
        PushString("answer"u8);

        int type = darp_luau_pgettable(_state, -2);

        type.ShouldBe((int)lua_Type.LUA_TNUMBER);
        lua_tonumber(_state, -1).ShouldBe(42);
        lua_gettop(_state).ShouldBe(2);
    }

    [Fact]
    public void GetUsesIndexAndReturnsNilForAMissingValue()
    {
        PushTable("return setmetatable({}, { __index = function(_, key) return #key end })"u8);
        PushString("four"u8);
        darp_luau_pgettable(_state, -2).ShouldBe((int)lua_Type.LUA_TNUMBER);
        lua_tonumber(_state, -1).ShouldBe(4);
        lua_settop(_state, 0);

        PushTable("return {}"u8);
        PushString("missing"u8);
        darp_luau_pgettable(_state, -2).ShouldBe((int)lua_Type.LUA_TNIL);
        lua_gettop(_state).ShouldBe(2);
    }

    [Theory]
    [InlineData("return setmetatable({}, { __index = function() error('boom', 0) end })", "boom")]
    [InlineData("return setmetatable({}, { __index = function() coroutine.yield() end })", "attempt to yield")]
    public void GetReturnsTheErrorInsteadOfRaisingIt(string table, string expectedError)
    {
        PushTable(Encoding.UTF8.GetBytes(table));
        PushString("missing"u8);

        int result = darp_luau_pgettable(_state, -2);

        result.ShouldBe(-(int)lua_Status.LUA_ERRRUN);
        ReadString(-1).ShouldContain(expectedError);
        // The key was replaced by the error, like it is replaced by the value.
        lua_gettop(_state).ShouldBe(2);
        Run("return 1 + 1"u8);
        lua_tonumber(_state, -1).ShouldBe(2);
    }

    [Fact]
    public void GetReturnsAnErrorObjectThatIsNotAString()
    {
        PushTable("return setmetatable({}, { __index = function() error({ code = 7 }) end })"u8);
        PushString("missing"u8);

        darp_luau_pgettable(_state, -2).ShouldBe(-(int)lua_Status.LUA_ERRRUN);

        lua_type(_state, -1).ShouldBe((int)lua_Type.LUA_TTABLE);
    }

    [Fact]
    public void SetPopsKeyAndValueAndUsesNewIndex()
    {
        PushTable("log = {} return setmetatable({ own = 1 }, { __newindex = function(_, k, v) log[k] = v end })"u8);
        PushString("own"u8);
        lua_pushnumber(_state, 5);
        darp_luau_psettable(_state, -3).ShouldBe((int)lua_Status.LUA_OK);
        PushString("fresh"u8);
        lua_pushnumber(_state, 21);
        darp_luau_psettable(_state, -3).ShouldBe((int)lua_Status.LUA_OK);
        lua_gettop(_state).ShouldBe(1);

        // The existing key was written to the table, the new one went to __newindex.
        PushString("own"u8);
        darp_luau_pgettable(_state, -2).ShouldBe((int)lua_Type.LUA_TNUMBER);
        lua_tonumber(_state, -1).ShouldBe(5);
        lua_pop(_state, 1);
        PushString("fresh"u8);
        darp_luau_pgettable(_state, -2).ShouldBe((int)lua_Type.LUA_TNIL);
        Run("return log.fresh"u8);
        lua_tonumber(_state, -1).ShouldBe(21);
    }

    [Fact]
    public void SetOnAFrozenTableReturnsTheError()
    {
        PushTable("return table.freeze({ value = 1 })"u8);
        PushString("value"u8);
        lua_pushnumber(_state, 2);

        int status = darp_luau_psettable(_state, -3);

        status.ShouldBe((int)lua_Status.LUA_ERRRUN);
        ReadString(-1).ShouldContain("readonly");
        // Key and value were replaced by the error.
        lua_gettop(_state).ShouldBe(2);
    }

    [Fact]
    public void SetOnAFrozenTableWithNewIndexTakesANewKeyLikeAScriptWrite()
    {
        PushTable("backing = {} return table.freeze(setmetatable({}, { __newindex = backing }))"u8);
        PushString("fresh"u8);
        lua_pushnumber(_state, 23);

        darp_luau_psettable(_state, -3).ShouldBe((int)lua_Status.LUA_OK);

        Run("return backing.fresh"u8);
        lua_tonumber(_state, -1).ShouldBe(23);
    }

    [Theory]
    [InlineData(false, "nil")]
    [InlineData(true, "NaN")]
    public void SetWithAKeyLuauRejectsReturnsTheError(bool nan, string expectedError)
    {
        PushTable("return {}"u8);
        if (nan)
            lua_pushnumber(_state, double.NaN);
        else
            lua_pushnil(_state);
        lua_pushnumber(_state, 1);

        darp_luau_psettable(_state, -3).ShouldBe((int)lua_Status.LUA_ERRRUN);

        ReadString(-1).ShouldContain(expectedError);
        lua_gettop(_state).ShouldBe(2);
    }

    /// <summary> Runs <paramref name="source"/> and leaves its single result on the stack. </summary>
    private void PushTable(ReadOnlySpan<byte> source) => Run(source);

    private void Run(ReadOnlySpan<byte> source)
    {
        fixed (byte* pSource = source)
        fixed (byte* chunkname = "=test"u8)
        {
            nuint bytecodeSize;
            byte* bytecode = luau_compile(pSource, (nuint)source.Length, null, &bytecodeSize);
            try
            {
                luau_load(_state, chunkname, bytecode, bytecodeSize, 0).ShouldBe(0);
            }
            finally
            {
                luau_free(bytecode);
            }
        }
        lua_pcall(_state, 0, 1, 0).ShouldBe((int)lua_Status.LUA_OK);
    }

    private void PushString(ReadOnlySpan<byte> value)
    {
        fixed (byte* pValue = value)
            lua_pushlstring(_state, pValue, (nuint)value.Length);
    }

    private string ReadString(int idx)
    {
        nuint length;
        byte* value = lua_tolstring(_state, idx, &length);
        return Encoding.UTF8.GetString(value, (int)length);
    }
}
