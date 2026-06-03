# MUI — architecture (draft v0)

How `.mui` / `.crm` go from text to pixels, in **two modes** that share one
component AST.

```
              ┌──────────── .mui / .crm source ────────────┐
              │                                             │
              ▼                                             ▼
        mui-syntax (lexer + parser, reuses copper-syntax/copper-parser)
              │
              ▼
     ┌───────────────────────────── COMPONENT AST ─────────────────────────────┐
     │  views, elements, props, bindings, control-flow, embedded Copper exprs   │
     └───────────────┬───────────────────────────────────────┬─────────────────┘
                     │ DEV (live design)                       │ RELEASE
                     ▼                                         ▼
        mui-runtime  +  Evaluator (mui-jit, Cranelift)    cforge codegen
        builds/reconciles the mocida tree via mocida-rs   → Copper/Rust calling
        + binds Signals; file watcher reloads on save        the `mocida` crate
                     │                                         ▼
                     ▼                                     cargo build → native
            live window (hot-reload)                       binary (no JIT shipped)
```

Key idea: **interpret/JIT in dev, transpile in release.** The release path is
cheap because Copper already transpiles to Rust and mocida already has Rust
bindings — codegen just emits `mocida-rs` builder calls.

---

## Crates

| Crate | Repo | Responsibility |
| ----- | ---- | -------------- |
| `mui-syntax` | copper-lang (next to `copper-syntax`) | Lexer + parser for `.mui` and the `view{}` block; produces the **component AST**. Reuses Copper's tokenizer/expr parser for everything inside `{ }`. |
| `cforge` (extend) | copper-lang | Recognize `.mui`/`.crm`; **release codegen**: lower the component AST to Copper/Rust that calls the `mocida` crate. |
| `mui-jit` | copper-lang | Lower the embedded-Copper subset (handlers, prop exprs, `computed`/`effect`) to native code via **Cranelift**. Sits behind `trait Evaluator`. |
| `mui-runtime` | mocida-rs workspace | Build + reconcile the mocida widget tree from the component AST via `mocida-rs`; wire `Signal` bindings; own the diff. |
| `mui-dev` | mocida-rs workspace | The live-design host binary: `mui-runtime` + an `Evaluator` + a file watcher (`notify`). Renders a `.mui`/`.crm` and hot-reloads it. |

Dependency direction stays acyclic: `mui-syntax` is language-only;
`mui-runtime` depends on `mocida-rs` + the AST; `mui-dev` depends on both and
on an `Evaluator` impl.

---

## The `Evaluator` trait (why JIT slots in cleanly)

The runtime never calls Cranelift directly — it talks to an `Evaluator`. This
lets a tiny tree-walker bring the design loop up *now* and Cranelift replace it
later **without touching the runtime or the syntax**.

```rust
/// Compiles/holds the embedded Copper of one component instance and runs its
/// handlers / computed exprs against the current signal environment.
pub trait Evaluator {
    /// Prepare a component's embedded code (handlers, computed, effects).
    fn load(&mut self, unit: &ComponentAst) -> Result<EvalHandle>;
    /// Evaluate an expression node to a Value (prop bindings, ${...}).
    fn eval(&mut self, h: EvalHandle, expr: ExprId, env: &SignalEnv) -> Value;
    /// Invoke a handler closure (onClick, onChange, …).
    fn call(&mut self, h: EvalHandle, handler: HandlerId, args: &[Value], env: &SignalEnv);
}
```

- **v1 impl — `TreeWalk`** (in `mui-jit` or a sibling): walks the Copper AST
  subset directly. Fast to write, good enough for design iteration.
- **target impl — `CraneliftJit`**: lowers the same subset to IR → machine
  code; caches compiled handlers; near-native on hot paths.

### JIT prerequisite (be honest about sequencing)

Copper today is *token-rewrite → Rust*. Cranelift needs a **typed AST/IR** for
the subset it compiles. So:

1. Grow `copper-parser`/`copper-syntax` into a typed AST for the **expression
   + statement subset** MUI needs (arithmetic, comparisons, strings, `if`,
   `match`, calls to bound fns, signal read/write). Not all of Copper.
2. Ship `TreeWalk` over that AST → design loop is live.
3. Add `CraneliftJit` behind the same trait; widen the subset over time.

This honors "JIT from the start" (it's the target engine) without blocking the
primary goal (live design), which only needs the markup runtime.

---

## Reactivity & binding (lean on mocida `Signal`)

- `signal(v)` ↔ mocida `Signal<T>` (`mocida-rs::Signal`).
- A bound prop subscribes to the signals its expression read; on change the
  runtime re-evals the expr and calls the widget's setter (e.g.
  `text::by_ptr::set_text`, `Slider::set_value`). Granular — no tree rebuild.
- Two-way inputs register an `onChange` that writes the signal, with an
  equality guard to break feedback loops.

## Reconciliation (hot-reload without losing state)

On file change:
1. `mui-syntax` re-parses → new component AST.
2. `mui-runtime` diffs new vs live tree, matching by `key:` then structural
   path + type (using `UIWidget_SetId` / `UIWidget_FindByData` / `walker`).
3. Reused nodes keep their bound signals (state survives); changed props
   re-apply; deltas are created/destroyed.
4. The `Evaluator` reloads the component's embedded code; handler identities
   are re-bound to the reused widgets.

## Release codegen

`cforge build app.crm`:
1. Parse to the component AST.
2. Lower each `view` to a Copper/Rust function returning a `mocida-rs` widget
   tree (builders + `Signal` wiring). `${expr}` and handlers become normal
   Copper closures/trampolines.
3. Hand off to the existing Copper→Rust pipeline; `cargo build` produces a
   native binary. **No parser/JIT in the shipped artifact.**

