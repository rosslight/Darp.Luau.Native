#include "include/luau_api.h"

// Luau internals, for luaD_pcall: the protected call that lua_pcall is built on.
#include "luau/VM/src/ldo.h"

#include <new>
#include <stdlib.h>
#include <string.h>

struct darp_luau_callback_context_data
{
    darp_luau_callback callback;
    void* ctx;
    darp_luau_callback_destructor dtor;
};

struct darp_luau_require_context_data
{
    luarequire_Configuration managed_config;
    darp_luau_require_load_callback load_callback;
    void* managed_ctx;
};

static void darp_luau_raise_top_error(lua_State* L)
{
    lua_error(L);
}

// Marks a resume that delivers the result of a yielded callback. Its address is the mark, which a script cannot forge.
static char darp_luau_callback_resume_mark;

static void darp_luau_callback_context_destructor(lua_State* L, void* userdata)
{
    (void)L;
    auto* context = static_cast<darp_luau_callback_context_data*>(userdata);
    if (context->dtor)
        context->dtor(context->ctx);
}

static int darp_luau_callback_trampoline(lua_State* L)
{
    auto* context = static_cast<darp_luau_callback_context_data*>(lua_touserdata(L, lua_upvalueindex(1)));
    int result = context->callback(L, context->ctx);

    if (result >= 0)
        return result;

    if (result == DARP_LUAU_CALLBACK_YIELD)
    {
        // Empty the frame, so that the continuation sees exactly what the resume passed.
        lua_settop(L, 0);
        return lua_yield(L, 0);
    }

    darp_luau_raise_top_error(L);
    return 0;
}

// Runs when a coroutine that yielded in a callback is resumed.
static int darp_luau_callback_continuation(lua_State* L, int status)
{
    (void)status;

    if (lua_gettop(L) == 0 || lua_tolightuserdata(L, 1) != &darp_luau_callback_resume_mark)
    {
        // Someone other than the host resumed the coroutine. The host loses its claim on the coroutine, so it does
        // not deliver the result of the callback to whatever the coroutine does next.
        lua_setthreaddata(L, nullptr);
        luaL_error(L, "cannot resume a coroutine that is waiting for a managed callback");
    }

    lua_remove(L, 1);
    return lua_gettop(L);
}

static int darp_luau_require_callback_trampoline(lua_State* L)
{
    auto* context = static_cast<darp_luau_callback_context_data*>(lua_touserdata(L, lua_upvalueindex(1)));
    int result = context->callback(L, context->ctx);

    if (result >= 0)
        return result;

    if (result != DARP_LUAU_REQUIRE_PROXY)
    {
        darp_luau_raise_top_error(L);
        return 0;
    }

    lua_pushvalue(L, lua_upvalueindex(2));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);

    int return_count = lua_gettop(L);
    if (return_count != 1)
    {
        lua_settop(L, 0);
        luaL_error(L, "module must return a single value");
        return 0;
    }

    return 1;
}

static int darp_luau_require_load_trampoline(
    lua_State* L,
    void* ctx,
    const char* path,
    const char* chunkname,
    const char* loadname)
{
    auto* context = static_cast<darp_luau_require_context_data*>(ctx);
    int result = context->load_callback(L, context->managed_ctx, path, chunkname, loadname);

    if (result < 0)
    {
        darp_luau_raise_top_error(L);
        return 0;
    }

    return result;
}

static bool darp_luau_require_is_require_allowed(lua_State* L, void* ctx, const char* requirer_chunkname)
{
    auto* context = static_cast<darp_luau_require_context_data*>(ctx);
    return context->managed_config.is_require_allowed(L, context->managed_ctx, requirer_chunkname);
}

static luarequire_NavigateResult darp_luau_require_reset(lua_State* L, void* ctx, const char* requirer_chunkname)
{
    auto* context = static_cast<darp_luau_require_context_data*>(ctx);
    return context->managed_config.reset(L, context->managed_ctx, requirer_chunkname);
}

