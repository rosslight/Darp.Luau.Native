#pragma once

#include <stddef.h>

#include "../luau/Require/include/Luau/Require.h"
#include "../luau/VM/include/lua.h"

#ifndef LUAU_EXPORT_API
#ifdef _MSC_VER
#define LUAU_EXPORT_API extern __declspec(dllexport)
#else
#define LUAU_EXPORT_API extern __attribute__((visibility("default")))
#endif
#endif

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    DARP_LUAU_REQUIRE_PROXY = -2,
    DARP_LUAU_CALLBACK_YIELD = -3,
};

typedef int (*darp_luau_callback)(lua_State* L, void* ctx);

// Runs when Luau frees a function pushed by darp_luau_pushcallback: when it collects the function, or when the state
// closes. It runs during a garbage collection, so it must not use the state.
typedef void (*darp_luau_callback_destructor)(void* ctx);

typedef int (*darp_luau_require_load_callback)(
    lua_State* L,
    void* ctx,
    const char* path,
    const char* chunkname,
    const char* loadname);

typedef struct darp_luau_require_context_data darp_luau_require_context;

LUAU_EXPORT_API void luau_free(void* ptr);

// Pushes a function that calls `callback`. When the callback returns DARP_LUAU_CALLBACK_YIELD, the coroutine yields
// and only darp_luau_resumecallback can continue it with the results of the callback. Any other resume raises an error
// in the coroutine and clears its thread data, which tells the host that it no longer waits in the callback.
//
// `dtor` releases `ctx` once the function is gone. It can be null when nothing has to be released.
LUAU_EXPORT_API void darp_luau_pushcallback(
    lua_State* L,
    darp_luau_callback callback,
    void* ctx,
    darp_luau_callback_destructor dtor,
    const char* debugname);

// Resumes a coroutine that yielded in a callback pushed by darp_luau_pushcallback. The top `narg` values become the
// results of that callback. Returns what lua_resume returns.
LUAU_EXPORT_API int darp_luau_resumecallback(lua_State* L, lua_State* from, int narg);

// lua_gettable and lua_settable for a host that must not let a Luau error unwind through its own frames: an
// error raised by __index or __newindex, a frozen table, or a nil or NaN key is returned instead of raised.
//
// Reads t[k] for the table at `idx` and the key on top, and replaces the key with the value. Returns the type of the
// value. On an error it replaces the key with the error object and returns the negated status.
LUAU_EXPORT_API int darp_luau_pgettable(lua_State* L, int idx);

// Does t[k] = v for the table at `idx`, the key below the top and the value on top, and pops both. Returns LUA_OK.
// On an error it replaces both with the error object and returns the status.
LUAU_EXPORT_API int darp_luau_psettable(lua_State* L, int idx);

LUAU_EXPORT_API void darp_luau_pushrequirecallback(
    lua_State* L,
    darp_luau_callback callback,
    void* ctx,
    const char* debugname);

LUAU_EXPORT_API darp_luau_require_context* darp_luau_newrequirecontext(
    luarequire_Configuration_init config_init,
    darp_luau_require_load_callback load_callback,
    void* ctx);

LUAU_EXPORT_API void darp_luau_freerequirecontext(darp_luau_require_context* context);

LUAU_EXPORT_API int darp_luau_pushproxyrequire(lua_State* L, darp_luau_require_context* context);

#ifdef __cplusplus
}
#endif
