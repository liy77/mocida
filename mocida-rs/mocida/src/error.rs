//! Error type for fallible mocida calls.

use std::ffi::NulError;
use std::fmt;

/// Result alias used throughout the crate.
pub type Result<T> = std::result::Result<T, Error>;

/// Errors surfaced by the safe wrapper.
#[derive(Debug)]
pub enum Error {
    /// A C function that allocates (e.g. `UIApp_Create`) returned NULL.
    /// The C library logs the underlying cause to stderr.
    Null(&'static str),
    /// Passed a Rust string containing an interior NUL byte to a C API.
    Nul(NulError),
    /// Failed to convert a borrowed `CStr` to a Rust `&str`.
    Utf8(std::str::Utf8Error),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Null(ctx) => write!(f, "mocida returned NULL from {ctx}"),
            Error::Nul(e) => write!(f, "input string contained NUL byte: {e}"),
            Error::Utf8(e) => write!(f, "C string was not valid UTF-8: {e}"),
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Error::Null(_) => None,
            Error::Nul(e) => Some(e),
            Error::Utf8(e) => Some(e),
        }
    }
}

impl From<NulError> for Error {
    fn from(e: NulError) -> Self {
        Error::Nul(e)
    }
}

impl From<std::str::Utf8Error> for Error {
    fn from(e: std::str::Utf8Error) -> Self {
        Error::Utf8(e)
    }
}