static luarequire_NavigateResult darp_luau_require_jump_to_alias(lua_State* L, void* ctx, const char* path)
{
    auto* context = static_cast<darp_luau_require_context_data*>(ctx);
    return context->managed_config.jump_to_alias(L, context->managed_ctx, path);
}

static luarequire_NavigateResult darp_luau_require_to_parent(lua_State* L, void* ctx)
{
    auto* context = static_cast<darp_luau_require_context_data*>(ctx);
    return context->managed_config.to_parent(L, context->managed_ctx);
}

static luarequire_NavigateResult darp_luau_require_to_child(lua_State* L, void* ctx, const char* name)
{
    auto* context = static_cast<darp_luau_require_context_data*>(ctx);
    return context->managed_config.to_child(L, context->managed_ctx, name);
}

static bool darp_luau_require_is_module_present(lua_State* L, void* ctx)
{
    auto* context = static_cast<darp_luau_require_context_data*>(ctx);
    return context->managed_config.is_module_present(L, context->managed_ctx);
}

static luarequire_WriteResult darp_luau_require_get_chunkname(
    lua_State* L,
    void* ctx,
    char* buffer,
    size_t buffer_size,
    size_t* size_out)
{
    auto* context = static_cast<darp_luau_require_context_data*>(ctx);
    return context->managed_config.get_chunkname(L, context->managed_ctx, buffer, buffer_size, size_out);
}

static luarequire_WriteResult darp_luau_require_get_loadname(
    lua_State* L,
    void* ctx,
    char* buffer,
    size_t buffer_size,
    size_t* size_out)
{
    auto* context = static_cast<darp_luau_require_context_data*>(ctx);
    return context->managed_config.get_loadname(L, context->managed_ctx, buffer, buffer_size, size_out);
}

static luarequire_WriteResult darp_luau_require_get_cache_key(
    lua_State* L,
    void* ctx,
    char* buffer,
    size_t buffer_size,
    size_t* size_out)
{
    auto* context = static_cast<darp_luau_require_context_data*>(ctx);
    return context->managed_config.get_cache_key(L, context->managed_ctx, buffer, buffer_size, size_out);
}

static luarequire_ConfigStatus darp_luau_require_get_config_status(lua_State* L, void* ctx)
{
    auto* context = static_cast<darp_luau_require_context_data*>(ctx);
    return context->managed_config.get_config_status(L, context->managed_ctx);
}

static luarequire_WriteResult darp_luau_require_get_config(
    lua_State* L,
    void* ctx,
    char* buffer,
    size_t buffer_size,
    size_t* size_out)
{
    auto* context = static_cast<darp_luau_require_context_data*>(ctx);
    return context->managed_config.get_config(L, context->managed_ctx, buffer, buffer_size, size_out);
}

static void darp_luau_proxyrequire_config_init(luarequire_Configuration* config)
{
    memset(config, 0, sizeof(luarequire_Configuration));
    config->is_require_allowed = darp_luau_require_is_require_allowed;
    config->reset = darp_luau_require_reset;
    config->jump_to_alias = darp_luau_require_jump_to_alias;
    config->to_parent = darp_luau_require_to_parent;
    config->to_child = darp_luau_require_to_child;
    config->is_module_present = darp_luau_require_is_module_present;
    config->get_chunkname = darp_luau_require_get_chunkname;
    config->get_loadname = darp_luau_require_get_loadname;
    config->get_cache_key = darp_luau_require_get_cache_key;
    config->get_config_status = darp_luau_require_get_config_status;
    config->get_config = darp_luau_require_get_config;
    config->load = darp_luau_require_load_trampoline;
}

void luau_free(void* ptr)
{
    free(ptr);
}

