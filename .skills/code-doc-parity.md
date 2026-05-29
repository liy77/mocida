# Golden rule: code must match the docs

**The code must do exactly what the READMEs and docs say — no more, no less.**
A user reads the docs and expects the code to behave that way. Documentation
that promises behavior the code doesn't deliver (or vice-versa) is treated as a
bug.

## What this means in practice

- When you **change behavior**, update the README(s) / header docs / examples
  in the **same change**. When the **docs claim** something, make the code
  actually do it.
- Before writing a command, keybinding, API name, or flag into a README,
  **verify it against the real source** (the `uikit/*.h` headers and the `.c`
  / `.rs` implementation). Don't document aspirational or half-wired features
  as if they work.
- Keep examples runnable: types/functions referenced in README snippets must
  exist and be exported (e.g. re-exported from `mocida-rs/mocida/src/lib.rs`).

## Real example (don't repeat this)

The debug overlay used to be documented as "press F9/F10/F11" and "runs when
`MOCIDA_DEBUG` is set", but:

- it only drew in debug builds (a compile gate), so users pressing the keys saw
  nothing — the docs implied it always worked;
- `F11` is the conventional **fullscreen** key and shouldn't be a binding.

The fix made the docs and code agree: the overlay is now **opt-in at runtime**
(`UIDebugOverlay_SetEnabled(1)`), works in any build, and the hotkeys are
**F9** (bounds) · **F10** (stats HUD) · **F8** (heatmap) · **F12** (toggle all).
Every README, the header comment, the on-screen hint, and the illustration were
updated together. Do the same for any feature you touch.
