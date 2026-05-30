# Bundled fonts (iOS)

iOS apps are sandboxed and cannot read the system font directories
(`/System/Library/Fonts`). On iOS, Mocida loads fonts **only from inside
the app bundle**.

Drop any `.ttf` / `.otf` / `.ttc` files in this directory. The iOS build
(`python build.py --ios`) copies them into the app bundle under `fonts/`,
and `UISearchFonts()` discovers them at launch — so they are available to
`UIGetFont("Family Name")` and as the default text font, exactly like
system fonts on desktop.

This is also where you put your **custom fonts**: anything here ships in
the `.ipa` and is registered automatically. Refer to them by their family
name (the name the font reports internally), e.g.:

```c
UIText_SetFontFamily(t, UIGetFont("Inter"));
```

If this directory has no fonts, the iOS build also copies one system font
from the build host (Arial, else Helvetica) into the bundle as a fallback
so text still renders. On desktop builds this directory is ignored — fonts
are read from the OS font directories.
