"""
두 리소스 폴더(예: GD 2.2081 Resources ↔ 2.209 Resources, 또는 팩의 옛 버전 ↔ 새 버전)의 버튼 시트를 비교한다.

  python diff_sheets.py <옛 폴더> <새 폴더> [--data mod/resources/vanilla-gd.json ...] [--json out.json]

시트(GJ_GameSheet03/04, GJ_LaunchSheet)마다:
  - 새로 생긴 / 사라진 프레임
  - 자리가 바뀐 프레임 (그림은 같고 시트 안 위치/회전만 바뀜)
  - 그림이 바뀐 프레임 (크기가 달라졌거나 픽셀이 달라짐)
단독 이미지(GJ_button_*, GJ_square*): 새로 생김 / 사라짐 / 크기·그림이 바뀜.
--data: 테마 데이터(pack.json 형식)가 새 시트와 맞는지 (모드의 배치 검사와 같은 기준) 확인한다.
"""
import argparse
import json
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import extract_masks as em

SHEETS = ["GJ_GameSheet03-uhd", "GJ_GameSheet04-uhd", "GJ_LaunchSheet-uhd"]
SINGLE_RE = re.compile(r"^(GJ_button_\d+|GJ_square\d+)-uhd\.png$")


def crop(img: np.ndarray, fr: dict) -> np.ndarray:
    """시트에서 프레임 그림을 바로 세운 방향으로 잘라 낸다 (회전돼 저장된 프레임도 비교할 수 있게)"""
    x, y, w, h = fr["rect"]
    c = img[y:y + h, x:x + w]
    return np.rot90(c) if fr["rotated"] else c


def same_pixels(a: np.ndarray, b: np.ndarray) -> bool:
    if a.shape != b.shape:
        return False
    # 다시 압축/패킹하면서 생기는 미세한 차이는 무시 (알파가 거의 0인 곳의 색 등)
    diff = np.abs(a.astype(np.int16) - b.astype(np.int16))
    visible = (a[..., 3] > 8) | (b[..., 3] > 8)
    return float(diff[visible].max(initial=0)) <= 6


def diff_sheet(old_dir: Path, new_dir: Path, name: str) -> dict | None:
    op, np_ = old_dir / f"{name}.plist", new_dir / f"{name}.plist"
    if not op.exists() or not np_.exists():
        return None
    of, nf = em.load_frames(op), em.load_frames(np_)
    oi = np.asarray(Image.open(old_dir / f"{name}.png").convert("RGBA"))
    ni = np.asarray(Image.open(new_dir / f"{name}.png").convert("RGBA"))
    added = sorted(set(nf) - set(of))
    removed = sorted(set(of) - set(nf))
    moved, changed = [], []
    for n in sorted(set(of) & set(nf)):
        a, b = of[n], nf[n]
        same_place = a["rect"] == b["rect"] and a["rotated"] == b["rotated"]
        if not same_pixels(crop(oi, a), crop(ni, b)):
            changed.append(n)
        elif not same_place:
            moved.append(n)
    return {"size": [list(oi.shape[1::-1]), list(ni.shape[1::-1])], "frames": [len(of), len(nf)],
            "added": added, "removed": removed, "moved": moved, "changed": changed}


def diff_singles(old_dir: Path, new_dir: Path) -> dict:
    on = {p.name for p in old_dir.glob("*-uhd.png") if SINGLE_RE.match(p.name)}
    nn = {p.name for p in new_dir.glob("*-uhd.png") if SINGLE_RE.match(p.name)}
    changed = []
    for n in sorted(on & nn):
        a = np.asarray(Image.open(old_dir / n).convert("RGBA"))
        b = np.asarray(Image.open(new_dir / n).convert("RGBA"))
        if not same_pixels(a, b):
            changed.append(f"{n} ({a.shape[1]}x{a.shape[0]} -> {b.shape[1]}x{b.shape[0]})")
    return {"added": sorted(nn - on), "removed": sorted(on - nn), "changed": changed}


def check_data(data_path: Path, new_dir: Path) -> list[str]:
    """테마 데이터가 새 시트에서도 맞는지: 모드의 배치 검사(이름별 위치/크기/회전)와 같은 기준"""
    data = json.loads(data_path.read_text(encoding="utf-8"))
    out = []
    for sheet in data["sheets"]:
        plist = new_dir / f"{sheet['name']}.plist"
        if not plist.exists():
            out.append(f"{sheet['name']}: no plist in the new folder")
            continue
        frames = em.load_frames(plist)
        size = Image.open(new_dir / f"{sheet['name']}.png").size
        names = sheet.get("names")
        if list(size) != sheet["size"]:
            out.append(f"{sheet['name']}: sheet size {sheet['size']} -> {list(size)} (data not used: size differs)")
            continue
        if not names:
            out.append(f"{sheet['name']}: data has no frame names (size matches; the mod cannot check the layout)")
            continue
        bad = [n for n, row in zip(names, sheet["frames"])
               if n not in frames or frames[n]["rect"] != [int(v) for v in row[:4]] or int(frames[n]["rotated"]) != int(row[4])]
        state = "OK (layout matches)" if not bad else f"OUT OF DATE: {len(bad)}/{len(names)} buttons differ (e.g. {', '.join(bad[:4])})"
        out.append(f"{sheet['name']}: {state}")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("old", type=Path)
    ap.add_argument("new", type=Path)
    ap.add_argument("--data", type=Path, nargs="*", default=[], help="새 시트와 맞는지 확인할 테마 데이터 (pack.json 형식)")
    ap.add_argument("--json", type=Path, help="결과 전체를 JSON으로")
    ap.add_argument("--buttons", action="store_true", help="목록에 버튼 이름 프레임만 (extract_masks의 버튼 판별 규칙)")
    args = ap.parse_args()

    result = {"sheets": {}, "singles": diff_singles(args.old, args.new), "data": {}}
    keep = (lambda n: em.is_button_name(n, False)) if args.buttons else (lambda n: True)
    for name in SHEETS:
        d = diff_sheet(args.old, args.new, name)
        if d is None:
            continue
        result["sheets"][name] = d
        (os, ns), (of, nf) = d["size"], d["frames"]
        print(f"[{name}] size {os[0]}x{os[1]} -> {ns[0]}x{ns[1]}, frames {of} -> {nf}")
        for key, label in (("added", "new"), ("removed", "removed"), ("changed", "art changed"), ("moved", "moved only")):
            items = [n for n in d[key] if keep(n)]
            print(f"  {label:12} {len(items):4}" + (f"  {', '.join(items[:12])}{' ...' if len(items) > 12 else ''}" if items else ""))
    s = result["singles"]
    print(f"[singles] new {s['added']}, removed {s['removed']}, changed {s['changed']}")
    for p in args.data:
        lines = check_data(p, args.new)
        result["data"][p.name] = lines
        print(f"[data] {p.name}")
        for line in lines:
            print(f"  {line}")
    if args.json:
        args.json.write_text(json.dumps(result, ensure_ascii=False, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
