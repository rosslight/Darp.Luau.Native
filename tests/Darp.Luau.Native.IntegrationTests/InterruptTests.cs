using Shouldly;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Xunit;
using static Darp.Luau.Native.LuauNative;

namespace Darp.Luau.Native.IntegrationTests;

public sealed unsafe class InterruptTests : IDisposable
{
    private const int Never = int.MaxValue;

    private readonly lua_State* _state = luaL_newstate();

    public InterruptTests() => luaL_openlibs(_state);

    public void Dispose() => lua_close(_state);

    /// <summary> Asks to stop the script from the given safepoint on, like a host whose request stays set. </summary>
    private struct StopRequest
    {
        public int SafepointsBeforeStop;
        public int Safepoints;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ShouldStop(lua_State* L, void* ctx)
    {
        var request = (StopRequest*)ctx;
        return request->Safepoints++ >= request->SafepointsBeforeStop ? 1 : 0;
    }

    [Fact]
    public void BreaksACoroutineInAnEndlessLoop()
    {
        var request = new StopRequest { SafepointsBeforeStop = 100 };
        var interrupt = new darp_luau_interrupt { callback = &ShouldStop, ctx = &request };
        darp_luau_setinterrupt(_state, &interrupt);
        lua_State* thread = NewThread("while true do end"u8);

        ((lua_Status)lua_resume(thread, null, 0)).ShouldBe(lua_Status.LUA_BREAK);
    }

    [Fact]
    public void ABreakIsNotAnErrorThatPcallCatches()
    {
        var request = new StopRequest { SafepointsBeforeStop = 100 };
        var interrupt = new darp_luau_interrupt { callback = &ShouldStop, ctx = &request };
        darp_luau_setinterrupt(_state, &interrupt);
        lua_State* thread = NewThread(
            """
            while true do
                pcall(function()
                    entered = true
                    while true do end
                end)
                returned = true
            end
            """u8
        );

        ((lua_Status)lua_resume(thread, null, 0)).ShouldBe(lua_Status.LUA_BREAK);

        ReadGlobalType("entered"u8).ShouldBe(lua_Type.LUA_TBOOLEAN);
        ReadGlobalType("returned"u8).ShouldBe(lua_Type.LUA_TNIL);
    }

    [Fact]
    public void RaisesAnErrorWhereLuauCannotBreak()
    {
        var request = new StopRequest { SafepointsBeforeStop = 100 };
        var interrupt = new darp_luau_interrupt { callback = &ShouldStop, ctx = &request };
        darp_luau_setinterrupt(_state, &interrupt);
        Load(_state, "while true do end"u8);

        // lua_pcall runs the chunk on the main thread, which cannot yield and so cannot break.
        ((lua_Status)lua_pcall(_state, 0, 0, 0)).ShouldBe(lua_Status.LUA_ERRRUN);

        ReadString(_state, -1).ShouldBe("script was interrupted");
    }

    [Fact]
    public void LeavesTheStateUsableAfterTheError()
    {
        var request = new StopRequest { SafepointsBeforeStop = 100 };
        var interrupt = new darp_luau_interrupt { callback = &ShouldStop, ctx = &request };
        darp_luau_setinterrupt(_state, &interrupt);
        Load(_state, "local function spin() while true do end end spin()"u8);
        ((lua_Status)lua_pcall(_state, 0, 0, 0)).ShouldBe(lua_Status.LUA_ERRRUN);
        lua_settop(_state, 0);

        request = new StopRequest { SafepointsBeforeStop = Never };
        Load(_state, "local sum = 0 for i = 1, 100 do sum += i end return sum"u8);

        ((lua_Status)lua_pcall(_state, 0, 1, 0)).ShouldBe(lua_Status.LUA_OK);
        lua_tonumber(_state, -1).ShouldBe(5050);
    }

    /// <summary> Luau cannot pass the break of the coroutine on to code that cannot yield, and raises an error there. </summary>
    [Fact]
    public void StopsACoroutineOfTheScriptAndTheCodeThatResumedItWhereLuauCannotBreak()
    {
        var request = new StopRequest { SafepointsBeforeStop = 100 };
        var interrupt = new darp_luau_interrupt { callback = &ShouldStop, ctx = &request };
        darp_luau_setinterrupt(_state, &interrupt);
        Load(
            _state,
            """
            local endless = coroutine.wrap(function() while true do end end)
            local ok, message = pcall(endless)
            resume_error = message
            while true do end
            """u8
        );

        ((lua_Status)lua_pcall(_state, 0, 0, 0)).ShouldBe(lua_Status.LUA_ERRRUN);

        ReadString(_state, -1).ShouldBe("script was interrupted");
        fixed (byte* name = "resume_error"u8)
            lua_getglobal(_state, name);
        ReadString(_state, -1).ShouldContain("attempt to break across metamethod/C-call boundary");
    }

