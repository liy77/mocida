//! Raw FFI bindings to the [Mocida](https://github.com/liy77/mocida)
//! C UI toolkit.
//!
//! Everything in this crate is `unsafe` and matches the C API 1:1.
//! Most users want the higher-level [`mocida`](../mocida/index.html)
//! crate instead, which wraps the calls below in safe Rust types
//! with `Drop`-based cleanup and builder-style configuration.
//!
//! # Setup
//!
//! Bindings are generated at build time by `bindgen`, so the build
//! script needs to know where the mocida C install lives:
//!
//! ```text
//! $env:MOCIDA_INCLUDE_DIR = "C:\path\to\mocida\src\headers"
//! $env:MOCIDA_LIB_DIR     = "C:\path\to\mocida\build"
//! cargo build
//! ```
//!
//! See `mocida-sys/build.rs` for the full list of env vars.

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(clippy::all)]
#![allow(dead_code)]

// `cfg(docsrs)` and the `docs-only` feature both skip the build script
// (see build.rs). When that happens we don't have a generated
// bindings.rs in OUT_DIR — include a sealed empty shim so the crate
// still compiles for documentation purposes.
#[cfg(any(docsrs, feature = "docs-only"))]
mod bindings {
    // Intentionally empty: the docs build only verifies the crate
    // structure. Re-running cargo without `docs-only` regenerates the
    // real bindings.
}

#[cfg(not(any(docsrs, feature = "docs-only")))]
mod bindings {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

pub use bindings::*;
