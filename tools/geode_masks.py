"""Geode 기본 버튼(원형/탭/에디터 베이스, 메인 메뉴 로고)용 마스크를 만든다.

Geode 스프라이트는 텍스쳐 팩이 다시 그리지 않은 GD 기본 스타일(흰 테두리 + 검은 안쪽 선 + 색 바탕,
금색 로고 + 검은 외곽선)이라 그라데이션 추출 대신 색으로 직접 나눈다:
  베이스: 밝은 테두리 → R(테두리), 색 바탕/검은 안쪽 선 → G(바탕), 반투명 그림자 → 원본 유지
  로고:   금색 → B(글리프), 검은 외곽선/그림자 → 원본 유지 (어두운 바탕 위라 보이지 않는다)
마스크는 원본 시트와 같은 배치이고 값에 원본 알파를 곱해 둔다 (extract_masks.py와 같은 규칙).

출력 (mod/resources):
  geode.json                      시트별 프레임 표 (팩 json과 같은 행 형식 + 마지막 열 dim: 비활성 느낌 베이스)
  geode__<시트>-mask.png          마스크 시트
  --preview 폴더를 주면 Diamond 기본 공식으로 칠해 본 결과 이미지도 저장한다.

사용: python geode_masks.py <geode/resources/geode.loader> <mod/resources> [--preview DIR]
"""
import argparse
import json
import plistlib
import re
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

SHEETS = {
    # 시트 → 대상 프레임 판별
    # 아이콘 선택 베이스는 테두리 없이 바탕색만으로 선택 상태를 보여 주므로 제외
    "BlankSheet-uhd": lambda n: n.startswith("geode.loader/base") and "IconSelect" not in n,
    "LogoSheet-uhd": lambda n: n.endswith("-logo-outline-gold.png"),
}
FILL = 0x11            # Diamond 바탕색 (#111111)
DARK = 0.25            # 이보다 어두우면 검은 선/그림자
SHADOW_ALPHA = 0.6     # 이보다 투명한 어두운 픽셀은 그림자 (GD 그림자는 알파 0.4)
DIM = 0.4              # 선택 안 된 탭/회색 베이스의 테두리 세기
PREVIEW = ("MediumAlt_Green", "MediumAlt_Gray", "Small_Pink", "baseTab", "IconSelect", "Editor_Normal_Green",
           "Account_Normal_Blue", "Category", "logo")


def nums(s):
    return [float(n) for n in re.findall(r"-?\d+\.?\d*", s)]


def load_frames(plist_path):
    pl = plistlib.load(open(plist_path, "rb"))
    out = {}
    for name, f in pl["frames"].items():
        x, y, w, h = map(int, nums(f["textureRect"]))
        ox, oy = nums(f["spriteOffset"])
        sw, sh = map(int, nums(f["spriteSourceSize"]))
        out[name] = dict(x=x, y=y, w=w, h=h, rotated=bool(f["textureRotated"]), offset=(ox, oy), source=(sw, sh))
    return out


def frame_pixels(sheet, f):
    """시트 안 영역 (회전 저장된 프레임은 시트에서 h×w)"""
    w, h = (f["h"], f["w"]) if f["rotated"] else (f["w"], f["h"])
    return sheet[f["y"]:f["y"] + h, f["x"]:f["x"] + w]


def base_mask(px, dim=1.0):
    rgb, a = px[..., :3], px[..., 3]
    straight = np.where(a[..., None] > 0, rgb / np.maximum(a[..., None], 1e-6), 0)
    straight = np.clip(straight, 0, 1)
    mn, mx = straight.min(-1), straight.max(-1)
    opaque = a > 0.004
    dark = mx < DARK
    shadow = dark & (a < SHADOW_ALPHA)

    # 바탕색의 밝기 기준: 가운데 영역의 불투명 픽셀 (회색 베이스도 테두리보다 어둡다)
    h, w = a.shape
    cy, cx = slice(int(h * 0.3), int(h * 0.7)), slice(int(w * 0.3), int(w * 0.7))
    center = (a[cy, cx] > 0.9) & ~dark[cy, cx]
    fmin = float(np.percentile(mn[cy, cx][center], 95)) if center.sum() > 20 else 0.0
    fmin = min(fmin, 0.8)

    # 흰 테두리 ↔ 바탕/검은 선 사이 픽셀은 가장 어두운 채널로 비율을 나눈다
    r = np.clip((mn - fmin) / max(1 - fmin - 0.05, 0.05), 0, 1)
    r = np.where(dark, 0, r)
    # 바탕 안의 하이라이트(왼쪽 위 밝은 부분)는 테두리가 아니다: 검은 안쪽 선에 막혀 바깥과 이어지지 않는
    # 픽셀은 모두 바탕으로 본다 (탭처럼 아래가 열린 모양은 바깥과 이어지므로 밝기 규칙을 그대로 쓴다)
    wall = dark & (a >= SHADOW_ALPHA)
    labels, _ = ndimage.label(~wall)
    edge = np.unique(np.concatenate([labels[0], labels[-1], labels[:, 0], labels[:, -1]]))
    exterior = np.isin(labels, edge[edge > 0])
    r = np.where(exterior, r, 0)
    # 비활성 느낌의 베이스(선택 안 된 탭, 회색)는 테두리를 바탕색 쪽으로 섞어 어둡게 해 상태 차이를 남긴다
    r = r * dim
    g = 1 - r
    keep = shadow | ~opaque
    r = np.where(keep, 0, r)
    g = np.where(keep, 0, g)
    return r, g, np.zeros_like(r)


