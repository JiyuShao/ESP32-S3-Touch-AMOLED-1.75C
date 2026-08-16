#!/usr/bin/env python3
"""Generate LVGL C pixel frames from the Clawd CSS-animated SVGs.

Usage: gen_clawd_frames.py [--browser PATH] [--out DIR] [--preview DIR] [--card]

Clawd animations are CSS @keyframes inside SVG (no spritesheet, no SMIL).
We sample the timeline by appending a negative animation-delay override
(`*{animation-delay:-Tms !important}` = jump every animated element to
time T) and screenshotting with a headless browser on a transparent
background. PIL then crops the sprite, fits it to 96x104, composites it
on the screen bg, and emits pet_frames.h/c in the same format as
gen_frames.py — the device-side pet_render needs no changes.

License: the generated pixel frames are derivative works of Clawd's
AGPL-3.0 SVGs; see tools/pet-assets/clawd/NOTICE.md.
"""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

SCRIPT_DIR = Path(__file__).parent
SVG_DIR = SCRIPT_DIR / "clawd"

TARGET_W, TARGET_H = 270, 180  # ~58% of the 466px screen width, crab aspect 1.5
MAX_FRAMES = 8
VIEWPORT = 500
DEFAULT_CYCLE_S = 12.0

# Frames are plain RGB565 baked onto the app's page background color
# (pet_theme.h Monokai bg #272822) — the display stack is proven with
# RGB565, and a baked bg that matches the page is visually seamless.
SCREEN_BG = (0x27, 0x28, 0x22)

# display state -> (svg file, loop on device, frame count, frame_ms).
# Loop states sample a full cycle evenly and play it back at ~6.7x speed
# (300 ms/frame with 8 frames = 2.4 s loop). One-shots play faster and
# hold the last frame.
STATES = [
    ("idle", "clawd-idle-follow.svg", True, 8, 300),
    ("thinking", "clawd-working-thinking.svg", True, 8, 300),
    ("working", "clawd-working-typing.svg", True, 8, 300),
    ("attention", "clawd-happy.svg", True, 8, 300),
    ("error", "clawd-error.svg", False, 4, 300),  # one-shot: play once, hold last frame
]

# startup animation: wake, played once on first run
INTRO = ("wake", "clawd-wake.svg", False, 6, 250)


def default_browser():
    if sys.platform == "win32":
        for p in (r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
                  r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
                  r"C:\Program Files\Google\Chrome\Application\chrome.exe"):
            if Path(p).exists():
                return p
    elif sys.platform == "darwin":
        for p in ("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
                  "/Applications/Chromium.app/Contents/MacOS/Chromium"):
            if Path(p).exists():
                return p
    else:
        for name in ("google-chrome", "chromium", "chromium-browser"):
            if subprocess.run(["which", name], capture_output=True).returncode == 0:
                return name
    raise SystemExit("no headless browser found; pass one with --browser")


def parse_cycle_s(svg_text):
    """Longest animation-duration in the stylesheet = one cycle."""
    durations = [float(d) for d in re.findall(
        r"animation(?:-[a-z]+)?\s*:[^;]*?\b(\d+(?:\.\d+)?)s\b", svg_text)]
    return max(durations) if durations else DEFAULT_CYCLE_S


def sample_times(cycle_s, count):
    """count samples evenly spread across one cycle."""
    return [cycle_s * i / count for i in range(count)]


HTML_WRAPPER = (
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>"
    "html,body{{margin:0;padding:0;overflow:hidden;background:transparent;}}"
    "svg{{display:block;width:{sv}px !important;height:{sv}px !important;}}"
    "</style></head><body>{svg}</body></html>"
)

# The headless viewport is shorter than --window-size (no chrome, but the
# page box still loses a strip at the bottom). The SVG is shrunk to 400px
# so nothing gets clipped by overflow:hidden (which would amputate the
# crab's legs and paint scrollbars otherwise).
SVG_RENDER_PX = 400


def render_frame(svg_path, t_ms, browser, profile, out_png, workdir):
    """Screenshot the SVG with every animated element jumped to time t_ms.

    The SVG is wrapped in an HTML page with margin:0 + overflow:hidden —
    a bare SVG file gets Chromium's default body margin, overflowing the
    viewport by a few px and painting scrollbars into the screenshot
    (they would end up baked into every frame).
    """
    src = svg_path.read_text(encoding="utf-8")
    if "</svg>" not in src:
        raise SystemExit(f"{svg_path}: no closing </svg> tag")
    injected = src.replace(
        "</svg>", f"<style>*{{animation-delay:-{t_ms}ms !important}}</style></svg>", 1)
    html = HTML_WRAPPER.format(sv=SVG_RENDER_PX, svg=injected)
    tmp_html = workdir / f"seek_{int(t_ms)}ms.html"
    tmp_html.write_text(html, encoding="utf-8")
    subprocess.run(
        [browser, "--headless", "--disable-gpu",
         f"--user-data-dir={profile}",
         f"--window-size={VIEWPORT},{VIEWPORT}",
         "--default-background-color=00000000",
         f"--screenshot={out_png}",
         tmp_html.as_uri()],
        check=True, capture_output=True)