void darp_luau_pushcallback(
    lua_State* L,
    darp_luau_callback callback,
    void* ctx,
    darp_luau_callback_destructor dtor,
    const char* debugname)
{
    // Only the function refers to this userdata, so Luau frees it once the function is gone.
    void* userdata =
        lua_newuserdatadtor(L, sizeof(darp_luau_callback_context_data), darp_luau_callback_context_destructor);
    auto* context = new (userdata) darp_luau_callback_context_data{callback, ctx, dtor};
    (void)context;

    lua_pushcclosurek(L, darp_luau_callback_trampoline, debugname, 1, darp_luau_callback_continuation);
}

int darp_luau_resumecallback(lua_State* L, lua_State* from, int narg)
{
    lua_pushlightuserdata(L, &darp_luau_callback_resume_mark);
    lua_insert(L, -(narg + 1));
    return lua_resume(L, from, narg + 1);
}

struct darp_luau_member_context_data
{
    darp_luau_member_callback callback;
    void* ctx;
    darp_luau_callback_destructor dtor;
};

static void darp_luau_member_context_destructor(lua_State* L, void* userdata)
{
    (void)L;
    auto* context = static_cast<darp_luau_member_context_data*>(userdata);
    if (context->dtor)
        context->dtor(context->ctx);
}

void darp_luau_pushmembercontext(
    lua_State* L,
    darp_luau_member_callback callback,
    void* ctx,
    darp_luau_callback_destructor dtor)
{
    void* userdata =
        lua_newuserdatadtor(L, sizeof(darp_luau_member_context_data), darp_luau_member_context_destructor);
    auto* context = new (userdata) darp_luau_member_context_data{callback, ctx, dtor};
    (void)context;
}

// Upvalues of a member function.
enum
{
    DARP_LUAU_MEMBER_CONTEXT = 1,
    DARP_LUAU_MEMBER_NUMBER = 2,
};

static int darp_luau_member_function_trampoline(lua_State* L)
{
    auto* context =
        static_cast<darp_luau_member_context_data*>(lua_touserdata(L, lua_upvalueindex(DARP_LUAU_MEMBER_CONTEXT)));
    int member = lua_tointeger(L, lua_upvalueindex(DARP_LUAU_MEMBER_NUMBER));
    int result = context->callback(L, context->ctx, member);

    if (result >= 0)
        return result;

    if (result == DARP_LUAU_CALLBACK_YIELD)
    {
        // Empty the frame, so that the continuation sees exactly what the resume passed.
        lua_settop(L, 0);
        return lua_yield(L, 0);
    }

    darp_luau_raise_top_error(L);
    return 0;
}

void darp_luau_setmemberfunction(lua_State* L, int idx, const char* name, int context_idx, int member)
{
    idx = lua_absindex(L, idx);
    context_idx = lua_absindex(L, context_idx);

    lua_pushstring(L, name);
    lua_pushvalue(L, context_idx);
    lua_pushinteger(L, member);
    lua_pushcclosurek(L, darp_luau_member_function_trampoline, name, 2, darp_luau_callback_continuation);
    lua_rawset(L, idx);
}

// Upvalues of the __index and __newindex of a user type.
enum
{
    DARP_LUAU_ACCESS_CONTEXT = 1,
    // A method is stored as its function, a getter as its member number.
    DARP_LUAU_ACCESS_READABLE = 2,
    DARP_LUAU_ACCESS_SETTERS = 3,
    DARP_LUAU_ACCESS_FALLBACK = 4,
    DARP_LUAU_ACCESS_UPVALUES = DARP_LUAU_ACCESS_FALLBACK,
};

// Pushes what the table in the upvalue `upvalue` holds for the key at `key_idx`, and tells whether it holds anything.
static bool darp_luau_member_lookup(lua_State* L, int upvalue, int key_idx)
{
    lua_pushvalue(L, key_idx);
    if (lua_rawget(L, lua_upvalueindex(upvalue)) != LUA_TNIL)
        return true;

    lua_pop(L, 1);
    return false;
}

