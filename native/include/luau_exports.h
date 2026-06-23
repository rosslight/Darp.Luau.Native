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
};

typedef int (*darp_luau_callback)(lua_State* L, void* ctx);

typedef int (*darp_luau_require_load_callback)(
    lua_State* L,
    void* ctx,
    const char* path,
    const char* chunkname,
    const char* loadname);

LUAU_EXPORT_API void luau_free(void* ptr);

LUAU_EXPORT_API void darp_luau_pushcallback(
    lua_State* L,
    darp_luau_callback callback,
    void* ctx,
    const char* debugname);

LUAU_EXPORT_API void darp_luau_pushrequirecallback(
    lua_State* L,
    darp_luau_callback callback,
    void* ctx,
    const char* debugname);

LUAU_EXPORT_API void* darp_luau_newrequirecontext(
    luarequire_Configuration_init config_init,
    darp_luau_require_load_callback load_callback,
    void* ctx);

LUAU_EXPORT_API void darp_luau_freerequirecontext(void* context);

LUAU_EXPORT_API int darp_luau_pushproxyrequire(lua_State* L, void* context);

#ifdef __cplusplus
}
#endif
