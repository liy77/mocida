#!/usr/bin/env python3
"""Generate realistic device frame PNGs for the mui-dev simulator.

The screen area is fully transparent; the body surrounds it. Frame geometry
(bezel sizes, screen offsets) MUST stay in sync with device_profiles.rs.
"""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageChops

# Internal render multiplier — images are rendered at UPSCALE× and downsampled
# with Lanczos for smooth anti-aliased edges.
UPSCALE = 4

OUT_DIR = Path(__file__).parent

# (name, screen_w, screen_h, family, colorway)
DEVICES = [
    ("iphone15",       393, 852, "iphone", "black"),
    ("iphone15pro",    393, 852, "iphone", "black"),
    ("iphone15promax", 430, 932, "iphone", "black"),
    ("iphone16",       393, 852, "iphone", "black"),
    ("galaxy_s24",     360, 780, "galaxy", "black"),
    ("galaxy_s24plus", 384, 832, "galaxy", "silver"),
    ("galaxy_s25",     360, 780, "galaxy", "black"),
    ("galaxy_s25ultra",384, 832, "galaxy", "silver"),
]

# Bezel sizes per family (left, right, top, bottom) in target pixels.
# MUST match DeviceProfile::iphone / DeviceProfile::galaxy in device_profiles.rs.
BEZELS = {
    "iphone": (20, 20, 20, 28),
    "galaxy": (14, 14, 14, 18),
}

# Outer body corner radius (target pixels)
BODY_RADIUS = {"iphone": 48, "galaxy": 32}

# Color themes per (colorway, family)
THEMES = {
    "black_iphone": dict(
        body=(22, 22, 26),
        edge=(42, 42, 50),
        rail=(62, 62, 72),
        outline=(64, 64, 74),
        btn=(54, 54, 64),
        port=(10, 10, 14),
        speaker=(40, 40, 50),
        hl_alpha=26,
        camera=(6, 6, 9),
    ),
    "black_galaxy": dict(
        body=(18, 18, 22),
        edge=(34, 34, 42),
        rail=(52, 52, 62),
        outline=(56, 56, 66),
        btn=(48, 48, 58),
        port=(8, 8, 12),
        speaker=(34, 34, 44),
        hl_alpha=18,
        camera=(5, 5, 8),
    ),
    "silver_galaxy": dict(
        body=(192, 192, 200),
        edge=(220, 220, 228),
        rail=(242, 242, 248),
        outline=(148, 148, 158),
        btn=(208, 208, 216),
        port=(82, 82, 96),
        speaker=(148, 148, 162),
        hl_alpha=68,
        camera=(12, 12, 16),
    ),
}


# ── Helpers ──────────────────────────────────────────────────────────────────

def _rounded_mask(size, radius):
    """Grayscale mask (255 inside, 0 outside) for a rounded rectangle."""
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).rounded_rectangle(
        (0, 0, size[0] - 1, size[1] - 1), radius=radius, fill=255
    )
    return m


def _edge_gradient_overlay(W, H, body_r, edge_col, body_col, body_mask):
    """Semi-transparent horizontal gradient: lighter at left/right edges."""
    ov = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(ov)
    fade_px = W // 5   # gradient extends inward ~20% from each edge
    for x in range(fade_px):
        t = 1.0 - x / fade_px
        t2 = t * t          # quadratic — sharper at the very edge
        a = int(52 * t2)
        col = edge_col
        draw.line([(x, 0), (x, H - 1)], fill=(*col, a))
        draw.line([(W - 1 - x, 0), (W - 1 - x, H - 1)], fill=(*col, a))

    # clip to body shape so corners stay transparent
    ov_a = ov.split()[3]
    ov.putalpha(ImageChops.multiply(ov_a, body_mask))
    return ov


# ── Main frame builder ───────────────────────────────────────────────────────

