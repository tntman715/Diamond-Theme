"""
수기 분리 도구: 팩 제작자가 버튼 분리(테두리/바탕/아이콘)를 그림 프로그램에서 직접 칠할 수 있게 꺼내고, 칠한 것을 다시 넣는다.

  내보내기: python hand_masks.py export <팩 | 추출 결과 폴더> <작업 폴더> --frames GJ_playBtn_001.png ...
               [--match 정규식] [--profile vanilla|glossy|whole|generic|diamond] [--overrides 보정 파일]
               [--whole-fallback] [--base-plists <GD Resources>]
  가져오기: python hand_masks.py import <작업 폴더> <보정 파일.json> [--all]

작업 폴더에는 버튼마다 폴더가 생긴다:
  <버튼>/original.png   원본 그림 (참고용, 고치지 않는다)
  <버튼>/mask.png       지금 자동 분리 결과. 이걸 고친다
  <버튼>/<버튼>.ora     위 두 장을 레이어로 묶은 파일 (Krita, GIMP에서 바로 열린다. "mask" 레이어를 고친다)
  <버튼>/guide.png      원본 위에 마스크를 겹친 참고 그림
칠하는 색: 빨강 = 테두리, 초록 = 바탕, 파랑 = 아이콘, 검정(또는 지우개) = 원본 유지. 섞인 색은 비율대로 나뉜다.

가져오기는 고친 마스크(.ora 또는 mask.png 중 나중에 저장한 것)를 <보정 파일 이름>-masks/ 폴더에 넣고 보정 파일에
{"frames": {"<버튼>": {"mask": "<경로>"}}}를 적는다. 이후 make_theme_data.py / extract_masks.py를 --overrides로 돌리면
그 버튼은 자동 분리 대신 손으로 칠한 마스크를 쓴다. 버튼마다 <버튼>/check.png로 결과를 확인할 수 있다.
"""
import argparse
import json
import re
import sys
import time
import xml.etree.ElementTree as ET
import zipfile
from io import BytesIO
from pathlib import Path

import numpy as np
from PIL import Image

import extract_masks as em
import inspect_frames as insp

MANIFEST = "hand-masks.json"

README = """# 버튼 분리 작업 폴더 / Button split work folder

## 한국어
버튼마다 폴더가 있습니다. 각 폴더의 `mask.png`(또는 `<버튼>.ora`의 "mask" 레이어)를 고치세요.

| 색 | 뜻 |
|---|---|
| 빨강 (#FF0000) | 테두리 (outline) |
| 초록 (#00FF00) | 바탕 (body) |
| 파랑 (#0000FF) | 아이콘 (icon) |
| 검정 또는 지운 곳 | 원본 그대로 (색을 바꾸지 않음) |

- 바탕 = 버튼의 판(배경), 아이콘 = 판 위에 그린 그림·글자, 테두리 = 가장자리의 링.
- 가장자리를 부드럽게 칠해도 됩니다 (섞인 색은 비율대로 나뉩니다).
- 그림 크기와 방향은 바꾸지 마세요. 버튼 바깥(투명한 곳)은 어떻게 칠해도 상관없습니다.
- `.ora`는 Krita, GIMP에서 원본과 마스크가 레이어로 열립니다. 포토샵은 `original.png` 위에 `mask.png`를 레이어로 올려 쓰고, 고친 뒤 `mask.png`로 저장하세요.
- 다 고친 뒤: `python tools/hand_masks.py import <이 폴더> <보정 파일.json>`

## English
Each button has its own folder. Edit `mask.png` (or the "mask" layer of `<button>.ora`).

| Color | Meaning |
|---|---|
| Red (#FF0000) | outline |
| Green (#00FF00) | body |
| Blue (#0000FF) | icon |
| Black or erased | keep original (not recolored) |

- Body = the button's plate, icon = pictures and text on the plate, outline = the ring along the edge.
- Soft edges are fine (mixed colors are split proportionally).
- Don't change the image size or orientation. What you paint outside the button (transparent area) doesn't matter.
- `.ora` opens in Krita and GIMP with the original and the mask as layers. In Photoshop, put `mask.png` as a layer over `original.png`, edit, and save it back as `mask.png`.
- When done: `python tools/hand_masks.py import <this folder> <overrides.json>`
"""


def upright(img: Image.Image, rotated: bool) -> Image.Image:
    return img.rotate(90, expand=True) if rotated else img


def to_image(arr: np.ndarray) -> Image.Image:
    return Image.fromarray(np.clip(arr * 255 + 0.5, 0, 255).astype(np.uint8))


