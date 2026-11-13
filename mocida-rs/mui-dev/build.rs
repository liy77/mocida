fn main() {
    println!("cargo:rerun-if-env-changed=MOCIDA_LIB_DIR");

    // macOS/Linux: libmocida is linked as @rpath/libmocida.dylib, so the binary
    // needs an rpath to find it without DYLD_LIBRARY_PATH — the build's mocida
    // lib dir, or beside the exe.
    if std::env::var("CARGO_CFG_TARGET_OS").map_or(false, |os| os != "windows") {
        if let Ok(dir) = std::env::var("MOCIDA_LIB_DIR") {
            if !dir.is_empty() {
                println!("cargo:rustc-link-arg=-Wl,-rpath,{dir}");
            }
        }
        if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("macos") {
            println!("cargo:rustc-link-arg=-Wl,-rpath,@executable_path");
        } else {
            println!("cargo:rustc-link-arg=-Wl,-rpath,$ORIGIN");
        }
    }
}
