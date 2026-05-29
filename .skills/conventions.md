# Conventions

## Security / privacy (this repo is public)

- **Never commit secrets**: tokens, API keys, credentials, `.env` files,
  signing keys, or personal data (emails, real names tied to private info,
  home directory paths). Keep everything in `.skills/` public-safe.
- Use **repo-relative paths** in docs and scripts, not machine-specific
  absolute paths.

## Commits

- **Do not attribute AI tools as co-authors.** No `Co-Authored-By:` trailers
  for assistants, no AI "Generated with" footers in commit messages or PRs.
  Authorship stays human.
- Keep commits focused; update docs in the same commit as the behavior change
  (see `code-doc-parity.md`).

## Formatting (pre-commit)

- **C** — `clang-format` (`.clang-format`), scoped to `mocida/src` only; the
  vendored SDL trees are excluded.
- **Rust** — `cargo fmt` (`rustfmt.toml`) over the `mocida-rs/` workspace.
- The hooks only touch staged files — never mass-reformat the tree.

## Cross-platform

- Build/release scripts are **pure stdlib Python 3** and must run on Windows,
  Linux and macOS. Don't hardcode `cmd`/`bash`-only behavior; branch on the
  platform when needed.

## C logging format (for parity in docs/illustrations)

Terminal output is:

```
HH:MM:SS.mmm LEVEL [category] (file:line func) message
```

- Level is one of `TRACE DEBUG INFO WARN ERROR FATAL`, color-coded on a TTY
  (INFO green, WARN yellow, ERROR red).
- Categories are dotted, e.g. `mocida.core`, `mocida.render`, `mocida.layout`,
  `mocida.memory`. Any logger example in docs/SVGs must use this real format.