def write_ora(path: Path, layers: list[tuple[str, Image.Image, float]]) -> None:
    """OpenRaster: layers = [(이름, 그림, 불투명도)], 첫 번째가 맨 위"""
    w, h = layers[0][1].size
    stack = ET.Element("image", version="0.0.5", w=str(w), h=str(h))
    st = ET.SubElement(stack, "stack")
    with zipfile.ZipFile(path, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), "image/openraster", compress_type=zipfile.ZIP_STORED)
        for name, img, opacity in layers:
            buf = BytesIO()
            img.save(buf, "PNG")
            z.writestr(f"data/{name}.png", buf.getvalue(), compress_type=zipfile.ZIP_DEFLATED)
            ET.SubElement(st, "layer", name=name, src=f"data/{name}.png", opacity=f"{opacity:.2f}",
                          visibility="visible", x="0", y="0")
        merged = Image.alpha_composite(layers[-1][1].convert("RGBA"), layers[0][1].convert("RGBA"))
        for name, data in (("mergedimage.png", merged), ("Thumbnails/thumbnail.png", merged.copy())):
            if name.startswith("Thumbnails"):
                data.thumbnail((256, 256))
            buf = BytesIO()
            data.save(buf, "PNG")
            z.writestr(name, buf.getvalue(), compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("stack.xml", ET.tostring(stack, encoding="unicode", xml_declaration=True),
                   compress_type=zipfile.ZIP_DEFLATED)


def read_ora_mask(path: Path) -> Image.Image:
    """.ora 안의 "mask" 레이어 (없으면 맨 위 레이어)"""
    with zipfile.ZipFile(path) as z:
        stack = ET.fromstring(z.read("stack.xml"))
        layers = stack.findall(".//layer")
        if not layers:
            raise ValueError("no layers")
        layer = next((l for l in layers if (l.get("name") or "").strip().lower() == "mask"), layers[0])
        img = Image.open(BytesIO(z.read(layer.get("src")))).convert("RGBA")
        # 레이어 위치가 옮겨졌으면 원래 자리에 놓는다
        w, h = int(stack.get("w")), int(stack.get("h"))
        x, y = int(float(layer.get("x", 0))), int(float(layer.get("y", 0)))
        if (x, y) != (0, 0) or img.size != (w, h):
            canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            canvas.paste(img, (x, y))
            img = canvas
        return img


def safe_stem(name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]", "_", name.removesuffix(".png"))


# ---------------------------------------------------------------- 내보내기

def export(args) -> int:
    overrides = {}
    if args.overrides:
        overrides = json.loads(args.overrides.read_text(encoding="utf-8"))
        for entry in overrides.get("frames", {}).values():
            if entry.get("mask"):
                entry["mask"] = str((args.overrides.parent / entry["mask"]).resolve())
    em.OPTIONS["whole_fallback"] = args.whole_fallback
    get, names = insp.load_source(args.source, args.base_plists)
    profile = args.profile
    if not profile and (args.source / "atlas.json").exists():
        profile = json.loads((args.source / "atlas.json").read_text(encoding="utf-8")).get("profile")
    profile = profile or "vanilla"

    wanted = list(args.frames)
    if args.match:
        wanted += [n for n in names if re.search(args.match, n) and n not in wanted]
    for n in [n for n in wanted if n not in names]:
        print(f"! frame not found: {n}")
    wanted = [n for n in wanted if n in names]
    if not wanted:
        print("no frames (use --frames or --match)")
        return 1

    work = args.work
    work.mkdir(parents=True, exist_ok=True)
    manifest_path = work / MANIFEST
    manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.exists() else {"version": 1, "frames": {}}
    manifest.update(source=str(args.source), profile=profile)
    (work / "README.md").write_text(README, encoding="utf-8")

    for name in wanted:
        rgb, aa, rotated, single = get(name)
        m = np.zeros(rgb.shape, np.uint8)
        over = overrides.get("frames", {}).get(name)
        if over and over.get("mask"):
            over = {**over, "_rotated": rotated}
        force = True if name in overrides.get("themed", []) else False if name in overrides.get("plain", []) else None
        f = em.detect_fill(rgb, aa, min_count=20)
        stats = em.build_frame_mask(rgb, aa, f, m, force, profile, name in overrides.get("icons", []), name, single, over)
        weights = np.where((aa > 0.02)[..., None], m.astype(np.float32) / np.maximum(aa[..., None] * 255, 1), 0)
        weights = np.clip(weights, 0, 1)

        stem = safe_stem(name)
        d = work / stem
        d.mkdir(exist_ok=True)
        original = upright(to_image(np.dstack([rgb, aa])), rotated)
        mask_rgba = np.dstack([weights, (aa > 0.02).astype(np.float32)])
        mask = upright(to_image(mask_rgba), rotated)
        original.save(d / "original.png")
        mask.save(d / "mask.png")
        # 참고 그림: 어두운 바탕 위 원본 + 마스크 색 55%
        bg = Image.new("RGBA", original.size, (40, 40, 52, 255))
        base = Image.alpha_composite(bg, original)
        overlay = mask.copy()
        overlay.putalpha(Image.eval(mask.getchannel("A"), lambda v: v * 55 // 100))
        Image.alpha_composite(base, overlay).save(d / "guide.png")
        write_ora(d / f"{stem}.ora", [("mask", mask, 0.6), ("original", original, 1.0)])
        manifest["frames"][name] = {
            "dir": stem, "size": list(original.size), "rotated": bool(rotated),
            "themed": bool(stats.get("themed")), "exportedAt": time.time(),
        }
        state = "themed" if stats.get("themed") else "not themed (empty mask)"
        print(f"+ {name} -> {d.name}/ ({state})")
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{len(wanted)} buttons exported to {work}. Edit mask.png or the .ora 'mask' layer, then run import.")
    return 0


# ---------------------------------------------------------------- 가져오기

def check_image(original: Image.Image, mask: Image.Image) -> Image.Image:
    """원본 | 칠한 마스크 | 결과 미리보기 (원본을 흐리게 깔고 종류 색을 입힌 것)"""
    o = np.asarray(original.convert("RGBA")).astype(np.float32) / 255
    k = np.asarray(mask.convert("RGBA")).astype(np.float32) / 255
    w = k[..., :3] * k[..., 3:4]
    total = w.sum(-1, keepdims=True)
    w = np.where(total > 1, w / np.maximum(total, 1e-6), w)
    a = o[..., 3:4]
    gray = o[..., :3].mean(-1, keepdims=True)
    colors = np.array([[0.92, 0.25, 0.2], [0.24, 0.78, 0.31], [0.24, 0.43, 1.0]])
    tint = w @ colors
    keep = 1 - w.sum(-1, keepdims=True)
    result = keep * o[..., :3] + (1 - keep) * (0.35 * gray + 0.65 * tint)
    bg = np.array([0.16, 0.16, 0.2])
    panels = [o[..., :3] * a + bg * (1 - a), w * a + bg * (1 - a), result * a + bg * (1 - a)]
    h, wd = o.shape[:2]
    out = Image.new("RGB", (wd * 3 + 20, h), (10, 10, 12))
    for i, p in enumerate(panels):
        out.paste(to_image(p), (i * (wd + 10), 0))
    return out


def import_(args) -> int:
    work = args.work
    manifest = json.loads((work / MANIFEST).read_text(encoding="utf-8"))
    data = json.loads(args.overrides.read_text(encoding="utf-8")) if args.overrides.exists() else {}
    frames = data.setdefault("frames", {})
    mask_dir = args.overrides.parent / f"{args.overrides.stem}-masks"
    mask_dir.mkdir(parents=True, exist_ok=True)
    done = skipped = errors = 0
    for name, info in manifest["frames"].items():
        d = work / info["dir"]
        cands = [p for p in (d / f"{info['dir']}.ora", d / "mask.png") if p.exists()]
        if not cands:
            print(f"! {name}: no mask.png or .ora in {d}")
            errors += 1
            continue
        src = max(cands, key=lambda p: p.stat().st_mtime)
        if not args.all and src.stat().st_mtime <= info["exportedAt"] + 1:
            skipped += 1
            continue
        try:
            mask = read_ora_mask(src) if src.suffix == ".ora" else Image.open(src).convert("RGBA")
        except Exception as e:   # 잘못 저장된 파일
            print(f"! {name}: cannot read {src.name}: {e}")
            errors += 1
            continue
        if list(mask.size) != info["size"]:
            print(f"! {name}: {src.name} is {mask.size[0]}x{mask.size[1]}, expected {info['size'][0]}x{info['size'][1]} "
                  f"(don't resize or rotate the image)")
            errors += 1
            continue
        out = mask_dir / f"{info['dir']}.png"
        mask.save(out)
        entry = frames.setdefault(name, {})
        entry.pop("regions", None)
        entry.pop("whole", None)
        entry["mask"] = out.relative_to(args.overrides.parent).as_posix()
        check_image(Image.open(d / "original.png"), mask).save(d / "check.png")
        print(f"+ {name}: {src.name} -> {entry['mask']} (check: {info['dir']}/check.png)")
        done += 1
    args.overrides.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"{done} masks imported, {skipped} unchanged, {errors} problems. Rebuild the theme data with "
          f"--overrides {args.overrides}")
    return 1 if errors else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    ex = sub.add_parser("export", help="버튼 그림과 지금 분리 결과를 작업 폴더로 꺼낸다")
    ex.add_argument("source", type=Path)
    ex.add_argument("work", type=Path)
    ex.add_argument("--frames", nargs="*", default=[])
    ex.add_argument("--match")
    ex.add_argument("--profile", choices=["vanilla", "glossy", "whole", "generic", "diamond"])
    ex.add_argument("--overrides", type=Path, help="지금 쓰는 보정 파일 (보정이 반영된 상태에서 시작)")
    ex.add_argument("--whole-fallback", action="store_true")
    ex.add_argument("--base-plists", type=Path)
    im = sub.add_parser("import", help="고친 마스크를 보정 파일에 등록한다")
    im.add_argument("work", type=Path)
    im.add_argument("overrides", type=Path)
    im.add_argument("--all", action="store_true", help="고치지 않은 것까지 전부 가져온다")
    args = ap.parse_args()
    return export(args) if args.cmd == "export" else import_(args)


if __name__ == "__main__":
    sys.exit(main())
