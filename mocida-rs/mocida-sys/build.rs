//! Build script for `mocida-sys`.
//!
//! Discovers the mocida install via env vars and:
//!   1. Runs `bindgen` on `wrapper.h` against the mocida public headers.
//!   2. Emits `cargo:rustc-link-search` + `cargo:rustc-link-lib` so the
//!      generated `extern "C"` calls resolve at link time.
//!
//! Required env vars:
//!   MOCIDA_INCLUDE_DIR  Path containing the `uikit/` header tree
//!                       (e.g. <repo>/src/headers).
//!   MOCIDA_LIB_DIR      Path containing the import lib / static lib
//!                       (e.g. <repo>/build).
//!
//! Optional:
//!   MOCIDA_LIB_NAME     Library base name passed to `-l` (default
//!                       "mocida"). On MSVC the linker looks for
//!                       <name>.lib; on GNU for lib<name>.a/.so.
//!   MOCIDA_STATIC       "1" to link statically (`static=`), anything
//!                       else for dynamic (default dynamic).
//!   SDL3_INCLUDE_DIR    Additional include path for SDL3 headers, used
//!                       when the `sdl3-headers` feature is enabled.

use std::env;
use std::path::PathBuf;

fn main() {
    println!("cargo:rerun-if-changed=wrapper.h");
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-env-changed=MOCIDA_INCLUDE_DIR");
    println!("cargo:rerun-if-env-changed=MOCIDA_LIB_DIR");
    println!("cargo:rerun-if-env-changed=MOCIDA_LIB_NAME");
    println!("cargo:rerun-if-env-changed=MOCIDA_STATIC");
    println!("cargo:rerun-if-env-changed=SDL3_INCLUDE_DIR");

    // docs.rs / `--features docs-only`: skip everything, ship the
    // pre-generated bindings shipped in src/bindings_prebuilt.rs.
    if env::var_os("CARGO_FEATURE_DOCS_ONLY").is_some()
        || env::var_os("DOCS_RS").is_some()
    {
        println!("cargo:warning=mocida-sys: docs-only build, skipping bindgen and link.");
        return;
    }

    // Resolve the include directory:
    //   1. honor MOCIDA_INCLUDE_DIR explicitly if set,
    //   2. otherwise read it straight from HKCU\Environment on
    //      Windows (covers the common case where the user already
    //      configured it at OS level but the current shell snapshot
    //      missed the update),
    //   3. otherwise fall back to the standard installer location
    //      (%LOCALAPPDATA%\Programs\Mocida\include).
    let include_dir = resolve_dir("MOCIDA_INCLUDE_DIR", "include")
        .unwrap_or_else(|| {
            panic!(
                "MOCIDA_INCLUDE_DIR could not be resolved.\n\n\
                 Tried (in order):\n\
                 - $env:MOCIDA_INCLUDE_DIR\n\
                 - HKCU:\\Environment\\MOCIDA_INCLUDE_DIR (Windows)\n\
                 - %LOCALAPPDATA%\\Programs\\Mocida\\include\n\n\
                 Point any of these at the directory containing the `uikit/` header tree, \
                 or build with `--features docs-only` to skip native wiring."
            )
        });

    let lib_dir = resolve_dir("MOCIDA_LIB_DIR", "lib");
    let lib_name = env::var("MOCIDA_LIB_NAME").unwrap_or_else(|_| "mocida".to_string());
    let link_static = env::var("MOCIDA_STATIC").map(|v| v == "1").unwrap_or(false);

    if let Some(lib_dir) = &lib_dir {
        println!("cargo:rustc-link-search=native={}", lib_dir.display());
        // Mirror every .dll from the lib dir into the cargo target
        // dirs that hold binaries/examples/tests, so Windows finds
        // them at run-time without the user having to copy by hand.
        // Cargo only refreshes the build script when an input
        // changes — we register each DLL with `rerun-if-changed`
        // below so a new mocida release triggers a re-copy.
        if cfg!(target_os = "windows") {
            stage_runtime_dlls(lib_dir);
        }
    } else {
        println!(
            "cargo:warning=mocida-sys: MOCIDA_LIB_DIR not set; relying on \
             the linker's default search paths."
        );
    }

    let link_kind = if link_static { "static" } else { "dylib" };
    println!("cargo:rustc-link-lib={}={}", link_kind, lib_name);

    // Mocida pulls in WebView2, Media Foundation, DirectComposition, ...
    // Surface the most common Win32 system libs so consumers don't have
    // to repeat them. Harmless when unused (the linker drops them).
    if cfg!(target_os = "windows") {
        for lib in [
            "user32", "gdi32", "shell32", "ole32", "oleaut32", "uuid",
            "shcore", "advapi32", "dwmapi", "imm32", "version", "winmm",
            "setupapi", "mfplat", "mfreadwrite", "mfuuid", "mf",
        ] {
            println!("cargo:rustc-link-lib=dylib={}", lib);
        }
    }

    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("OUT_DIR set by cargo"));
    let bindings_path = out_dir.join("bindings.rs");

    let mut builder = bindgen::Builder::default()
        .header("wrapper.h")
        .clang_arg(format!("-I{}", include_dir.display()))
        .derive_default(true)
        .derive_debug(true)
        .derive_copy(true)
        .layout_tests(false)
        .generate_comments(true)
        .prepend_enum_name(false)
        .default_enum_style(bindgen::EnumVariation::NewType {
            is_bitfield: false,
            is_global: true,
        })
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        // Public mocida API only; don't pull every SDL/Windows symbol.
        .allowlist_function("UI.*|widgc.*|MOCIDA_.*|SDL_GetPerformanceCounter|SDL_GetPerformanceFrequency|SDL_GetTicks")
        .allowlist_type("UI.*|FontStyle|FontEntry|HWND|HWND__")
        .allowlist_var("UI_.*|MOCIDA_.*");

    if env::var_os("CARGO_FEATURE_SDL3_HEADERS").is_some() {
        if let Some(sdl) = env::var_os("SDL3_INCLUDE_DIR") {
            builder = builder.clang_arg(format!("-I{}", PathBuf::from(sdl).display()));
        }
    } else {
        // No SDL3 on disk: synthesize a tiny `SDL3/` and `SDL3_ttf/`
        // stub tree and put it on the include path BEFORE the real
        // mocida headers. The stubs declare just the opaque types the
        // public API touches (SDL_Window, SDL_Surface, SDL_Scancode,
        // Uint16, Uint64); enabling `sdl3-headers` swaps them for the
        // real definitions.
        let stub_root = out_dir.join("sdl_stub");
        let sdl3_dir = stub_root.join("SDL3");
        let ttf_dir = stub_root.join("SDL3_ttf");
        let img_dir = stub_root.join("SDL3_image");
        std::fs::create_dir_all(&sdl3_dir).expect("mkdir SDL3 stub");
        std::fs::create_dir_all(&ttf_dir).expect("mkdir SDL3_ttf stub");
        std::fs::create_dir_all(&img_dir).expect("mkdir SDL3_image stub");
        // Stubs are content-addressed: we only touch them when the
        // bytes change. Re-writing identical content would bump
        // their mtime and trick cargo into thinking the build
        // script's outputs are stale, forcing a useless rebuild of
        // every downstream crate on every `cargo build`.
        write_if_changed(&sdl3_dir.join("SDL.h"), sdl_shim());
        let satellite = |guard: &str| {
            format!(
                "#ifndef {guard}\n\
                 #define {guard}\n\
                 #include <SDL3/SDL.h>\n\
                 #endif\n"
            )
        };
        write_if_changed(&sdl3_dir.join("SDL_stdinc.h"), &satellite("SDL_stdinc_h_"));
        write_if_changed(&sdl3_dir.join("SDL_timer.h"), &satellite("SDL_timer_h_"));
        write_if_changed(&ttf_dir.join("SDL_ttf.h"), &satellite("SDL_ttf_h_"));
        write_if_changed(&img_dir.join("SDL_image.h"), &satellite("SDL_image_h_"));

        builder = builder.clang_arg(format!("-I{}", stub_root.display()));
    }

    match builder.generate() {
        Ok(bindings) => {
            bindings
                .write_to_file(&bindings_path)
                .expect("write bindings.rs");
        }
        Err(e) => {
            panic!(
                "bindgen failed: {e}\n\n\
                 Verify MOCIDA_INCLUDE_DIR points at the directory \
                 containing `uikit/*.h`. Current value: {}",
                include_dir.display()
            );
        }
    }
}