def make_frame(name, screen_w, screen_h, family, colorway):
    bl, br, bt, bb = BEZELS[family]
    fw = screen_w + bl + br
    fh = screen_h + bt + bb

    s  = UPSCALE
    W, H = fw * s, fh * s

    th = THEMES[f"{colorway}_{family}"]
    r  = BODY_RADIUS[family] * s

    # Screen position in the high-res canvas
    sx, sy = bl * s, bt * s
    sw, sh = screen_w * s, screen_h * s

    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))

    # ── Drop shadow ──────────────────────────────────────────────────────────
    sh_off  = 5 * s
    sh_blur = 9 * s
    shadow_canvas_w = W + 2 * sh_blur + sh_off
    shadow_canvas_h = H + 2 * sh_blur + sh_off
    shadow = Image.new("RGBA", (shadow_canvas_w, shadow_canvas_h), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle(
        (sh_blur, sh_blur + sh_off,
         sh_blur + W - 1, sh_blur + sh_off + H - 1),
        radius=r, fill=(0, 0, 0, 75),
    )
    shadow = shadow.filter(ImageFilter.GaussianBlur(radius=sh_blur // 2))
    img.paste(shadow, (-sh_blur, -sh_blur), shadow)

    draw = ImageDraw.Draw(img)

    # ── Body base ────────────────────────────────────────────────────────────
    draw.rounded_rectangle((0, 0, W - 1, H - 1), radius=r, fill=(*th["body"], 255))

    # ── Metallic edge sheen ──────────────────────────────────────────────────
    body_mask = _rounded_mask((W, H), r)
    ov = _edge_gradient_overlay(W, H, r, th["edge"], th["body"], body_mask)
    img = Image.alpha_composite(img, ov)

    draw = ImageDraw.Draw(img)

    # ── Rail highlights: thin bright strip at left / right body edge ─────────
    rail_px = 3 * s
    for x in range(rail_px):
        t = 1.0 - x / rail_px
        a = int(155 * t)
        draw.line([(x, 0), (x, H - 1)], fill=(*th["rail"], a))
        draw.line([(W - 1 - x, 0), (W - 1 - x, H - 1)], fill=(*th["rail"], a))

    # ── Outer body outline ───────────────────────────────────────────────────
    draw.rounded_rectangle(
        (0, 0, W - 1, H - 1),
        radius=r,
        outline=(*th["outline"], 255),
        width=s,
    )

    # ── Top gloss arc (before cutting screen hole) ───────────────────────────
    draw.arc(
        (2 * s, 2 * s, W - 2 * s, H // 2),
        start=20, end=160,
        fill=(255, 255, 255, th["hl_alpha"]),
        width=s * 2,
    )

    # ── Cut the screen hole ──────────────────────────────────────────────────
    draw.rectangle((sx, sy, sx + sw - 1, sy + sh - 1), fill=(0, 0, 0, 0))

    # ── Thin inner border around the screen (drawn on the body ring) ─────────
    bw = s  # 1 target-pixel border
    draw.rectangle(
        (sx - bw, sy - bw, sx + sw - 1 + bw, sy + sh - 1 + bw),
        outline=(8, 8, 12, 210),
        width=bw,
    )
    # Re-clear screen interior (border stroke may overlap the hole)
    draw.rectangle((sx, sy, sx + sw - 1, sy + sh - 1), fill=(0, 0, 0, 0))

    # ── Camera overlay (opaque, drawn over the transparent screen area) ───────
    if family == "iphone":
        # Dynamic Island: rounded pill, centered near top of screen.
        # Reference: iPhone 15 Dynamic Island ≈ 126 × 37 points.
        di_w = 126 * s
        di_h = 37 * s
        di_x = sx + (sw - di_w) // 2
        di_y = sy + 13 * s      # 13 pt from the top of the screen
        draw.rounded_rectangle(
            (di_x, di_y, di_x + di_w - 1, di_y + di_h - 1),
            radius=di_h // 2,
            fill=(*th["camera"], 255),
        )
    else:
        # Punch-hole front camera, centered near top of screen.
        # Reference: Galaxy S24 hole ≈ 9–10 pt radius.
        hole_r = 9 * s
        hcx = sx + sw // 2
        hcy = sy + 22 * s       # 22 pt from top of screen
        draw.ellipse(
            (hcx - hole_r, hcy - hole_r,
             hcx + hole_r - 1, hcy + hole_r - 1),
            fill=(*th["camera"], 255),
        )

    # ── Side buttons ────────────────────────────────────────────────────────
    btn_fill = (*th["btn"], 255)
    btn_corner = 2 * s

    if family == "iphone":
        bw_btn = 5 * s   # button width (protrudes ~3px at target scale)
        protrude = 3 * s # visible portion outside body
        left_x = -(bw_btn - protrude)  # partially outside left edge

        # Mute / ring switch (upper left)
        ms_h = 28 * s
        ms_y = H // 5
        draw.rounded_rectangle(
            (left_x, ms_y, left_x + bw_btn - 1, ms_y + ms_h - 1),
            radius=btn_corner, fill=btn_fill,
        )

        # Volume Up
        vu_h = 68 * s
        vu_y = ms_y + ms_h + 14 * s
        draw.rounded_rectangle(
            (left_x, vu_y, left_x + bw_btn - 1, vu_y + vu_h - 1),
            radius=btn_corner, fill=btn_fill,
        )

        # Volume Down
        vd_y = vu_y + vu_h + 10 * s
        draw.rounded_rectangle(
            (left_x, vd_y, left_x + bw_btn - 1, vd_y + vu_h - 1),
            radius=btn_corner, fill=btn_fill,
        )

        # Power button (right)
        pwr_h = 90 * s
        pwr_x = W - protrude
        pwr_y = H // 3
        draw.rounded_rectangle(
            (pwr_x, pwr_y, pwr_x + bw_btn - 1, pwr_y + pwr_h - 1),
            radius=btn_corner, fill=btn_fill,
        )

    else:  # Galaxy
        bw_btn = 4 * s
        protrude = 3 * s
        left_x = -(bw_btn - protrude)

        # Volume rocker (left, one long button)
        vol_h = 90 * s
        vol_y = H // 3
        draw.rounded_rectangle(
            (left_x, vol_y, left_x + bw_btn - 1, vol_y + vol_h - 1),
            radius=btn_corner, fill=btn_fill,
        )

        # Power / Bixby button (right)
        pwr_h = 68 * s
        pwr_x = W - protrude
        pwr_y = H // 3
        draw.rounded_rectangle(
            (pwr_x, pwr_y, pwr_x + bw_btn - 1, pwr_y + pwr_h - 1),
            radius=btn_corner, fill=btn_fill,
        )

    # ── Bottom: USB-C port + speaker dots ────────────────────────────────────
    cx   = W // 2
    bot_cy = H - (bb * s) // 2   # vertical center of bottom bezel

    # USB-C slot
    usbc_w = 28 * s
    usbc_h = 8 * s
    ux = cx - usbc_w // 2
    uy = bot_cy - usbc_h // 2
    draw.rounded_rectangle(
        (ux, uy, ux + usbc_w - 1, uy + usbc_h - 1),
        radius=usbc_h // 2,
        fill=(*th["port"], 255),
    )

    # Speaker dots: 3 on each side of the USB-C slot
    spk_col = (*th["speaker"], 255)
    dot_r   = 2 * s
    dot_gap = 7 * s
    gap_from_usbc = 18 * s
    for side in (-1, 1):
        base_x = cx + side * (usbc_w // 2 + gap_from_usbc)
        for i in range(3):
            dx = base_x + side * i * dot_gap
            draw.ellipse(
                (dx - dot_r, bot_cy - dot_r,
                 dx + dot_r - 1, bot_cy + dot_r - 1),
                fill=spk_col,
            )

    # ── Downsample to target resolution ──────────────────────────────────────
    return img.resize((fw, fh), Image.Resampling.LANCZOS)


# ── Entry point ──────────────────────────────────────────────────────────────

def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, screen_w, screen_h, family, colorway in DEVICES:
        img = make_frame(name, screen_w, screen_h, family, colorway)
        out = OUT_DIR / f"{name}.png"
        img.save(out)
        print(f"Wrote {out}  ({img.width}x{img.height})")


if __name__ == "__main__":
    main()
