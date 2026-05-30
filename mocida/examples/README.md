# Mocida examples

Small, self-contained Mocida apps — one `.c` file each, building to one
executable. They run on every platform Mocida supports (Windows, Linux,
macOS, iOS) and show the responsive, safe-area-aware patterns you'd use in
a real app.

| Example       | Shows |
|---------------|-------|
| `hello.c`     | Minimal app: a label + a button that counts taps. |
| `counter.c`   | State + a width-filling two-button row. |

Both lay out against the real window size (`UIApp_GetWidth/Height`,
`UIScreen_GetSize`) and inset their top content by the device safe area
(`UIScreen_GetSafeArea`) so nothing hides behind a notch / Dynamic Island.

## Build & run

Examples build by default with the project (`-DMOCIDA_BUILD_EXAMPLES=OFF`
to skip).

```sh
# Desktop (Windows / Linux / macOS)
python build.py
./build/<platform>/debug/hello        # or counter

# iOS (unsigned .app/.ipa; re-sign to install on a device)
python build.py --ios
# or run on the iOS Simulator:
python build.py --ios --simulator
```

To bundle assets/fonts an example needs on iOS, add an `app.bundle`
manifest at the repo root (see `bundle.h`) — the build copies it and its
assets into the `.app`, and the runtime resolves `mocida://` paths from it.
