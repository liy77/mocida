# Mocida — assets

Visuals shipped with the repo plus drop-in slots for media files
used by the test executables. Anything ending in `.svg` or `.png` is
checked in. Audio/video samples are **not** committed (license / size
reasons); use `make-samples.ps1` to synthesize lightweight test
versions.

## What's here

| File                    | Used by                                | Notes |
|-------------------------|----------------------------------------|-------|
| `logo.svg`              | `UIApp_SetWindowIcon` in every test    | The Mocida mark (two overlapping rounded squares). |
| `sdl_logo.png`          | README + `test_image.exe`              | SDL credit logo, also used by the image-tinting demo. |
| `banner.svg`            | Top of the README                      | Animated hero (SMIL — works on GitHub). |
| `feature-quality.svg`   | README                                 | Aliased vs analytic-coverage AA showcase. |
| `feature-widgets.svg`   | README                                 | Animated widget gallery. |
| `feature-debug.svg`     | README                                 | Logger / profiler / overlay / crash handler. |
| `make-samples.ps1`      | dev script                              | Generates `sample.mp4` + `click.wav` via ffmpeg. |
| `sample.mp4` *(generated)* | `test_video.exe`                    | Synthetic 720p test pattern, 8 s, ~200 KB. |
| `sample.mkv/mov/avi` *(optional)* | `test_video.exe` (presets list) | Drop your own; the test cycles through presets. |
| `click.wav` *(generated at runtime)* | `test_sound.exe`            | A 120 ms sine beep — created automatically the first time the test runs (`EnsureBeepFile`). |

## Generating the samples

`test_video.exe` looks for `assets/sample.mp4` (then `.mkv`, `.mov`,
`.avi` in order). The repo doesn't ship one; run the helper:

```pwsh
.\assets\make-samples.ps1
```

The script uses **ffmpeg** if it's on `PATH`. Install it once via
winget:

```pwsh
winget install Gyan.FFmpeg
```

It produces:

- `assets/sample.mp4` — 8 s H.264 + AAC, 1280×720 SMPTE test pattern
  with a sine sweep, ~200 KB
- `assets/click.wav` — 120 ms 440 Hz sine (matches what
  `test_sound.c::EnsureBeepFile` synthesizes; created here so the test
  finds it on first run too)

The script is idempotent: if either file already exists, it's left
alone (pass `-Force` to regenerate).

## Drop your own video

If you'd rather test against real content, drop any file at one of:

```
assets/sample.mp4
assets/sample.mkv
assets/sample.mov
assets/sample.avi
```

`test_video.exe` iterates them in that order until it finds one. The
`UIVideo` widget is backed by **Windows Media Foundation**, so any
codec MF supports works (H.264, HEVC with the Store extension, VP9,
AV1, …). DRM-protected content fails fast with a clear error in the
log.
