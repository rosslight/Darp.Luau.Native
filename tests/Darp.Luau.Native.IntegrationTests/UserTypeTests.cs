using Shouldly;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Xunit;
using static Darp.Luau.Native.LuauNative;

namespace Darp.Luau.Native.IntegrationTests;

public sealed unsafe class UserTypeTests
{
    private const int Tag = 1;
    private const int NoFallback = -1;

    private const int AddMethod = 0;
    private const int ValueGetter = 1;
    private const int ValueSetter = 2;
    private const int IndexFallback = 3;
    private const int NewIndexFallback = 4;
    private const int WaitMethod = 5;
    private const int FailingGetter = 6;
    private const int YieldingGetter = 7;

    private struct Host
    {
        public int Destructions;
        public double Value;
        public int FallbackWrites;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Member(lua_State* L, void* ctx, int member)
    {
        var host = (Host*)ctx;
        switch (member)
        {
            case AddMethod:
                // [self, amount]
                lua_pushnumber(L, host->Value + lua_tonumber(L, 2));
                return 1;
            case ValueGetter:
                // [self, key]
                lua_pushnumber(L, host->Value);
                return 1;
            case ValueSetter:
                // [self, key, value]
                host->Value = lua_tonumber(L, 3);
                return 0;
            case IndexFallback:
                fixed (byte* text = "fallback"u8)
                    lua_pushstring(L, text);
                return 1;
            case NewIndexFallback:
                host->FallbackWrites++;
                return 0;
            case WaitMethod:
            case YieldingGetter:
                return DARP_LUAU_CALLBACK_YIELD;
            default:
                fixed (byte* message = "boom"u8)
                    lua_pushstring(L, message);
                return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void CountDestruction(void* ctx) => ((Host*)ctx)->Destructions++;

    [Fact]
    public void MethodIsAValueAndBothCallFormsAreTheSameCall()
    {
        var host = new Host { Value = 40 };
        var state = luaL_newstate();
        try
        {
            SetObjectGlobal(state, &host, NoFallback, NoFallback);

            Run(state, "local add = obj.add; return add(obj, 1), obj:add(2), obj.add == add"u8, results: 3);

            lua_tonumber(state, -3).ShouldBe(41);
            lua_tonumber(state, -2).ShouldBe(42);
            lua_toboolean(state, -1).ShouldBe(1);
        }
        finally
        {
            lua_close(state);
        }
    }

    [Fact]
    public void GetterAndSetterAreCalledWithTheArgumentsOfTheMetamethod()
    {
        var host = new Host();
        var state = luaL_newstate();
        try
        {
            SetObjectGlobal(state, &host, NoFallback, NoFallback);

            Run(state, "obj.value = 7; return obj.value"u8, results: 1);

            lua_tonumber(state, -1).ShouldBe(7);
            host.Value.ShouldBe(7);
        }
        finally
        {
            lua_close(state);
        }
    }

    [Theory]
    [InlineData("obj.readonly = 1", "test:1: userdata member 'readonly' is read-only")]
    [InlineData("return obj.writeonly", "test:1: userdata member 'writeonly' is write-only")]
    [InlineData("obj.add = nil", "test:1: userdata method 'add' cannot be assigned")]
    public void DeclaredNameDoesNotReachTheFallback(string source, string expectedError)
    {
        var host = new Host();
        var state = luaL_newstate();
        try
        {
            SetObjectGlobal(state, &host, IndexFallback, NewIndexFallback);

            RunAndReadError(state, Encoding.UTF8.GetBytes(source)).ShouldBe(expectedError);
            host.FallbackWrites.ShouldBe(0);
        }
        finally
        {
            lua_close(state);
        }
    }

    [Fact]
    public void UnknownKeysOfAnyTypeReachTheFallback()
    {
        var host = new Host();
        var state = luaL_newstate();
        try
        {
            SetObjectGlobal(state, &host, IndexFallback, NewIndexFallback);

            Run(state, "obj.unknown = 1; obj[2] = 3; return obj.unknown, obj[2]"u8, results: 2);

            ReadString(state, -2).ShouldBe("fallback");
            ReadString(state, -1).ShouldBe("fallback");
            host.FallbackWrites.ShouldBe(2);
        }
        finally
        {
            lua_close(state);
        }
    }

    [Fact]
    public void WithoutFallbackAnUnknownKeyReadsNilAndCannotBeWritten()
    {
        var host = new Host();
        var state = luaL_newstate();
        try
        {
            SetObjectGlobal(state, &host, NoFallback, NoFallback);

            Run(state, "return obj.unknown, obj[1]"u8, results: 2);
            lua_isnil(state, -2).ShouldBeTrue();
            lua_isnil(state, -1).ShouldBeTrue();
            lua_settop(state, 0);

            RunAndReadError(state, "obj.unknown = 1"u8).ShouldBe("test:1: attempt to set unknown userdata member 'unknown'");
            RunAndReadError(state, "obj[1] = 1"u8).ShouldBe("test:1: attempt to set a userdata member with a number key");
        }
        finally
        {
            lua_close(state);
        }
    }

    [Theory]
    [InlineData("return obj:wait() + 1")]
    [InlineData("return obj.wait(obj) + 1")]
    [InlineData("local wait = obj.wait; return wait(obj) + 1")]
    public void MethodYieldsAndIsResumedWithItsResult(string source)
    {
        var host = new Host();
        var state = luaL_newstate();
        try
        {
            SetObjectGlobal(state, &host, NoFallback, NoFallback);
            var thread = lua_newthread(state);
            LoadChunk(thread, Encoding.UTF8.GetBytes(source));

            ((lua_Status)lua_resume(thread, null, 0)).ShouldBe(lua_Status.LUA_YIELD);
            lua_pushinteger(thread, 41);
            ((lua_Status)darp_luau_resumecallback(thread, null, 1)).ShouldBe(lua_Status.LUA_OK);

            lua_tointeger(thread, -1).ShouldBe(42);
        }
        finally
        {
            lua_close(state);
        }
    }

    [Fact]
    public void AccessorErrorIsRaisedAndAnAccessorCannotYield()
    {
        var host = new Host();
        var state = luaL_newstate();
        try
        {
            SetObjectGlobal(state, &host, NoFallback, NoFallback);

            RunAndReadError(state, "return obj.failing"u8).ShouldBe("boom");
            RunAndReadError(state, "return obj.yielding"u8)
                .ShouldBe("test:1: attempt to yield while a userdata member is read or written");
        }
        finally
        {
            lua_close(state);
        }
    }

    [Fact]
    public void MethodHasItsNameAsDebugName()
    {
        var host = new Host();
        var state = luaL_newstate();
        try
        {
            luaL_openlibs(state);
            SetObjectGlobal(state, &host, NoFallback, NoFallback);

            Run(state, "return debug.info(obj.add, 'n')"u8, results: 1);

            ReadString(state, -1).ShouldBe("add");
        }
        finally
        {
            lua_close(state);
        }
    }

    [Fact]
    public void UserdataHasTheTagAndTheTypeNameOfItsMetatable()
    {
        var host = new Host();
        var state = luaL_newstate();
        try
        {
            luaL_openlibs(state);
            SetObjectGlobal(state, &host, NoFallback, NoFallback);

            Run(state, "return typeof(obj)"u8, results: 1);
            ReadString(state, -1).ShouldBe("Thing");

            fixed (byte* name = "obj"u8)
                lua_getglobal(state, name);
            (lua_touserdatatagged(state, -1, Tag) != null).ShouldBeTrue();
            (lua_touserdatatagged(state, -1, Tag + 1) == null).ShouldBeTrue();
        }
        finally
        {
            lua_close(state);
        }
    }

    [Fact]
    public void ContextDestructorRunsOnceAfterEveryUserIsGone()
    {
        var host = new Host();
        var state = luaL_newstate();
        try
        {
            int metatable = SetObjectGlobal(state, &host, NoFallback, NoFallback);
            Run(state, "kept = obj.add; obj = nil"u8, results: 0);
            lua_unref(state, metatable);

            // A method that a script kept still needs the context.
            lua_gc(state, (int)lua_GCOp.LUA_GCCOLLECT, 0);
            host.Destructions.ShouldBe(0);
            Run(state, "kept = nil"u8, results: 0);

            lua_gc(state, (int)lua_GCOp.LUA_GCCOLLECT, 0);
            host.Destructions.ShouldBe(1);
        }
        finally
        {
            lua_close(state);
        }
        host.Destructions.ShouldBe(1);
    }

    /// <summary> Sets the global <c>obj</c> to a userdata of a type with a few members and returns the reference of its metatable. </summary>
    private static int SetObjectGlobal(lua_State* L, Host* host, int indexFallback, int newIndexFallback)
    {
        lua_newtable(L);
        int metatable = lua_gettop(L);
        fixed (byte* typeKey = "__type"u8)
        fixed (byte* typeName = "Thing"u8)
        {
            lua_pushstring(L, typeName);
            lua_rawsetfield(L, metatable, typeKey);
        }

        darp_luau_pushmembercontext(L, &Member, host, &CountDestruction);
        int context = lua_gettop(L);

        // Readable members: a method is its function, a getter its member number.
        lua_newtable(L);
        SetMethod(L, "add"u8, context, AddMethod);
        SetMethod(L, "wait"u8, context, WaitMethod);
        SetAccessor(L, "value"u8, ValueGetter);
        SetAccessor(L, "readonly"u8, ValueGetter);
        SetAccessor(L, "failing"u8, FailingGetter);
        SetAccessor(L, "yielding"u8, YieldingGetter);

        lua_newtable(L);
        SetAccessor(L, "value"u8, ValueSetter);
        SetAccessor(L, "writeonly"u8, ValueSetter);

        darp_luau_setmemberaccess(L, metatable, indexFallback, newIndexFallback);
        lua_gettop(L).ShouldBe(metatable);
        int reference = lua_ref(L, metatable);
        lua_pop(L, 1);

        void* memory = darp_luau_newuserdatawithmetatable(L, (nuint)sizeof(nint), Tag, reference);
        (*(nint*)memory).ShouldBe(0);
        fixed (byte* name = "obj"u8)
            lua_setglobal(L, name);
        return reference;
    }

    private static void SetMethod(lua_State* L, ReadOnlySpan<byte> name, int context, int member)
    {
        fixed (byte* pName = name)
            darp_luau_setmemberfunction(L, -1, pName, context, member);
    }

    private static void SetAccessor(lua_State* L, ReadOnlySpan<byte> name, int member)
    {
        lua_pushinteger(L, member);
        fixed (byte* pName = name)
            lua_rawsetfield(L, -2, pName);
    }

    private static void Run(lua_State* L, ReadOnlySpan<byte> source, int results)
    {
        LoadChunk(L, source);
        var status = (lua_Status)lua_pcall(L, 0, results, 0);
        if (status != lua_Status.LUA_OK)
            throw new InvalidOperationException(ReadString(L, -1));
    }

    private static string RunAndReadError(lua_State* L, ReadOnlySpan<byte> source)
    {
        LoadChunk(L, source);
        ((lua_Status)lua_pcall(L, 0, 0, 0)).ShouldBe(lua_Status.LUA_ERRRUN);
        string error = ReadString(L, -1);
        lua_pop(L, 1);
        return error;
    }

    private static void LoadChunk(lua_State* L, ReadOnlySpan<byte> source)
    {
        fixed (byte* pSource = source)
        fixed (byte* chunkname = "=test"u8)
        {
            nuint bytecodeSize;
            var bytecode = luau_compile(pSource, (nuint)source.Length, null, &bytecodeSize);
            try
            {
                luau_load(L, chunkname, bytecode, bytecodeSize, 0).ShouldBe(0);
            }
            finally
            {
                luau_free(bytecode);
            }
        }
    }

    private static string ReadString(lua_State* L, int idx)
    {
        nuint length;
        var value = lua_tolstring(L, idx, &length);
        return Encoding.UTF8.GetString(value, (int)length);
    }
}