/// Write `contents` to `path`, but only if the file doesn't exist
/// or its current bytes differ. Lets the SDL stub headers stay on a
/// stable mtime across builds, which keeps cargo's fingerprint
/// happy and avoids spurious "everything is stale" rebuilds.
fn write_if_changed(path: &std::path::Path, contents: &str) {
    use std::fs;
    if let Ok(existing) = fs::read(path) {
        if existing == contents.as_bytes() {
            return;
        }
    }
    fs::write(path, contents).expect("write stub header");
}

/// Copy every `.dll` from `lib_dir` into the cargo target dirs
/// (`target/<profile>/`, `target/<profile>/examples/`,
/// `target/<profile>/deps/`) so Windows finds them next to whichever
/// `.exe` cargo just built.
///
/// Each DLL is registered with `cargo:rerun-if-changed=`, so the
/// build script re-runs (and the copy re-happens) whenever the user
/// upgrades the upstream Mocida install — no more "the C side
/// shipped a fix but my .exe still uses the old DLL" surprises.
fn stage_runtime_dlls(lib_dir: &PathBuf) {
    use std::fs;

    let entries = match fs::read_dir(lib_dir) {
        Ok(e) => e,
        Err(_) => return,
    };
    let dlls: Vec<PathBuf> = entries
        .flatten()
        .map(|e| e.path())
        .filter(|p| {
            p.extension()
                .and_then(|s| s.to_str())
                .map(|s| s.eq_ignore_ascii_case("dll"))
                .unwrap_or(false)
        })
        .collect();
    if dlls.is_empty() {
        return;
    }

    // Walk OUT_DIR up to the cargo target root: OUT_DIR is
    // `<target>/<profile>/build/<crate>-<hash>/out`, so four
    // ancestors gets us to `<target>/<profile>/`.
    let out_dir = match env::var_os("OUT_DIR") {
        Some(v) => PathBuf::from(v),
        None => return,
    };
    let profile_dir = match out_dir.ancestors().nth(3) {
        Some(p) => p.to_path_buf(),
        None => return,
    };

    let dest_dirs = [
        profile_dir.clone(),
        profile_dir.join("examples"),
        profile_dir.join("deps"),
    ];

    for dest in &dest_dirs {
        let _ = fs::create_dir_all(dest);
    }

    for src in &dlls {
        println!("cargo:rerun-if-changed={}", src.display());
        let Some(name) = src.file_name() else { continue };
        for dest_dir in &dest_dirs {
            let dest = dest_dir.join(name);
            // Only copy when the destination is missing or older —
            // avoids unnecessary writes (cheap modification-time
            // check on Windows).
            let needs_copy = match (fs::metadata(&dest), fs::metadata(src)) {
                (Ok(dm), Ok(sm)) => match (dm.modified(), sm.modified()) {
                    (Ok(dt), Ok(st)) => st > dt,
                    _ => true,
                },
                _ => true,
            };
            if needs_copy {
                let _ = fs::copy(src, &dest);
            }
        }
    }
}

