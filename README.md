# Mocida (monorepo)

This repository is a monorepo containing the Mocida C UI toolkit and its Rust bindings.

## Layout

| Path         | Contents                                                                 |
| ------------ | ------------------------------------------------------------------------ |
| `mocida/`    | The Mocida C library — a Windows-first UI toolkit on SDL3 (sources, build scripts, vendored SDL/SDL_image/SDL_ttf, tests, docs). See `mocida/README.md`. |
| `mocida-rs/` | The Rust bindings: a Cargo workspace with the `mocida-sys` FFI crate and the idiomatic `mocida` wrapper. See `mocida-rs/README.md`. |
| `.github/`   | Monorepo CI workflows.                                                   |

The Rust crate (`mocida-sys/build.rs`) links the C library via environment variables:
`MOCIDA_INCLUDE_DIR` (the headers dir, i.e. `mocida/src/headers`) and `MOCIDA_LIB_DIR`
(the build output dir), with optional `MOCIDA_LIB_NAME` / `MOCIDA_STATIC`.

## Formatting and pre-commit hooks

Formatting is enforced on **changed/staged files only** via the
[pre-commit](https://pre-commit.com/) framework:

- **C** — `clang-format` (config: `.clang-format`), scoped to `mocida/src` only.
  Vendored SDL/SDL_image/SDL_ttf trees are excluded.
- **Rust** — `cargo fmt` (config: `rustfmt.toml`), scoped to the `mocida-rs/` workspace.

### One-time setup

Run from the repository root:

```sh
# 1. Install the pre-commit framework (pick one):
pipx install pre-commit
# or: pip install --user pre-commit

# 2. Install the git hook into this repo:
pre-commit install
```

Prerequisites the hooks invoke:

- **clang-format** — the `mirrors-clang-format` hook vendors its own binary
  (pinned to v18.1.8), so no separate install is needed. A system `clang-format`
  on `PATH` also works.
- **rustfmt** — `rust-toolchain.toml` pins the `stable` channel with the `rustfmt`
  component, so `cargo fmt` works after a normal `rustup` install:
  `rustup component add rustfmt`.

The hooks reformat only the files you stage at commit time; they never mass-reformat
the tree.
