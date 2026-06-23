#include "include/luau_api.h"

#include <new>
#include <stdlib.h>
#include <string.h>

struct darp_luau_callback_context_data
{
    darp_luau_callback callback;
    void* ctx;
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

static int darp_luau_callback_trampoline(lua_State* L)
{
    auto* context = static_cast<darp_luau_callback_context_data*>(lua_touserdata(L, lua_upvalueindex(1)));
    int result = context->callback(L, context->ctx);

    if (result >= 0)
        return result;

    darp_luau_raise_top_error(L);
    return 0;
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

void darp_luau_pushcallback(lua_State* L, darp_luau_callback callback, void* ctx, const char* debugname)
{
    void* userdata = lua_newuserdata(L, sizeof(darp_luau_callback_context_data));
    auto* context = new (userdata) darp_luau_callback_context_data{callback, ctx};
    (void)context;

    lua_pushcclosure(L, darp_luau_callback_trampoline, debugname, 1);
}

void darp_luau_pushrequirecallback(lua_State* L, darp_luau_callback callback, void* ctx, const char* debugname)
{
    void* userdata = lua_newuserdata(L, sizeof(darp_luau_callback_context_data));
    auto* context = new (userdata) darp_luau_callback_context_data{callback, ctx};
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