/// Resolve a directory used by the build (`include` or `lib`).
///
/// Looks in three places, in order:
///
///   1. The named env var (`MOCIDA_INCLUDE_DIR` / `MOCIDA_LIB_DIR`).
///   2. On Windows, the same name under `HKCU\Environment` —
///      catches the very common case where the user configured it
///      at OS level but the current shell didn't refresh its env
///      snapshot.
///   3. The installer's standard layout under
///      `%LOCALAPPDATA%\Programs\Mocida\<subdir>`.
///
/// Returns `None` only when none of those resolve to an existing
/// directory.
fn resolve_dir(env_name: &str, installer_subdir: &str) -> Option<PathBuf> {
    if let Some(v) = env::var_os(env_name) {
        let p = PathBuf::from(v);
        if p.is_dir() {
            return Some(p);
        }
    }

    #[cfg(target_os = "windows")]
    if let Some(p) = read_hkcu_env(env_name) {
        if p.is_dir() {
            println!(
                "cargo:warning=mocida-sys: {env_name} resolved from HKCU\\Environment ({})",
                p.display()
            );
            return Some(p);
        }
    }

    #[cfg(target_os = "windows")]
    if let Some(local) = env::var_os("LOCALAPPDATA") {
        let p = PathBuf::from(local)
            .join("Programs")
            .join("Mocida")
            .join(installer_subdir);
        if p.is_dir() {
            println!(
                "cargo:warning=mocida-sys: {env_name} auto-detected at {} (installer default)",
                p.display()
            );
            return Some(p);
        }
    }

    None
}

