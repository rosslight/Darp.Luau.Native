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
// closes. It runs during a garbage collection, so it must not use the state, and it must return normally.
typedef void (*darp_luau_callback_destructor)(void* ctx);

typedef int (*darp_luau_require_load_callback)(
    lua_State* L,
    void* ctx,
    const char* path,
    const char* chunkname,
    const char* loadname);

typedef struct darp_luau_require_context_data darp_luau_require_context;

// Returns nonzero to stop the script that runs on `L`.
typedef int (*darp_luau_interrupt_callback)(lua_State* L, void* ctx);

typedef struct darp_luau_interrupt
{
    darp_luau_interrupt_callback callback;
    void* ctx;
} darp_luau_interrupt;

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

// A user type is a kind of userdata whose members the host declares up front: methods, getters and setters. Luau
// then resolves a member by name itself, and only calls the host to run it. One callback serves every member of a
// type; `member` tells it which one was reached.
typedef int (*darp_luau_member_callback)(lua_State* L, void* ctx, int member);

// Pushes the context that the members of one user type share. `dtor` releases `ctx` once Luau has freed every
// function and metatable that uses the context. It can be null when nothing has to be released.
LUAU_EXPORT_API void darp_luau_pushmembercontext(
    lua_State* L,
    darp_luau_member_callback callback,
    void* ctx,
    darp_luau_callback_destructor dtor);

// Does t[name] = f for the table at `idx`, where f is a function that calls member `member` of the context at
// `context_idx`. The function has the debug name `name`, and it yields and is resumed like one pushed by
// darp_luau_pushcallback.
LUAU_EXPORT_API void darp_luau_setmemberfunction(lua_State* L, int idx, const char* name, int context_idx, int member);

// Gives the metatable at `idx` an __index and a __newindex that resolve the members of a user type. Expects the
// context, the readable members and the setters on top of the stack, in that order, and pops them:
//
// - The readable members map a name to a function or to a member number. A function is a method: reading the name
//   returns it, so obj:name() and obj.name(obj) are the same call, and both can yield. A member number is a getter:
//   reading the name calls that member with the arguments of __index.
// - The setters map a name to a member number. Writing the name calls that member with the arguments of __newindex.
//
// A name that is declared but cannot be used this way raises an error: reading a name that only has a setter, and
// writing a name that only has a getter or is a method. Every other key, also one that is not a string, calls
// `index_member` or `newindex_member`. When that is negative, reading gives nil and writing raises an error.
//
// A member that is called from here cannot yield.
LUAU_EXPORT_API void darp_luau_setmemberaccess(lua_State* L, int idx, int index_member, int newindex_member);

// Pushes a new userdata with the tag `tag` and the metatable that `metatable_ref` refers to, and returns its memory,
// which is zeroed.
LUAU_EXPORT_API void* darp_luau_newuserdatawithmetatable(lua_State* L, size_t size, int tag, int metatable_ref);

// lua_gettable and lua_settable for a host that must not let a Luau error unwind through its own frames: an
// error raised by __index or __newindex, a frozen table, or a nil or NaN key is returned instead of raised.
//
// Reads t[k] for the table at `idx` and the key on top, and replaces the key with the value. Returns the type of the
// value. On an error it replaces the key with the error object and returns the negated status.
LUAU_EXPORT_API int darp_luau_pgettable(lua_State* L, int idx);

// Does t[k] = v for the table at `idx`, the key below the top and the value on top, and pops both. Returns LUA_OK.
// On an error it replaces both with the error object and returns the status.
LUAU_EXPORT_API int darp_luau_psettable(lua_State* L, int idx);

// Lets the host stop a running script. Luau calls `interrupt->callback` at its safepoints, the places where it checks
// for an interrupt: when a loop jumps back, when a script calls a function or returns from one, and when the string
// pattern matcher is entered or recurses. Once the callback returns nonzero, the script is stopped:
//
// - Where Luau can yield, the coroutine breaks: lua_resume returns LUA_BREAK. A break is not an error, so no pcall
//   of the script sees it. The host abandons the coroutine, for example with lua_resetthread.
// - Where Luau cannot yield, such as in lua_pcall, a metamethod or a table.sort comparator, the error
//   "script was interrupted" is raised, without a script position. A script can catch it, but for as long as the
//   callback returns nonzero the error is raised again at its next safepoint, so the script cannot go on.
//
// A coroutine that a script resumes from code that cannot yield still breaks. Luau cannot pass that break on and
// raises its own error "attempt to break across metamethod/C-call boundary" in the resuming code instead. That code is
// then stopped at its next safepoint like any other.
//
// Only safepoints are checked, and not every call is one. Work that Luau does without reaching a safepoint is not
// stopped before it ends: a built-in function that Luau calls on its fast path, such as math.abs, a pattern that the
// matcher works through without recursing, or string.rep with a large count.
//
// The callback runs on the thread that runs the script and must return normally. `interrupt` must stay valid until
// it is replaced or the state is closed. Null removes it, and a script then runs at full speed again.
//
// This uses the interrupt callback and the userdata of lua_callbacks(L).
LUAU_EXPORT_API void darp_luau_setinterrupt(lua_State* L, const darp_luau_interrupt* interrupt);

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
