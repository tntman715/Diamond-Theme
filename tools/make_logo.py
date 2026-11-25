"""모드 아이콘(logo.png)을 다이아몬드 팩 스타일로 그린다.

기울인 큐브: 어두운 바탕(#141414) + 파스텔 그라데이션 테두리(HSV 채도 0.4, 명도 1, 색조 120°→360° 왼쪽→오른쪽),
테두리만 있는 네모 눈과 입, 오른쪽 아래 'GDTP' 글자(배달의민족 주아), 오른쪽 아래 그림자.

    python tools/make_logo.py                      # mod/logo.png (336) + docs/logo-1024.png
    python tools/make_logo.py --font <ttf> --angle -15 --text GDTP
"""
import argparse
import colorsys
import os
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
SS = 4  # 슈퍼샘플링 배율


def find_font():
    names = ["BMJUA_ttf.ttf", "BMJUA.ttf"]
    dirs = [Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft/Windows/Fonts", Path("C:/Windows/Fonts")]
    for d in dirs:
        for n in names:
            if (d / n).exists():
                return d / n
    raise SystemExit("배달의민족 주아 폰트를 찾지 못했습니다. --font로 경로를 주세요.")


def gradient(w, h, sat=0.4, val=1.0, hue0=120.0, hue1=360.0):
    """다이아몬드 팩과 같은 왼쪽→오른쪽 색조 그라데이션 (RGB float 0..1)."""
    row = np.array([colorsys.hsv_to_rgb(((hue0 + (hue1 - hue0) * x / (w - 1)) % 360) / 360, sat, val)
                    for x in range(w)], dtype=np.float32)
    return np.broadcast_to(row[None, :, :], (h, w, 3))


def ring(size, box, width, radius):
    """둥근 네모 테두리 마스크."""
    m = Image.new("L", size, 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle(box, radius=radius, fill=255)
    x0, y0, x1, y1 = box
    inner = (x0 + width, y0 + width, x1 - width, y1 - width)
    d.rounded_rectangle(inner, radius=max(0, radius - width), fill=0)
    return m


def draw_sprite(c, font_path, text):
    """기울이기 전 큐브 한 장 (c x c, RGBA)."""
    size = (c, c)
    body = (0, 0, c - 1, c - 1)
    t = round(c * 0.045)        # 큐브 테두리 두께
    g = round(c * 0.036)        # 눈/입 선 두께
    r = round(c * 0.085)        # 큐브 모서리
    gr = round(c * 0.02)        # 눈/입 모서리

    fill = Image.new("L", size, 0)
    ImageDraw.Draw(fill).rounded_rectangle(body, radius=r, fill=255)

    lines = ring(size, body, t, r)
    def add(m):
        nonlocal lines
        lines = Image.fromarray(np.maximum(np.asarray(lines), np.asarray(m)))
    f = lambda v: round(c * v)
    add(ring(size, (f(0.21), f(0.20), f(0.41), f(0.40)), g, gr))   # 왼쪽 눈
    add(ring(size, (f(0.59), f(0.20), f(0.79), f(0.40)), g, gr))   # 오른쪽 눈
    add(ring(size, (f(0.17), f(0.50), f(0.83), f(0.65)), g, gr))   # 입

    if text:
        txt = Image.new("L", size, 0)
        font = ImageFont.truetype(str(font_path), f(0.17))
        d = ImageDraw.Draw(txt)
        # 주아체는 D-T처럼 간격이 벌어지는 쌍이 있어 글자를 하나씩 잉크 경계로 붙인다
        gap = font.size * 0.07
        boxes = [d.textbbox((0, 0), ch, font=font) for ch in text]
        # T는 가로획이 넓어 잉크 경계로 붙여도 기둥이 멀어 보이므로 조금 더 당긴다
        gaps = [gap - (font.size * 0.07 if "T" in (a, b) else 0) for a, b in zip(text, text[1:])]
        width = sum(b[2] - b[0] for b in boxes) + sum(gaps)
        bottom = max(b[3] for b in boxes)
        x = f(0.86) - width
        y = f(0.885) - bottom
        for i, (ch, b) in enumerate(zip(text, boxes)):
            d.text((x - b[0], y), ch, font=font, fill=255)
            x += b[2] - b[0] + (gaps[i] if i < len(gaps) else 0)
        add(txt)

    grad = gradient(c, c)
    a_fill = np.asarray(fill, dtype=np.float32) / 255
    a_line = np.asarray(lines, dtype=np.float32) / 255
    dark = np.array([0x14, 0x14, 0x14], dtype=np.float32) / 255
    rgb = dark * (1 - a_line[..., None]) + grad * a_line[..., None]
    alpha = np.maximum(a_fill, a_line)
    out = np.dstack([rgb, alpha])
    return Image.fromarray((out * 255 + 0.5).astype(np.uint8), "RGBA")


def compose(n, angle, font_path, text):
    s = n * SS
    sprite = draw_sprite(round(s * 0.66), font_path, text)
    tilted = sprite.rotate(angle, resample=Image.BICUBIC, expand=True)

    canvas = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ox = (s - tilted.width) // 2
    oy = (s - tilted.height) // 2

    # 팩 버튼처럼 오른쪽 아래로 진한 그림자
    shadow_a = tilted.getchannel("A").point(lambda v: v * 0.55)
    shadow = Image.new("RGBA", tilted.size, (0, 0, 0, 255))
    shadow.putalpha(shadow_a)
    shadow = shadow.filter(ImageFilter.GaussianBlur(s * 0.006))
    off = round(s * 0.022)
    canvas.alpha_composite(shadow, (ox - off // 2 + off, oy - off // 2 + off))
    canvas.alpha_composite(tilted, (ox - off // 2, oy - off // 2))

    # 프리멀티플라이 상태로 줄여야 가장자리에 검은 테가 안 생긴다
    return canvas.convert("RGBa").resize((n, n), Image.LANCZOS).convert("RGBA")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--font", type=Path)
    ap.add_argument("--angle", type=float, default=-15.0, help="기울기 (음수 = 시계 방향)")
    ap.add_argument("--text", default="GDTP")
    ap.add_argument("--logo", type=Path, default=ROOT / "mod" / "logo.png")
    ap.add_argument("--big", type=Path, default=ROOT / "docs" / "logo-1024.png")
    a = ap.parse_args()
    font = a.font or find_font()

    compose(336, a.angle, font, a.text).save(a.logo)
    compose(1024, a.angle, font, a.text).save(a.big)
    print("saved", a.logo, a.big)


if __name__ == "__main__":
    main()
