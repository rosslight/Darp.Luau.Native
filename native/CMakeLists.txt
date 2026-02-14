cmake_minimum_required(VERSION 3.20)

project(DarpLuauNative LANGUAGES C CXX)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/luau/CMakeLists.txt")
    message(FATAL_ERROR "Expected Luau submodule at native/luau. Run: git submodule update --init --recursive")
endif()

set(LUAU_BUILD_CLI OFF CACHE BOOL "Disable Luau CLI binaries" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "Disable Luau tests" FORCE)
set(LUAU_BUILD_WEB OFF CACHE BOOL "Disable Luau web target" FORCE)
set(LUAU_BUILD_SHARED ON CACHE BOOL "Build Luau shared libraries" FORCE)
set(LUAU_EXTERN_C ON CACHE BOOL "Export Luau C ABI with extern C" FORCE)

set(LUAU_DOTNET_RID "" CACHE STRING "RID used for staged install path")
if(LUAU_DOTNET_RID STREQUAL "")
    message(FATAL_ERROR "LUAU_DOTNET_RID must be provided, e.g. -DLUAU_DOTNET_RID=win-x64")
endif()

add_subdirectory(luau)

# Use a stable binary name for VM so managed declarations can target "luau".
set_target_properties(Luau.VM PROPERTIES OUTPUT_NAME luau)

install(TARGETS Luau.VM Luau.Common
    RUNTIME DESTINATION "${LUAU_DOTNET_RID}"
    LIBRARY DESTINATION "${LUAU_DOTNET_RID}"
    ARCHIVE DESTINATION "${LUAU_DOTNET_RID}")
