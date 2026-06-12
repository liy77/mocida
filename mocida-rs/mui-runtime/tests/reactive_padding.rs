// Integration tests for the reactive padding path (Task 5).
//
// Spec section 5.2: the macOS topbar reactive padding fix. The runtime now
// passes a `ReactiveEnv` (built from the live signal slot) into every
// `box_spacing_reactive` call site, so a `paddingLeft:` prop that uses
// `${is_macos == "1" ? 80 : 14}` evaluates to `80.0` on macOS and `14.0`
// elsewhere.
//
// We exercise the runtime→mui-syntax contract end-to-end:
//
//   1. Parse a tiny `.mui` source.
//   2. Build a `mui_runtime::Reactive` and seed it.
//   3. Call `mui_runtime::build_reactive_env` to mirror the runtime's
//      in-build env construction.
//   4. Call `mui_syntax::style::box_spacing_reactive` exactly the way
//      `mui-runtime/src/lib.rs` does on every Stack/Glass/Rectangle/Text/….
//
// This intentionally does NOT drive the full C-side render pipeline
// (`build_view` → mocida widgets), because that path requires a working
// `mocida-sys` build (MOCIDA_INCLUDE_DIR + libmocida.a), which is broken
// in this dev env (a pre-existing workspace-level issue, not caused by
// this change). The 3 cases below cover the reactive-resolution contract
// that the call-site migration actually depends on; the on-screen
// behaviour is verified by the Task 7 manual smoke test.

use mui_runtime::{build_reactive_env, Reactive};
use mui_syntax::ast::Node;
use mui_syntax::style::box_spacing_reactive;

const FIXTURE_STATIC: &str = "view V() { Stack(paddingLeft: 14) {} }";
const FIXTURE_REACTIVE: &str =
    "view V() { Stack(paddingLeft: ${is_macos == \"1\" ? 80 : 14}) {} }";

/// Parse `src`, return the first Element in the first view's body.
fn first_element(src: &str) -> mui_syntax::ast::Element {
    let doc = mui_syntax::parse(src);
    assert!(
        doc.errors.is_empty(),
        "parse errors in fixture: {:?}",
        doc.errors
    );
    let view = doc
        .views
        .into_iter()
        .next()
        .expect("fixture has at least one view");
    for node in view.body {
        if let Node::Element(el) = node {
            return el;
        }
    }
    panic!("fixture had no Element nodes: {src}");
}

#[test]
fn static_padding_left_resolves_to_14() {
    // Regression: a literal `paddingLeft: 14` must still resolve to 14.0
    // through the new reactive-aware call site. The runtime builds a
    // ReactiveEnv for every call (it's cheap: an empty HashMap when no
    // platform signals are set), so the static path is equivalent to the
    // pre-T5 `box_spacing` call.
    let reactive = Reactive::new();
    let env = build_reactive_env(&reactive);
    let el = first_element(FIXTURE_STATIC);
    let (pl, _pt, _pr, _pb) = box_spacing_reactive(&el, "padding", Some(&env))
        .expect("padding is set on the Stack");
    assert_eq!(pl, 14.0, "static paddingLeft: 14 should resolve to 14.0");
}

#[test]
fn reactive_padding_left_is_14_on_non_macos() {
    // The macOS topbar is the only place this matters, but the runtime
    // builds the env unconditionally — so a non-macOS host must still
    // see the spec-mandated `14.0` baseline (the static path's value).
    let reactive = Reactive::new();
    reactive.set_str("is_macos", "0");
    let env = build_reactive_env(&reactive);
    assert_eq!(
        env.signals.get("is_macos").map(String::as_str),
        Some("0"),
        "env must mirror the live signal"
    );
    let el = first_element(FIXTURE_REACTIVE);
    let (pl, _pt, _pr, _pb) = box_spacing_reactive(&el, "padding", Some(&env))
        .expect("padding is set on the Stack");
    assert_eq!(
        pl, 14.0,
        "reactive paddingLeft with is_macos=0 should resolve to 14.0"
    );
}

#[test]
fn reactive_padding_left_is_80_on_macos() {
    // The actual fix: on macOS the host sets `is_macos = "1"`, the
    // runtime's `build_reactive_env` mirrors it into the env, and
    // `box_spacing_reactive` evaluates the ternary to `80.0` — the
    // value the topbar needs to clear the macOS traffic lights.
    let reactive = Reactive::new();
    reactive.set_str("is_macos", "1");
    let env = build_reactive_env(&reactive);
    let el = first_element(FIXTURE_REACTIVE);
    let (pl, _pt, _pr, _pb) = box_spacing_reactive(&el, "padding", Some(&env))
        .expect("padding is set on the Stack");
    assert_eq!(
        pl, 80.0,
        "reactive paddingLeft with is_macos=1 should resolve to 80.0"
    );
}