// Calls a member with the arguments of the metamethod that is running. No yield can pass a metamethod.
static int darp_luau_member_access(lua_State* L, int member)
{
    auto* context =
        static_cast<darp_luau_member_context_data*>(lua_touserdata(L, lua_upvalueindex(DARP_LUAU_ACCESS_CONTEXT)));
    int result = context->callback(L, context->ctx, member);

    if (result >= 0)
        return result;

    if (result == DARP_LUAU_CALLBACK_YIELD)
        luaL_error(L, "attempt to yield while a userdata member is read or written");

    darp_luau_raise_top_error(L);
    return 0;
}

static int darp_luau_member_index(lua_State* L)
{
    const int key = 2;

    if (lua_type(L, key) == LUA_TSTRING)
    {
        if (darp_luau_member_lookup(L, DARP_LUAU_ACCESS_READABLE, key))
        {
            if (lua_type(L, -1) != LUA_TNUMBER)
                return 1;

            int member = lua_tointeger(L, -1);
            lua_pop(L, 1);
            return darp_luau_member_access(L, member);
        }

        if (darp_luau_member_lookup(L, DARP_LUAU_ACCESS_SETTERS, key))
            luaL_error(L, "userdata member '%s' is write-only", lua_tostring(L, key));
    }

    int fallback = lua_tointeger(L, lua_upvalueindex(DARP_LUAU_ACCESS_FALLBACK));
    if (fallback >= 0)
        return darp_luau_member_access(L, fallback);

    return 0;
}

static int darp_luau_member_newindex(lua_State* L)
{
    const int key = 2;

    if (lua_type(L, key) == LUA_TSTRING)
    {
        if (darp_luau_member_lookup(L, DARP_LUAU_ACCESS_SETTERS, key))
        {
            int member = lua_tointeger(L, -1);
            lua_pop(L, 1);
            darp_luau_member_access(L, member);
            return 0;
        }

        if (darp_luau_member_lookup(L, DARP_LUAU_ACCESS_READABLE, key))
        {
            if (lua_type(L, -1) == LUA_TNUMBER)
                luaL_error(L, "userdata member '%s' is read-only", lua_tostring(L, key));
            luaL_error(L, "userdata method '%s' cannot be assigned", lua_tostring(L, key));
        }
    }

    int fallback = lua_tointeger(L, lua_upvalueindex(DARP_LUAU_ACCESS_FALLBACK));
    if (fallback >= 0)
    {
        darp_luau_member_access(L, fallback);
        return 0;
    }

    if (lua_type(L, key) == LUA_TSTRING)
        luaL_error(L, "attempt to set unknown userdata member '%s'", lua_tostring(L, key));

    luaL_error(L, "attempt to set a userdata member with a %s key", luaL_typename(L, key));
    return 0;
}

// Pushes an __index or __newindex over the context, the readable members and the setters, which are at `first`
// and the two slots above it.
static void darp_luau_pushmemberaccess(
    lua_State* L,
    lua_CFunction function,
    const char* debugname,
    int first,
    int fallback)
{
    for (int i = 0; i < DARP_LUAU_ACCESS_UPVALUES - 1; i++)
        lua_pushvalue(L, first + i);
    lua_pushinteger(L, fallback);
    lua_pushcclosure(L, function, debugname, DARP_LUAU_ACCESS_UPVALUES);
}

void darp_luau_setmemberaccess(lua_State* L, int idx, int index_member, int newindex_member)
{
    idx = lua_absindex(L, idx);
    const int operands = DARP_LUAU_ACCESS_UPVALUES - 1;
    int first = lua_gettop(L) - operands + 1;

    darp_luau_pushmemberaccess(L, darp_luau_member_index, "__index", first, index_member);
    lua_rawsetfield(L, idx, "__index");
    darp_luau_pushmemberaccess(L, darp_luau_member_newindex, "__newindex", first, newindex_member);
    lua_rawsetfield(L, idx, "__newindex");

    lua_pop(L, operands);
}

