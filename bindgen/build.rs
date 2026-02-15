use std::error::Error;

fn main() -> Result<(), Box<dyn Error>> {
    // using bindgen, generate binding code
    bindgen::Builder::default()
        .header("../native/luau/VM/include/lua.h")
        .header("../native/luau/VM/include/luaconf.h")
        .header("../native/luau/VM/include/lualib.h")
        .header("../native/luau/Compiler/include/luacode.h")
        .default_enum_style(bindgen::EnumVariation::Rust {
            non_exhaustive: false,
        })
        .generate()?
        .write_to_file("src/generated.rs")?;

    // csbindgen code, generate C# dll import
    csbindgen::Builder::default()
        .input_bindgen_file("src/generated.rs")
        .csharp_dll_name("luau")
        .csharp_namespace("Darp.Luau.Native")
        .csharp_class_name("LuauNative")
        .csharp_class_accessibility("public")
        .csharp_generate_const_filter(|name| name.starts_with("LUA_") || name.starts_with("LUAI_"))
        .always_included_types(["lua_Status", "lua_CoStatus", "lua_Type", "lua_GCOp"])
        .generate_csharp_file("../src/Darp.Luau.Native/LuauNative.g.cs")?;

    Ok(())
}