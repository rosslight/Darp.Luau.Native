using System.Runtime.InteropServices;

namespace Darp.Luau.Native;

static unsafe partial class LuauNative
{
    [DllImport(__DllName, EntryPoint = "lua_error", CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    public static extern void lua_error(lua_State* L);
}