def bake(sprite):
    """Center the sprite on the page-bg canvas (RGB565 source)."""
    canvas = Image.new("RGBA", (TARGET_W, TARGET_H), SCREEN_BG + (255,))
    canvas.alpha_composite(sprite, ((TARGET_W - sprite.width) // 2,
                                    (TARGET_H - sprite.height) // 2))
    return canvas.convert("RGB")


def to_rgb565(pix):
    r, g, b = pix[:3]
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return v & 0xFF, v >> 8  # little-endian


def emit_frames(state, svg_path, count, browser, profile, workdir, preview_dir,
                c_lines):
    """Generate `count` RGB565 frames for one state; returns their symbols."""
    svg_text = svg_path.read_text(encoding="utf-8")
    cycle = parse_cycle_s(svg_text)
    symbols = []
    for i, t in enumerate(sample_times(cycle, count)):
        png = workdir / f"{state}_{i}.png"
        render_frame(svg_path, round(t * 1000), browser, profile, png, workdir)
        im = Image.open(png).convert("RGBA")
        bbox = im.getbbox()
        if bbox is None:
            raise SystemExit(f"{svg_path}: empty frame at t={t}s")
        rgb = bake(im.crop(bbox).resize(
            _fit(im.crop(bbox)), Image.LANCZOS))
        if preview_dir:
            rgb.save(preview_dir / f"frame_{state}_{i}.png")
        sym = f"pet_frame_{state}_{i}"
        data = bytearray()
        for y in range(TARGET_H):
            for x in range(TARGET_W):
                data += bytes(to_rgb565(rgb.getpixel((x, y))))
        c_lines.append(
            f"const uint8_t {sym}_map[] = {{\n    "
            + ", ".join(f"0x{b:02x}" for b in data) + "\n};\n")
        c_lines.append(
            f"const lv_image_dsc_t {sym} = {{\n"
            f"    .header.cf = LV_COLOR_FORMAT_RGB565,\n"
            f"    .header.magic = LV_IMAGE_HEADER_MAGIC,\n"
            f"    .header.w = {TARGET_W},\n"
            f"    .header.h = {TARGET_H},\n"
            f"    .header.stride = {TARGET_W * 2},\n"
            f"    .data_size = {len(data)},\n"
            f"    .data = {sym}_map,\n"
            f"}};\n")
        symbols.append(f"&{sym}")
    return symbols


FIT_SCALE = 0.85  # leave a margin so limbs never touch the frame edges


def _fit(sprite):
    """Thumbnail to 85% of the target box, keeping aspect."""
    sprite = sprite.copy()
    sprite.thumbnail((int(TARGET_W * FIT_SCALE), int(TARGET_H * FIT_SCALE)),
                     Image.LANCZOS)
    return sprite.size


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--browser", default=default_browser())
    ap.add_argument("--out", default=SCRIPT_DIR / ".." / ".." /
                    "examples/esp-idf/99_esp-brookesia/components/pet_render/assets")
    ap.add_argument("--preview", default=None, help="dir for PNG previews")
    args = ap.parse_args()

    browser = Path(args.browser)
    if not browser.exists():
        raise SystemExit(f"browser not found: {browser}")
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    preview_dir = Path(args.preview) if args.preview else None
    if preview_dir:
        preview_dir.mkdir(parents=True, exist_ok=True)

    workdir = Path(tempfile.mkdtemp(prefix="clawd_frames_"))
    profile = workdir / "profile"
    try:
        h_lines = [
            "/* Auto-generated by tools/pet-assets/gen_clawd_frames.py — do not edit. */",
            "#pragma once",
            "",
            '#include "pet_bridge.h"',
            '#include "lvgl.h"',
            "",
            f"#define PET_FRAME_W {TARGET_W}",
            f"#define PET_FRAME_H {TARGET_H}",
            f"#define PET_MAX_FRAMES {MAX_FRAMES}",
            "",
            "typedef struct {",
            "    const lv_image_dsc_t *frames[PET_MAX_FRAMES];",
            "    uint8_t count;",
            "    bool loop;",
            "    uint32_t frame_ms; // 0 = renderer default (150 ms)",
            "} pet_state_anim_t;",
            "",
            "extern const pet_state_anim_t pet_anims[PET_STATE_COUNT];",
            "extern const pet_state_anim_t pet_intro_anim; // wake, one-shot",
            "",
        ]
        c_lines = [
            "/* Auto-generated by tools/pet-assets/gen_clawd_frames.py — do not edit. */",
            '#include "pet_frames.h"',
            "",
        ]
        anims = {}
        for state, svg, loop, count, frame_ms in STATES:
            anims[state] = (emit_frames(
                state, SVG_DIR / svg, count, browser, profile,
                workdir, preview_dir, c_lines), loop, frame_ms)
        intro_frames = emit_frames(
            INTRO[0], SVG_DIR / INTRO[1], INTRO[3], browser, profile,
            workdir, preview_dir, c_lines)

        c_lines.append("const pet_state_anim_t pet_anims[PET_STATE_COUNT] = {")
        for state, _, _, _, _ in STATES:
            frames, loop, frame_ms = anims[state]
            c_lines.append(
                f"    [PET_STATE_{state.upper()}] = {{ .frames = {{{', '.join(frames)}}},"
                f" .count = {len(frames)}, .loop = {'true' if loop else 'false'},"
                f" .frame_ms = {frame_ms} }},"
            )
        c_lines.append("};")
        c_lines.append("")
        c_lines.append(
            "const pet_state_anim_t pet_intro_anim = { .frames = {"
            + ", ".join(intro_frames)
            + "}, .count = " + str(len(intro_frames)) + ", .loop = false,"
            + f" .frame_ms = {INTRO[4]} }};"
        )
        c_lines.append("")

        (out_dir / "pet_frames.h").write_text("\n".join(h_lines))
        (out_dir / "pet_frames.c").write_text("\n".join(c_lines))
        print(f"wrote {out_dir/'pet_frames.h'}, {out_dir/'pet_frames.c'}")
    finally:
        import shutil
        shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    main()