---

## Milestones

- **M0 — Spec** *(this folder)*: grammar, component model, dual-mode design. ✅
- **M1 — Static runtime** ✅: `mui-syntax` parses (incl. element args/props,
  `#rrggbb` colors, `Enum.Member`, handlers) → `mui-runtime` builds a mocida
  tree via `mocida-rs` (no logic). `mui-dev <file>` renders a `.mui`/`.crm` in
  a window. Unmapped widgets render a labelled placeholder so any design is
  visible while the widget surface grows.
- **M2 — Hot-reload of design** ✅: edit a `.mui`/`.crm` and the window updates
  in place — no close, no restart. A background thread polls the file's mtime
  (~150 ms, std-only, off the UI thread) and flips an atomic flag; a new
  per-frame `App::on_tick` hook (added to the C loop — one null-check per frame,
  zero cost when unused) reads the flag and, only on a real change, re-parses +
  rebuilds the tree and swaps it via `UIApp_SetChildren`. A syntactically broken
  edit keeps the last good render (the build refuses trees with parse errors).
  Currently a full re-interpret per save (not yet diff/reconcile, but state is
  re-seeded so the visual is correct). `cforge run` drives it via `mui-dev`.
- **M3 — Logic (reactivity core)** ✅ *(counter subset)*: `mut x = signal(n)`
  becomes a real mocida `Signal<i32>`; `${x}` text **subscribes** and updates
  via `by_ptr::set_text` on change; `Button(label, onClick: { x += 1 })` is a
  real button whose click mutates the signal (firing the subscriptions
  synchronously). The counter example is fully live in **both** `mui-runtime`
  (dev / `cforge run`) and `mui-codegen` (native / `cforge -c -r`). Recognised
  handler forms: `x += n`, `x -= n`, `x++`, `x--`, `x = x ± n`, `x = <int>`
  ([`HandlerAction`] in `mui-syntax`). Richer handler bodies, `computed`,
  `effect`, and reactive `if`/`for` re-evaluation are still ahead (a fuller
  TreeWalk evaluator).
- **Styling + anchors** ✅: widgets accept the mocida styling surface —
  `radius`, `borderWidth`, fill (`background`/`bg`/`color`, alpha = opacity, via
  `#rrggbb[aa]` or `rgba()`), `textColor`, `cursor` — plus `anchor:` (parent
  alignment: `center`/`top`/`bottomRight`/…). Shared extraction in
  `mui-syntax::style` keeps runtime + codegen in lockstep.
- **App config + bundle** ✅: a top-level `app { name:, id:, title:, width:,
  height:, background:, entry: }` block configures the window + bundle identity;
  both dev and codegen apply it (`App::set_name`/`set_app_id`, window size/bg,
  entry-view selection). `App::new` already auto-loads an `./app.bundle`
  manifest (`{name,id,assets{}}`, `mocida://` asset URIs); `cforge -c` copies a
  sibling `app.bundle` into `dist/mui/` so the native build loads it too.
- **Component import / reuse** ✅: `import { Card } from "./card.mui"` brings a
  view from another file into scope; `Card(title: "x")` then instantiates it —
  the call's args bind to the component's params, and its body is inlined (in
  dev via `build_view_with` + a registry; in codegen the body is emitted at each
  call site). `mui_syntax::loader::load` resolves the import graph (relative
  paths, transitive, cycle-guarded) into a `Registry`. Examples:
  `card.mui` + `dashboard.mui`.
- **M4 — Cranelift JIT**: swap the tree-walk for a JIT; widen the subset.
- **M5 — Release codegen** ✅: `cforge -c foo.mui` lowers the component AST to
  **Rust source** (the `mui-codegen` crate in copper-lang) that builds the
  mocida tree via `mocida-rs`; `-r`/`--release` also `cargo build`s it into a
  native binary in `dist/mui/`. Now emits the **reactive** subset too (real
  `Signal`s, text subscriptions, button click handlers — same surface as M3).
  `mui-codegen` is pure AST→text (no mocida dep) so it stays portable; only the
  generated crate links mocida.
- **M6 — Tooling** *(largely done ahead of order)*: the VS Code MUI extension
  already ships highlight + completion + hover + diagnostics + file icons for
  `.mui`/`.crm`; a `mui-lsp` on `mui-syntax` would add cross-file analysis.

### M1 — what landed

| Piece | Where | Status |
| ----- | ----- | ------ |
| Element args → positional + props + handlers + `key` | `copper-lang/crates/mui-syntax` | ✅ |
| `#rrggbb`/`#rrggbbaa` colors, `Enum.Member` literals | `mui-syntax` (pre-lex color rewrite) | ✅ |
| `mui-runtime`: AST → mocida `Children` (`Stack`, `Text`, props, colors) | `mocida-rs/mui-runtime` | ✅ |
| `mui-dev <file>`: render a `.mui`/`.crm` in a window | `mocida-rs/mui-dev` | ✅ |
| Interpolation: `"${x}"` shown as `{x}` (no evaluator yet) | `mui-runtime` | ✅ (static) |

Run it: `cargo run -p mui-dev -- editors/vscode-mui/examples/hello.mui` (set
`MOCIDA_INCLUDE_DIR`/`MOCIDA_LIB_DIR`; see `mocida-rs/EXAMPLES.md`).

## Risks

- **Copper IR maturity** gates the JIT — prioritize the typed-AST subset.
- **ABI boundary** mocida↔Copper: standardize on the `mocida-rs` C-ABI for
  both dev (FFI) and release (codegen) so there's one source of truth.
- **Two-way binding loops** — enforce the equality-guard convention.
