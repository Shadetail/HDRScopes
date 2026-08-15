# Generates resources/HDRScopes.ico (+ a PNG preview) — a stylized waveform
# scope: dark rounded plate, faint graticule, green waveform envelope and an
# amber reference line with the peak punching above it (the "HDR" story).
#
# Usage: python helper_scripts/make_icon.py
import math
import os

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_ICO = os.path.join(ROOT, "resources", "HDRScopes.ico")
OUT_PNG = os.path.join(ROOT, "resources", "HDRScopes_preview.png")

BG      = (14, 18, 24, 255)
BORDER  = (52, 66, 84, 255)
GRAT    = (44, 56, 70, 255)
AMBER   = (255, 190, 74, 255)
G_DARK  = (16, 116, 62, 255)
G_MID   = (38, 190, 105, 255)
G_EDGE  = (178, 255, 208, 255)


def envelope_top(t: float) -> float:
    """Upper edge of the waveform (unit coords, y down)."""
    y = 0.80
    y -= 0.55 * math.exp(-(((t - 0.54) / 0.15) ** 2))   # main HDR peak
    y -= 0.16 * math.exp(-(((t - 0.22) / 0.10) ** 2))   # secondary bump
    y -= 0.10 * math.exp(-(((t - 0.82) / 0.10) ** 2))   # small right bump
    return y


def envelope_bottom(t: float) -> float:
    return 0.86 - 0.02 * math.exp(-(((t - 0.5) / 0.3) ** 2))


def render(size: int, ss: int = 4) -> Image.Image:
    S = size * ss
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # Plate.
    r = 0.21 * S
    inset = 0.015 * S
    d.rounded_rectangle([inset, inset, S - 1 - inset, S - 1 - inset], radius=r, fill=BG,
                        outline=BORDER, width=max(1, int(0.018 * S)))

    x0, x1 = 0.14 * S, 0.88 * S
    # Faint graticule lines.
    for gy in (0.30, 0.49, 0.68):
        d.line([x0, gy * S, x1, gy * S], fill=GRAT, width=max(1, int(0.012 * S)))

    # Waveform envelope (sampled polygon).
    n = 96
    ts = [i / (n - 1) for i in range(n)]
    xs = [x0 + t * (x1 - x0) for t in ts]
    top = [envelope_top(t) * S for t in ts]
    bot = [envelope_bottom(t) * S for t in ts]

    body = list(zip(xs, top)) + list(zip(reversed(xs), reversed(bot)))
    d.polygon(body, fill=G_DARK)

    # Brighter band hugging the envelope (fakes trace density).
    band = 0.10 * S
    inner = list(zip(xs, top)) + [(x, min(y + band, b)) for x, y, b in
                                  zip(reversed(xs), reversed(top), reversed(bot))]
    d.polygon(inner, fill=G_MID)

    # Bright envelope line.
    d.line(list(zip(xs, top)), fill=G_EDGE, width=max(1, int(0.020 * S)), joint="curve")

    # Amber reference line (over the trace, like a scope overlay).
    ry = 0.30 * S
    d.line([x0, ry, x1, ry], fill=AMBER, width=max(1, int(0.026 * S)))

    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    os.makedirs(os.path.dirname(OUT_ICO), exist_ok=True)
    sizes = [256, 64, 48, 32, 24, 16]
    imgs = {s: render(s) for s in sizes}
    imgs[256].save(OUT_ICO, format="ICO",
                   append_images=[imgs[s] for s in sizes[1:]],
                   sizes=[(s, s) for s in sizes])

    # Preview sheet: each size on a light/dark checker-free strip.
    pad = 8
    w = sum(s + pad for s in sizes) + pad
    h = 256 + 2 * pad
    sheet = Image.new("RGBA", (w, h), (60, 60, 60, 255))
    x = pad
    for s in sizes:
        sheet.alpha_composite(imgs[s], (x, pad + (256 - s) // 2))
        x += s + pad
    sheet.save(OUT_PNG)
    print(f"wrote {OUT_ICO} and {OUT_PNG}")


if __name__ == "__main__":
    main()
