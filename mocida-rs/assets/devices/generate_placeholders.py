#!/usr/bin/env python3
"""Generate placeholder device frame PNGs for the mui-dev simulator.

Frame geometry (bezels and screen offsets) must match the logic in
DeviceProfile::framed in mocida-rs/mui-runtime/src/device_profiles.rs.
"""

from pathlib import Path
from PIL import Image, ImageDraw

# Each tuple is (name, frame_w, frame_h, screen_x, screen_y, screen_w, screen_h).
# Notch styling is a generic placeholder shared across all devices.
DEVICES = [
    ("iphone15", 433, 972, 20, 70, 393, 852),
    ("iphone15pro", 433, 972, 20, 70, 393, 852),
    ("iphone15promax", 470, 1052, 20, 70, 430, 932),
    ("iphone16", 433, 972, 20, 70, 393, 852),
    ("galaxy_s24", 400, 900, 20, 70, 360, 780),
    ("galaxy_s24plus", 424, 952, 20, 70, 384, 832),
    ("galaxy_s25", 400, 900, 20, 70, 360, 780),
    ("galaxy_s25ultra", 424, 952, 20, 70, 384, 832),
]

OUT_DIR = Path(__file__).parent


def make_frame(name: str, frame_w: int, frame_h: int, screen_x: int, screen_y: int, screen_w: int, screen_h: int) -> Image.Image:
    img = Image.new("RGBA", (frame_w, frame_h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    # Device body
    body = (0, 0, frame_w - 1, frame_h - 1)
    draw.rounded_rectangle(body, radius=40, fill=(30, 30, 35, 255), outline=(60, 60, 70, 255), width=4)
    # Screen area
    screen = (screen_x, screen_y, screen_x + screen_w - 1, screen_y + screen_h - 1)
    draw.rectangle(screen, fill=(0, 0, 0, 0))
    # Camera / speaker notch bar (generic placeholder, not device-specific)
    notch_w = 120
    notch_h = 28
    notch_x = (frame_w - notch_w) // 2
    notch_y = 28
    draw.rounded_rectangle((notch_x, notch_y, notch_x + notch_w, notch_y + notch_h), radius=14, fill=(10, 10, 10, 255))
    return img


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, frame_w, frame_h, screen_x, screen_y, screen_w, screen_h in DEVICES:
        img = make_frame(name, frame_w, frame_h, screen_x, screen_y, screen_w, screen_h)
        out = OUT_DIR / f"{name}.png"
        img.save(out)
        print(f"Wrote {out}")


if __name__ == "__main__":
    main()