    [Fact]
    public void RaisesTheErrorAgainAfterPcallCaughtIt()
    {
        var request = new StopRequest { SafepointsBeforeStop = 100 };
        var interrupt = new darp_luau_interrupt { callback = &ShouldStop, ctx = &request };
        darp_luau_setinterrupt(_state, &interrupt);
        Load(
            _state,
            """
            while true do
                local ok = pcall(function()
                    while true do end
                end)
                caught = not ok
            end
            """u8
        );

        ((lua_Status)lua_pcall(_state, 0, 0, 0)).ShouldBe(lua_Status.LUA_ERRRUN);

        ReadString(_state, -1).ShouldContain("script was interrupted");
        // The script saw the error once and could not go on: its next loop iteration raised it again.
        ReadGlobalType("caught"u8).ShouldBe(lua_Type.LUA_TBOOLEAN);
    }

    [Fact]
    public void StopsAMetamethodOfACoroutineWithAnErrorAndThenBreaksTheCoroutine()
    {
        var request = new StopRequest { SafepointsBeforeStop = 100 };
        var interrupt = new darp_luau_interrupt { callback = &ShouldStop, ctx = &request };
        darp_luau_setinterrupt(_state, &interrupt);
        lua_State* thread = NewThread(
            """
            local endless = setmetatable({}, { __index = function() while true do end end })
            local ok, message = pcall(function() return endless.key end)
            metamethod_error = message
            while true do end
            """u8
        );

        ((lua_Status)lua_resume(thread, null, 0)).ShouldBe(lua_Status.LUA_BREAK);

        fixed (byte* name = "metamethod_error"u8)
            lua_getglobal(_state, name);
        ReadString(_state, -1).ShouldContain("script was interrupted");
    }

    [Fact]
    public void StopsASlowStringPattern()
    {
        var request = new StopRequest { SafepointsBeforeStop = 10_000 };
        var interrupt = new darp_luau_interrupt { callback = &ShouldStop, ctx = &request };
        darp_luau_setinterrupt(_state, &interrupt);
        // Backtracks for longer than anyone waits.
        Load(_state, """return string.find(string.rep("a", 40), string.rep("a*", 40) .. "b")"""u8);

        ((lua_Status)lua_pcall(_state, 0, 0, 0)).ShouldBe(lua_Status.LUA_ERRRUN);

        ReadString(_state, -1).ShouldContain("script was interrupted");
    }

    [Fact]
    public void LetsTheScriptRunWhileTheCallbackReturnsZero()
    {
        var request = new StopRequest { SafepointsBeforeStop = Never };
        var interrupt = new darp_luau_interrupt { callback = &ShouldStop, ctx = &request };
        darp_luau_setinterrupt(_state, &interrupt);
        Load(_state, "local sum = 0 for i = 1, 100 do sum += i end return sum"u8);

        ((lua_Status)lua_pcall(_state, 0, 1, 0)).ShouldBe(lua_Status.LUA_OK);

        lua_tonumber(_state, -1).ShouldBe(5050);
        request.Safepoints.ShouldBeGreaterThanOrEqualTo(100);
    }

    [Fact]
    public void NullRemovesTheInterrupt()
    {
        var request = new StopRequest { SafepointsBeforeStop = 0 };
        var interrupt = new darp_luau_interrupt { callback = &ShouldStop, ctx = &request };
        darp_luau_setinterrupt(_state, &interrupt);
        darp_luau_setinterrupt(_state, null);
        Load(_state, "local sum = 0 for i = 1, 100 do sum += i end return sum"u8);

        ((lua_Status)lua_pcall(_state, 0, 1, 0)).ShouldBe(lua_Status.LUA_OK);

        request.Safepoints.ShouldBe(0);
    }

    /// <summary> Creates a coroutine that runs <paramref name="source"/> when it is resumed. </summary>
    private lua_State* NewThread(ReadOnlySpan<byte> source)
    {
        lua_State* thread = lua_newthread(_state);
        Load(thread, source);
        return thread;
    }

    private static void Load(lua_State* L, ReadOnlySpan<byte> source)
    {
        fixed (byte* pSource = source)
        fixed (byte* chunkname = "=test"u8)
        {
            nuint bytecodeSize;
            byte* bytecode = luau_compile(pSource, (nuint)source.Length, null, &bytecodeSize);
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

    private lua_Type ReadGlobalType(ReadOnlySpan<byte> name)
    {
        fixed (byte* pName = name)
            lua_getglobal(_state, pName);
        var type = (lua_Type)lua_type(_state, -1);
        lua_pop(_state, 1);
        return type;
    }

    private static string ReadString(lua_State* L, int idx)
    {
        nuint length;
        byte* value = lua_tolstring(L, idx, &length);
        return Encoding.UTF8.GetString(value, (int)length);
    }
}
