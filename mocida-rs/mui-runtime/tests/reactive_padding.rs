// Integration tests for the reactive padding path.
//
// Task 4 scaffold: only verifies the ReactiveEnv import path works and
// the build_reactive_env helper is reachable from the runtime's test
// crate. Full integration tests (static vs reactive padding) land in Task 5.

use mui_syntax::style::ReactiveEnv;

#[test]
fn reactive_env_default_is_empty() {
    let env = ReactiveEnv::default();
    assert!(env.signals.is_empty());
}
