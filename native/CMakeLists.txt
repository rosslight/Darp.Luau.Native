cmake_minimum_required(VERSION 3.21)
project(luau_monolithic_dll LANGUAGES C CXX)

# Build Luau as STATIC libs (important: no cross-DLL internals)
set(LUAU_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_WEB OFF CACHE BOOL "" FORCE)

# We will force C ABI + dllexport ourselves to avoid fighting Luau's built-in settings
set(LUAU_EXTERN_C OFF CACHE BOOL "" FORCE)

add_subdirectory(luau)

if (MSVC)
  set(LUAU_EXPORT_C "extern \"C\" __declspec(dllexport)")
else()
  set(LUAU_EXPORT_C "extern \"C\" __attribute__((visibility(\"default\")))")
endif()

# VM exports: lua_* uses LUA_API; luaL_* + luaopen_* use LUALIB_API
target_compile_definitions(Luau.VM PUBLIC
  "LUA_API=${LUAU_EXPORT_C}"
  "LUALIB_API=${LUAU_EXPORT_C}"
)

# Compiler exports: luau_compile uses LUACODE_API
target_compile_definitions(Luau.Compiler PUBLIC
  "LUACODE_API=${LUAU_EXPORT_C}"
)

# Require runtime exports are declared with LUALIB_API in Require.h
target_compile_definitions(Luau.Require PUBLIC
  "LUALIB_API=${LUAU_EXPORT_C}"
)

add_library(luau SHARED exports.cpp)
target_link_libraries(luau PRIVATE Luau.VM Luau.Compiler Luau.Require)

if (MSVC)
  target_link_options(luau PRIVATE
    "/WHOLEARCHIVE:$<TARGET_LINKER_FILE:Luau.VM>"
    "/WHOLEARCHIVE:$<TARGET_LINKER_FILE:Luau.Compiler>"
    "/WHOLEARCHIVE:$<TARGET_LINKER_FILE:Luau.Require>"
  )
endif()