/// Read a single value from `HKCU\Environment` via PowerShell. We
/// avoid linking against the Windows API directly so the build
/// script stays portable to non-Windows hosts.
#[cfg(target_os = "windows")]
fn read_hkcu_env(name: &str) -> Option<PathBuf> {
    use std::process::Command;
    let out = Command::new("powershell")
        .args([
            "-NoProfile",
            "-NonInteractive",
            "-Command",
            &format!(
                "[Environment]::GetEnvironmentVariable('{}', 'User')",
                name.replace('\'', "''")
            ),
        ])
        .output()
        .ok()?;
    if !out.status.success() {
        return None;
    }
    let s = String::from_utf8(out.stdout).ok()?;
    let trimmed = s.trim();
    if trimmed.is_empty() {
        None
    } else {
        Some(PathBuf::from(trimmed))
    }
}

/// Minimal stand-ins for SDL/Win32 symbols referenced by the public
/// mocida headers. These let bindgen parse the headers without a full
/// SDL3 install. The Rust side treats them as opaque pointers, so the
/// concrete shape doesn't have to match SDL's.
fn sdl_shim() -> &'static str {
    // Header guard matches what SDL3's real SDL.h uses (`SDL_h_`) so
    // mocida headers that include <SDL3/SDL.h> repeatedly only parse
    // the body once.
    r#"
#ifndef SDL_h_
#define SDL_h_

#include <stdint.h>
#include <stddef.h>

typedef uint8_t  Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef int8_t   Sint8;
typedef int16_t  Sint16;
typedef int32_t  Sint32;
typedef int64_t  Sint64;

typedef struct SDL_Window       SDL_Window;
typedef struct SDL_Renderer     SDL_Renderer;
typedef struct SDL_Texture      SDL_Texture;
typedef struct SDL_Surface      SDL_Surface;
typedef struct SDL_AudioStream  SDL_AudioStream;
typedef int SDL_Scancode;
typedef Uint32 SDL_Keycode;

/* A few function prototypes referenced by `static inline` helpers in
 * the public headers. Only the signatures matter — bindgen records
 * them but the link is satisfied by the real mocida/SDL3 binary. */
extern Uint64 SDL_GetPerformanceCounter(void);
extern Uint64 SDL_GetPerformanceFrequency(void);
extern Uint64 SDL_GetTicks(void);

#endif /* SDL_h_ */
"#
}
