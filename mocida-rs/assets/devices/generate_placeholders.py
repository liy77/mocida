#!/usr/bin/env python3
"""Generate placeholder device frame PNGs for the mui-dev simulator.

Frame geometry (bezels and screen offsets) must match the logic in
DeviceProfile::framed in mocida-rs/mui-runtime/src/device_profiles.rs.

These are generated placeholders styled after real devices: iPhone frames use a
Dynamic Island pill, Galaxy frames use a centered punch-hole camera, both drawn
inside the thin top bezel so the transparent screen area remains unobstructed.
"""

from pathlib import Path
from PIL import Image, ImageDraw

BEZEL = 14

# Each tuple is (name, screen_w, screen_h, family).
# Screen dimensions must match device_profiles.rs; family chooses styling.
DEVICES = [
    ("iphone15", 393, 852, "iphone"),
    ("iphone15pro", 393, 852, "iphone"),
    ("iphone15promax", 430, 932, "iphone"),
    ("iphone16", 393, 852, "iphone"),
    ("galaxy_s24", 360, 780, "galaxy"),
    ("galaxy_s24plus", 384, 832, "galaxy"),
    ("galaxy_s25", 360, 780, "galaxy"),
    ("galaxy_s25ultra", 384, 832, "galaxy"),
]

BODY_FILL = (28, 28, 30, 255)      # ~#1c1c1e
BODY_OUTLINE = (55, 55, 60, 255)   # subtle edge
INNER_BEZEL = (0, 0, 0, 255)
CAMERA_FILL = (8, 8, 10, 255)
BUTTON_FILL = (50, 50, 55, 255)

OUT_DIR = Path(__file__).parent


def make_frame(name: str, screen_w: int, screen_h: int, family: str) -> Image.Image:
    frame_w = screen_w + 2 * BEZEL
    frame_h = screen_h + 2 * BEZEL
    img = Image.new("RGBA", (frame_w, frame_h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    if family == "iphone":
        corner_radius = 40
        side_button_w = 4
    else:  # galaxy
        corner_radius = 28
        side_button_w = 3

    # Device body.
    body = (0, 0, frame_w - 1, frame_h - 1)
    draw.rounded_rectangle(
        body, radius=corner_radius, fill=BODY_FILL, outline=BODY_OUTLINE, width=2
    )

    # Transparent screen area (the simulator renders the app here).
    screen = (
        BEZEL,
        BEZEL,
        BEZEL + screen_w - 1,
        BEZEL + screen_h - 1,
    )
    draw.rectangle(screen, fill=(0, 0, 0, 0), outline=INNER_BEZEL, width=2)

    # Camera cutout inside the top bezel, never overlapping the screen.
    if family == "iphone":
        # Dynamic Island pill.
        pill_w = 90
        pill_h = 22
        pill_x = (frame_w - pill_w) // 2
        pill_y = (BEZEL - pill_h) // 2
        draw.rounded_rectangle(
            (pill_x, pill_y, pill_x + pill_w - 1, pill_y + pill_h - 1),
            radius=pill_h // 2,
            fill=CAMERA_FILL,
        )
    else:
        # Galaxy punch-hole.
        hole_r = 7
        hole_x = frame_w // 2
        hole_y = BEZEL // 2 + 1
        draw.ellipse(
            (
                hole_x - hole_r,
                hole_y - hole_r,
                hole_x + hole_r - 1,
                hole_y + hole_r - 1,
            ),
            fill=CAMERA_FILL,
        )

    # Subtle side button hint (right edge) for a little realism.
    if family == "iphone":
        draw.rectangle(
            (frame_w - 1, frame_h // 3, frame_w + side_button_w - 2, frame_h // 3 + 50),
            fill=BUTTON_FILL,
        )
    else:
        draw.rectangle(
            (frame_w - 1, frame_h // 3, frame_w + side_button_w - 2, frame_h // 3 + 40),
            fill=BUTTON_FILL,
        )

    return img


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, screen_w, screen_h, family in DEVICES:
        img = make_frame(name, screen_w, screen_h, family)
        out = OUT_DIR / f"{name}.png"
        img.save(out)
        print(f"Wrote {out} ({img.width}x{img.height})")


if __name__ == "__main__":
    main()
