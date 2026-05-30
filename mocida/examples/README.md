# Mocida examples

Each example is a **standalone app** in its own folder — its own
`CMakeLists.txt`, `main.c` and `app.bundle`. They are NOT built with the
library or the demo; you build one on its own. This keeps each example a
realistic, self-contained app you can run, name, icon and ship.

```
examples/
  hello/      CMakeLists.txt  main.c  app.bundle
  counter/    CMakeLists.txt  main.c  app.bundle
```

| Example   | Shows |
|-----------|-------|
| `hello`   | Minimal app: a label + a button that counts taps. |
| `counter` | State + a width-filling button row. |

Both lay out against the real window size (`UIApp_GetWidth/Height`,
`UIScreen_GetSize`) and inset their content by the device safe area
(`UIScreen_GetSafeArea`) so nothing hides behind a notch / Dynamic Island.

Each `app.bundle` sets the app's display name and a distinct bundle id, so
the examples install side-by-side (no need to uninstall one to try another).

## Build & run

```sh
# Desktop (Windows / Linux / macOS)
python build.py --example hello
./build/<platform>/example-hello-debug/hello

# iOS Simulator (builds + installs + launches)
python build.py --ios --simulator --example hello

# iOS device (unsigned .app/.ipa — re-sign to install)
python build.py --ios --example hello
```

Each example's `CMakeLists.txt` pulls in the mocida library (library target
only) via `add_subdirectory` and builds the app with the same
`mocida_add_executable` helper the library uses, so iOS bundling, the app
icon and frameworks all work. Assets listed in an example's `app.bundle`
are copied into its `.app` and resolved at runtime via `mocida://` URIs.
