# MUI — the mocida UI language

> **Design draft (M0).** This folder fixes the *design* of MUI before any
> compiler/runtime code is written. Nothing here is wired up yet.

MUI is a declarative UI language for the [mocida](../mocida/README.md) toolkit,
with logic written in [Copper](https://github.com/liy77/copper-lang). It
renders mocida widget trees and is built to **hot-reload designs live**.

## Files

| File | What |
| ---- | ---- |
| [`SPEC.md`](SPEC.md) | The language: component model, syntax, grammar (EBNF), reactivity, widget mapping. |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | Dual-mode execution, crates, the `Evaluator` trait (TreeWalk → Cranelift), reconciliation, release codegen, milestones. |
| [`examples/`](examples/) | Canonical `.mui` / `.crm` samples. |

## The shape, in one screen

```crm
// counter.mui
view Counter(start: int = 0) {
  mut count = signal(start)                  // Copper binding: `mut` (no `let`)
  Stack(orientation: vertical, gap: 12) {
    Text("Count: ${count}", size: 24)
    Button("+", onClick: { count += 1 })
    Slider(min: 0, max: 100, value: count)   // two-way bound to the signal
  }
}
```

## Decisions locked in M0

- **Components are `view` functions** (`view Name(props) { ... }`) — React/
  Leptos-style; props are typed params, state is `signal(...)`.
- **Two file types**: `.mui` (markup-first, like `.jsx`) and `.crm`
  (Copper + Mocida, like `.tsx`). Same component AST.
- **Dual-mode execution**: interpret/JIT in **dev** (instant hot-reload),
  transpile to Copper→Rust calling `mocida-rs` in **release** (native, no JIT
  shipped).
- **Reactivity reuses mocida `Signal`** — no second reactivity system.
- **Hot-reload engine target is a Cranelift JIT**, introduced behind a stable
  `Evaluator` trait so a tree-walk interpreter can bring the design loop up
  first and be swapped without changing syntax or runtime.

## What's next (M1)

Stand up `mui-syntax` (parser → component AST) and `mui-runtime` (build a tree
via `mocida-rs`) so a `.mui` renders. See the milestones in
[`ARCHITECTURE.md`](ARCHITECTURE.md#milestones).
