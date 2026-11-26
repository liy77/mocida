# MUI game / VN HUD (freeLayout)

Public conventions for **game screens** built with Mocidad MUI (visual novels,
overlays, status widgets) — especially under `mikasa run --listen`. Keep this
file **game-agnostic**: patterns only, no titles, brands, or project asset names.

## Engine-first (hard rule)

Build on **Mocidad’s own stack** — do not invent parallel runtimes.

- UI: MUI widgets (`Stack`, `Image`, `Text`, `Button`, `Glass`, `Timer`, …)
  backed by mocida (`UIImage`, signals, `App::on_tick` / `mui_host_frame_poll`).
- Time / motion: `Timer` + signals, or `UIAnim_*` / animated `Image` — not
  browser `setInterval`, DOM overlays, or custom JS clocks beside the canvas.
- Layout / input / audio / 3D: mocida APIs (`freeLayout`, `MouseArea`, `Audio`,
  `Scene3D`, …), not hand-rolled HTML/CSS game shells.
- If a capability is missing, extend **mui-runtime / mocida** so `.mui` stays
  the authoring surface — don’t bypass the engine from the game project.

## Core layout

- Prefer **one root `Stack(freeLayout: true, width: Window.width, height: Window.height)`**.
  Full-bleed scene art + absolute HUD layers. Do **not** treat the playable
  screen like a website page flow.
- **Never make `Glass` the root freeLayout scene.** Glass is for HUD panels only.
  A full-window Glass (vibrancy/tint) washes the scene to black/opaque and breaks
  the game view. Root = `Stack`; docks/dialogue = nested `Glass`.
- Scene backgrounds: `Image(..., fillMode: cover)` edge-to-edge.
- HUD layers (menus, dialogue, status widgets) as later siblings so they paint
  on top. Put persistent status chrome **last** so dialogue cannot cover it.

## Glass / vibrancy

`Glass` is demonstrated in `mui/examples/glass.mui`. Prefer it for frosted HUD
panels **when the target backend composites blur correctly**.

**Web / `mikasa --listen` caveat:** a `Glass` panel (especially `vibrancy-hud`)
can paint an opaque black backdrop over the whole scene. If the playable view
goes black while Text/buttons still show, **fall back immediately** to a muted
`Stack` panel:

```mui
Stack(
  radius: 8,
  background: #121218e8,
  borderColor: #3a3a42,
  borderWidth: 1,
  x: 10,
  y: Window.height - 178,
  width: Window.width - 20,
  height: 168,
  padding: 10,
) {
  // dock content
}
```

On native backends where Glass composites as intended:

```mui
Glass(
  effect: "vibrancy-hud",
  radius: 8,
  tint: #121218,
  tintOpacity: 0.62,
  blur: 22,
  x: 10, y: Window.height - 178,
  width: Window.width - 20, height: 168, padding: 10,
) { /* dock */ }
```

- Mobile / VN docks: bottom strip, muted tint, modest radius (6–8).
- Avoid neon accents and oversized pill clusters unless the product brand
  requires them.
- Buttons inside docks: muted fill, light text, small radius (~4).

## Live time / periodic ticks

Use the built-in non-visual **`Timer`** (mui-runtime). The host must call
`Reactive::poll_timers` each frame (mui-dev does this on web + desktop):

```mui
mut clockSec = signal(28800)   // seconds since midnight (08:00:00)
mut sunX = signal(0)
mut sunY = signal(0)
mut moonX = signal(0)
mut moonY = signal(0)
mut handX = signal(0)
mut handY = signal(0)
Timer(
  interval: 1000,
  onTick: { clockSec += 24 },   // 1 real hour ≈ 1 game day
  orbitSeconds: clockSec,
  orbitSize: 148,
  orbitIcon: 28,
  sunX: sunX, sunY: sunY,
  moonX: moonX, moonY: moonY,
  handX: handX, handY: handY,
)
Text(clockSec, format: "HH:MM:SS", size: 16, color: #f2efe6)
Image("sun.png", x: sunX, y: sunY, width: 28, height: 28)
```

- Prefer an **int** signal + `onTick: { n += … }` + `Text(..., format: "HH:MM:SS")`
  for live clocks (reliable on web rebuild).
- Optional `orbitSeconds:` writes sun/moon/hand pixel positions (midnight at top,
  clockwise) — Majora-style dial overlays on a blank ring face.
- Older web hosts use `r ≈ max(size/2 - icon/2 - 18, size*0.30)` which parks large
  icons in the hub. Compensate with a larger `orbitSize` + negative `orbitX/Y`
  so the computed center matches the visible dial and `r` hits the outer rim
  (see The Last Week `main.mui`). Newer runtimes fix the formula + optional
  `orbitUnit`/`orbitStep` for smooth deci-pixel motion.
- Optional `clock:` string signal + `step:` still work for `HH:MM:SS` strings.
- **Do not overwrite** the clock on scene/location changes; let Timer run.
- **One** digital readout only (center of the dial); do not bake HH:MM:SS into GIFs.
- Web: `mui_host_frame_poll` each frame (`app_web.c`) → `poll_timers`.

Decorative motion can still use an **animated GIF** (`Image(..., animated: true)`),
but **game time that must count** belongs on `Timer` + `Text(clock)`, not GIF
frames alone.

- Critical readouts must stay legible at the on-screen size.
- Variants (day/night) = separate files switched by a signal; keep shared docs
  free of title-specific art names.

### Cache bust (critical on listen)

Listen serves assets with:

```
Cache-Control: public, max-age=31536000, immutable
```

**Same URL = browser keeps the old bytes forever.** When regenerating art,
**change the filename** and restart listen so the new path is in the served
asset set (listen only exposes files referenced by the current `.mui`). Do not
rely on a hard refresh alone under that header.

## Dialogue docks

- Fixed-height bottom panel (`height` explicit). Avoid panels that grow with
  content and overflow status chrome.
- Choices as short muted buttons under the line.
- Keep primary menus at the **bottom** on mobile VN layouts; do not float the
  main action dock at the top unless the design system demands it.

## Title / entry screen

- Brand or product mark first; one short supporting line; one primary CTA.
- Keep the first viewport free of secondary docks, stats strips, and chrome
  clutter.

## Checklist

1. Critical readouts are readable at the displayed size.
2. Regenerated assets use **new filenames** (immutable cache).
3. Docks use `Glass` when blur composites correctly; otherwise muted `Stack`.
4. Persistent HUD is a late freeLayout sibling; dialogue height is capped.
5. Braces balanced; restart `mikasa run --listen=PORT …` after `.mui` edits.
6. Circular / rounded textures: use Mocidad `Image(..., radius:)` (GPU mask —
   not a baked black square behind the art).
