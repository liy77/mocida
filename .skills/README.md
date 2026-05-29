# `.skills` — rules for AI agents working on this monorepo

This folder holds **project conventions any AI agent (or human) should follow**
before changing this repository. It is committed to GitHub on purpose and is
**public** — never put secrets, tokens, credentials, personal data, or
machine-specific absolute paths here.

## How to use it

1. Read these files before planning a change.
2. Treat them as hard constraints, not suggestions. If a rule seems wrong,
   surface it to the maintainer instead of silently breaking it.
3. When you establish a new durable convention, add or update a file here in
   the same change.

## Index

| File | What it covers |
| ---- | -------------- |
| [`project-map.md`](project-map.md) | Repo layout, where dependencies and build output live. |
| [`build-test-release.md`](build-test-release.md) | How to build, test, and cut a release. |
| [`code-doc-parity.md`](code-doc-parity.md) | **The golden rule: code must match the docs.** |
| [`rust-bindings.md`](rust-bindings.md) | `mocida-rs` is bindings only — conventions + FFI patterns. |
| [`conventions.md`](conventions.md) | Formatting, commits, cross-platform, logging, secrets. |