def logo_mask(px):
    rgb, a = px[..., :3], px[..., 3]
    straight = np.clip(np.where(a[..., None] > 0, rgb / np.maximum(a[..., None], 1e-6), 0), 0, 1)
    mx = straight.max(-1)
    b = np.clip((mx - 0.1) / 0.55, 0, 1)   # 금색(밝음) → 1, 검은 외곽선 → 0, 사이는 비율
    b = np.where(a > 0.004, b, 0)
    return np.zeros_like(b), np.zeros_like(b), b


def render(px, r, g, b):
    """Diamond 기본 공식(HSV 120→360°, 채도 0.4, 바탕 #111111, 글리프 그라데이션)으로 칠해 보기"""
    h, w = px.shape[:2]
    a = px[..., 3]
    straight = np.clip(np.where(a[..., None] > 0, px[..., :3] / np.maximum(a[..., None], 1e-6), 0), 0, 1)
    hue = (120 + 240 * np.linspace(0, 1, w))[None, :].repeat(h, 0) / 360
    import colorsys
    lut = np.array([colorsys.hsv_to_rgb(x, 0.4, 1) for x in np.linspace(0, 1, 361)])
    grad = lut[(hue * 360).astype(int) % 361]
    keep = np.clip(1 - r - g - b, 0, 1)
    col = r[..., None] * grad + g[..., None] * (FILL / 255) + b[..., None] * grad + keep[..., None] * straight
    return np.dstack([col, a])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("geode", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--preview", type=Path)
    args = ap.parse_args()

    data = {"sheets": []}
    for sheet_name, pick in SHEETS.items():
        img = Image.open(args.geode / f"{sheet_name}.png").convert("RGBA")
        frames = load_frames(args.geode / f"{sheet_name}.plist")
        sheet = np.asarray(img, dtype=np.float32) / 255
        # 원본은 직선 알파 PNG → 셰이더/마스크 규칙(곱해진 값)에 맞춰 곱해 둔다
        sheet[..., :3] *= sheet[..., 3:4]
        mask = np.zeros(sheet.shape[:2] + (3,), dtype=np.float32)
        rows, previews = [], []
        for name, f in sorted(frames.items()):
            if not pick(name):
                continue
            px = frame_pixels(sheet, f)
            logo = sheet_name.startswith("Logo")
            dim = not logo and any(k in name for k in ("Unselected", "_Gray"))
            if logo:
                r, g, b = logo_mask(px)
            else:
                r, g, b = base_mask(px, DIM if dim else 1.0)
            a = px[..., 3]
            region = mask[f["y"]:f["y"] + px.shape[0], f["x"]:f["x"] + px.shape[1]]
            region[...] = np.dstack([r * a, g * a, b * a])
            # 아이콘을 얹는 베이스(원형/에디터/계정 등)는 아이콘 버튼으로 표시한다: 아이콘은 위에 따로 그려지는
            # 스프라이트라 베이스 자체에는 글리프 픽셀이 없지만, "아이콘 있는 버튼만 바탕 투명" 판별에 쓴다
            icon_base = any(k in name for k in ("baseCircle", "baseEditor", "baseAccount", "baseCross", "baseLeaderboard"))
            outline, glyph = (0, 1) if logo else (1, 1 if icon_base else 0)
            # 앞의 w, h는 시트 기준 영역 (회전 저장된 프레임은 가로세로가 바뀐다), 뒤의 w, h는 스프라이트 크기
            sheet_h, sheet_w = px.shape[:2]
            rows.append([f["x"], f["y"], sheet_w, sheet_h, int(f["rotated"]), f["w"], f["h"],
                         f["offset"][0], f["offset"][1], f["source"][0], f["source"][1], outline, glyph, FILL,
                         int(dim)])
            if args.preview and any(k in name for k in PREVIEW):
                # 회전 저장된 프레임은 원래 방향으로 돌린 뒤 칠한다 (그라데이션 방향이 스프라이트 기준)
                k = 1 if f["rotated"] else 0
                view = render(*(np.rot90(v, k) for v in (px, r, g, b)))
                previews.append((name, view))
        mask_file = f"geode__{sheet_name}-mask.png"
        Image.fromarray((np.clip(mask, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB").save(args.out / mask_file, optimize=True)
        probe = next(n for n in sorted(frames) if pick(n))
        data["sheets"].append({"name": sheet_name, "size": list(img.size), "mask": mask_file, "probe": probe, "frames": rows})
        print(f"{sheet_name}: {len(rows)} frames → {mask_file}")

        if args.preview:
            args.preview.mkdir(parents=True, exist_ok=True)
            pad = 8
            W = sum(v.shape[1] for _, v in previews[:12]) + pad * 13
            H = max(v.shape[0] for _, v in previews[:12]) + 2 * pad
            canvas = Image.new("RGBA", (W, H), (40, 40, 50, 255))
            x = pad
            for _, v in previews[:12]:
                tile = Image.fromarray((np.clip(v, 0, 1) * 255).astype(np.uint8), "RGBA")
                canvas.alpha_composite(tile, (x, pad))
                x += v.shape[1] + pad
            canvas.save(args.preview / f"{sheet_name}-preview.png")

    (args.out / "geode.json").write_text(json.dumps(data, separators=(",", ":")), encoding="utf-8")


if __name__ == "__main__":
    main()