void* darp_luau_newuserdatawithmetatable(lua_State* L, size_t size, int tag, int metatable_ref)
{
    void* userdata = lua_newuserdatatagged(L, size, tag);
    // The destructor of the tag can run before the host has filled the memory, so it must not find garbage there.
    memset(userdata, 0, size);
    lua_getref(L, metatable_ref);
    lua_setmetatable(L, -2);
    return userdata;
}

struct darp_luau_table_access
{
    int idx;
    int type;
};

static void darp_luau_pgettable_body(lua_State* L, void* ud)
{
    auto* access = static_cast<darp_luau_table_access*>(ud);
    access->type = lua_gettable(L, access->idx);
}

static void darp_luau_psettable_body(lua_State* L, void* ud)
{
    lua_settable(L, static_cast<darp_luau_table_access*>(ud)->idx);
}

int darp_luau_pgettable(lua_State* L, int idx)
{
    darp_luau_table_access access{idx, LUA_TNIL};
    // On an error, luaD_pcall cuts the stack back to the key and puts the error object in its place.
    int status = luaD_pcall(L, darp_luau_pgettable_body, &access, savestack(L, L->top - 1), 0);
    return status == LUA_OK ? access.type : -status;
}

int darp_luau_psettable(lua_State* L, int idx)
{
    darp_luau_table_access access{idx, LUA_TNIL};
    return luaD_pcall(L, darp_luau_psettable_body, &access, savestack(L, L->top - 2), 0);
}

static void darp_luau_interrupt_hook(lua_State* L, int gc)
{
    // Luau also reports the steps of its garbage collector, where a script cannot be stopped.
    if (gc >= 0)
        return;

    auto* interrupt = static_cast<const darp_luau_interrupt*>(lua_callbacks(L)->userdata);
    if (!interrupt->callback(L, interrupt->ctx))
        return;

    if (lua_isyieldable(L))
    {
        lua_break(L);
        return;
    }

    // lua_break would raise an error of its own here. The stack may be full at a safepoint.
    lua_rawcheckstack(L, 1);
    // Without a position: luaL_error would name the caller of the interrupted function, not the function.
    lua_pushliteral(L, "script was interrupted");
    lua_error(L);
}

void darp_luau_setinterrupt(lua_State* L, const darp_luau_interrupt* interrupt)
{
    lua_Callbacks* callbacks = lua_callbacks(L);
    callbacks->userdata = const_cast<darp_luau_interrupt*>(interrupt);
    callbacks->interrupt = interrupt ? darp_luau_interrupt_hook : nullptr;
}

void darp_luau_pushrequirecallback(lua_State* L, darp_luau_callback callback, void* ctx, const char* debugname)
{
    void* userdata = lua_newuserdata(L, sizeof(darp_luau_callback_context_data));
    auto* context = new (userdata) darp_luau_callback_context_data{callback, ctx, nullptr};
    (void)context;

    lua_insert(L, -2);
    lua_pushcclosure(L, darp_luau_require_callback_trampoline, debugname, 2);
}

darp_luau_require_context* darp_luau_newrequirecontext(
    luarequire_Configuration_init config_init,
    darp_luau_require_load_callback load_callback,
    void* ctx)
{
    auto* context = new (std::nothrow) darp_luau_require_context_data{};
    if (!context)
        return nullptr;

    config_init(&context->managed_config);
    context->load_callback = load_callback;
    context->managed_ctx = ctx;
    return context;
}

void darp_luau_freerequirecontext(darp_luau_require_context* context)
{
    delete context;
}

int darp_luau_pushproxyrequire(lua_State* L, darp_luau_require_context* context)
{
    return luarequire_pushproxyrequire(L, darp_luau_proxyrequire_config_init, context);
}
