using Shouldly;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Xunit;
using static Darp.Luau.Native.LuauNative;

namespace Darp.Luau.Native.IntegrationTests;

public sealed unsafe class CallbackTests
{
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Yield(lua_State* L, void* ctx) => DARP_LUAU_CALLBACK_YIELD;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Fail(lua_State* L, void* ctx)
    {
        fixed (byte* message = "boom"u8)
            lua_pushstring(L, message);
        return -1;
    }

    [Fact]
    public void CallbackYieldSuspendsCoroutineAndResumeValuesBecomeItsResults()
    {
        var state = luaL_newstate();
        try
        {
            PushCallbackGlobal(state, &Yield, "host"u8);
            var thread = lua_newthread(state);
            LoadChunk(thread, "local v = host(); return v + 1"u8);

            var status = (lua_Status)lua_resume(thread, null, 0);
            status.ShouldBe(lua_Status.LUA_YIELD);

            lua_pushinteger(thread, 41);
            status = (lua_Status)darp_luau_resumecallback(thread, null, 1);

            status.ShouldBe(lua_Status.LUA_OK);
            lua_tointeger(thread, -1).ShouldBe(42);
        }
        finally
        {
            lua_close(state);
        }
    }

    [Fact]
    public void ResumeOfAYieldedCallbackByAnyoneElseFailsAndClearsTheThreadData()
    {
        var state = luaL_newstate();
        try
        {
            luaL_openlibs(state);
            PushCallbackGlobal(state, &Yield, "host"u8);
            var thread = lua_newthread(state);
            fixed (byte* waiting = "waiting"u8)
                lua_setglobal(state, waiting);
            LoadChunk(thread, "local ok = pcall(host); return ok"u8);
            lua_setthreaddata(thread, (void*)1);
            ((lua_Status)lua_resume(thread, null, 0)).ShouldBe(lua_Status.LUA_YIELD);

            // A script resumes the waiting coroutine with a value of its own.
            LoadChunk(state, "return coroutine.resume(waiting, 'forged')"u8);
            ((lua_Status)lua_pcall(state, 0, 2, 0)).ShouldBe(lua_Status.LUA_OK);

            // The coroutine saw an error instead of the forged value, and the host lost its claim on it.
            lua_toboolean(state, -2).ShouldBe(1);
            lua_toboolean(state, -1).ShouldBe(0);
            ((nint)lua_getthreaddata(thread)).ShouldBe(0);
        }
        finally
        {
            lua_close(state);
        }
    }

    [Fact]
    public void CallbackYieldDropsItsArgumentsBeforeItsResultsArrive()
    {
        var state = luaL_newstate();
        try
        {
            luaL_openlibs(state);
            PushCallbackGlobal(state, &Yield, "host"u8);
            var thread = lua_newthread(state);
            LoadChunk(thread, "return select('#', host(1, 2, 3))"u8);
            ((lua_Status)lua_resume(thread, null, 0)).ShouldBe(lua_Status.LUA_YIELD);

            lua_pushinteger(thread, 41);
            var status = (lua_Status)darp_luau_resumecallback(thread, null, 1);

            status.ShouldBe(lua_Status.LUA_OK);
            lua_tointeger(thread, -1).ShouldBe(1);
        }
        finally
        {
            lua_close(state);
        }
    }

    [Fact]
    public void CallbackErrorIsCatchableByPcall()
    {
        var state = luaL_newstate();
        try
        {
            luaL_openlibs(state);
            PushCallbackGlobal(state, &Fail, "host"u8);
            LoadChunk(state, "return pcall(host)"u8);

            var status = (lua_Status)lua_pcall(state, 0, 2, 0);

            status.ShouldBe(lua_Status.LUA_OK);
            lua_toboolean(state, -2).ShouldBe(0);
            ReadString(state, -1).ShouldBe("boom");
        }
        finally
        {
            lua_close(state);
        }
    }

    private static void PushCallbackGlobal(
        lua_State* L,
        delegate* unmanaged[Cdecl]<lua_State*, void*, int> callback,
        ReadOnlySpan<byte> name)
    {
        fixed (byte* pName = name)
        {
            darp_luau_pushcallback(L, callback, null, pName);
            lua_setglobal(L, pName);
        }
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
