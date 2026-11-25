"""
LLM 채점용 과제 만들기 (vanilla 프로필 버튼 분할).

truth.json (정답 세트)의 프레임마다 두 가지 과제를 만든다.
정답 값은 종류 이름 하나, 또는 둘 다 맞는 애매한 영역이면 목록 (예: ["body", "icon"]). 40 px 미만 영역은 채점하지 않는다.
  tasks/fix/<프레임>.png, .md     보정 과제: 일부러 약한 기준 분할(baseline)을 보여 주고 틀린 영역만 고치게 한다
                                 (실제 작업과 같은 형식. llm-tuning-instructions.md 그대로 쓴다)
  tasks/blind/<프레임>.png, .md   블라인드 과제: 판정/이유 없이 영역 정보만 주고 모든 영역을 분류하게 한다

baseline(약한 기준 분할): 흰 테두리 = outline, 가장자리에 닿는 가장 큰 영역 하나 = body, 나머지 전부 icon.
지금 추출기보다 훨씬 단순해서 십자 모서리, 잘린 바탕 조각, 톱니 구멍, 튀어나온 그림 등에서 일부러 틀린다.

사용:
  python make_benchmark.py                          truth.json으로 과제 전체를 다시 만든다
  python make_benchmark.py --init GJ_a.png GJ_b.png  지금 추출기 결과(+ 보정 파일)로 정답 초안을 truth.json에 추가
                                                    → 반드시 그림을 보고 직접 확인/수정할 것
  [--source <추출 결과 폴더>] [--overrides <보정 파일>] [--force]
"""
import argparse
import json
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

import inspect_frames as insp   # noqa: E402

DEFAULT_SOURCE = HERE.parent.parent / "preview" / "assets" / "vanilla-gd"
DEFAULT_OVERRIDES = HERE.parent / "overrides" / "vanilla-gd.json"
TRUTH = HERE / "truth.json"
MIN_PX = 40   # 이보다 작은 점(선 사이 잡티)은 정답이 애매해서 채점하지 않는다


def baseline(rows):
    """약한 기준 분할: {id: [class, reason]}"""
    out = {}
    cands = [r for r in rows if r["auto"] != "outline" and r["edgeContact"] >= 0.2]
    main = max(cands, key=lambda r: r["px"])["id"] if cands else None
    for r in rows:
        if r["auto"] == "outline":
            out[str(r["id"])] = ["outline", "baseline: light region touching the outside"]
        elif r["id"] == main:
            out[str(r["id"])] = ["body", "baseline: largest region touching the edge"]
        else:
            out[str(r["id"])] = ["icon", "baseline: everything else is icon"]
    return out


def big_rows(rows):
    return [r for r in rows if r["px"] >= MIN_PX]


def init(names, source, overrides_path, force):
    truth = json.loads(TRUTH.read_text(encoding="utf-8")) if TRUTH.exists() else {
        "schema": 1, "source": "vanilla GD 2.2081 (preview/assets/vanilla-gd)", "frames": {}}
    overrides = json.loads(overrides_path.read_text(encoding="utf-8")) if overrides_path.exists() else {}
    get, all_names = insp.load_source(source, None)
    for name in names:
        if name not in all_names:
            print(f"! frame not found: {name}")
            continue
        if name in truth["frames"] and not force:
            print(f"- {name}: already in truth.json (use --force to replace)")
            continue
        _, _, _, _, rows = insp.analyze(get, name, overrides)
        rows = big_rows(rows)
        truth["frames"][name] = {
            "truth": {str(r["id"]): r["class"] for r in rows},
            "baseline": baseline(rows),
            "px": {str(r["id"]): r["px"] for r in rows},
            "note": "",
        }
        print(f"+ {name}: {len(rows)} regions (draft from the current extractor - review it)")
    TRUTH.write_text(json.dumps(truth, ensure_ascii=False, indent=1), encoding="utf-8")


FIX_HEADER = """# Fix task: {name}

Follow `docs/llm-tuning-instructions.md`. The split below is a deliberately weak baseline; correct every wrong region.
Image: `{image}` (left: original, middle: region numbers, right: current split)

## {name}  (themed)
"""

BLIND_HEADER = """# Blind task: {name}

Follow `docs/llm-tuning-instructions.md`, but there is no current split: **classify every region in the table**
(`outline`, `body`, `icon` or `keep`) and list all of them in `"regions"`.
Image: `{image}` (left: original, middle: region numbers)

## {name}
"""


def generate(source):
    truth = json.loads(TRUTH.read_text(encoding="utf-8"))
    get, all_names = insp.load_source(source, None)
    for mode in ("fix", "blind"):
        d = HERE / "tasks" / mode
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)
    for name, entry in truth["frames"].items():
        if name not in all_names:
            print(f"! {name}: not in {source}")
            continue
        rgb, aa, rotated, info, rows = insp.analyze(get, name, {})
        rows = big_rows(rows)
        ids = {str(r["id"]) for r in rows}
        if ids != set(entry["truth"]):
            print(f"! {name}: region numbers changed since truth.json was made (image or extractor changed) - re-check it")
        stem = name.removesuffix(".png")
        base = entry["baseline"]

        # 보정 과제: 기준 분할을 현재 판정처럼 보여 준다
        for r in info["regions"]:
            b = base.get(str(r["id"]))
            r["class"] = b[0] if b else "keep"
        insp.render(rgb, aa, info, rotated, f"{name}  —  fix task").save(HERE / "tasks" / "fix" / f"{stem}.png")
        lines = [FIX_HEADER.format(name=name, image=f"tasks/fix/{stem}.png"),
                 "| id | class | auto | where | color | px | depth | edge | reason |", "|---|---|---|---|---|---|---|---|---|"]
        for r in rows:
            cls, reason = base[str(r["id"])]
            lines.append(f"| {r['id']} | {cls} | {cls} | {r['where']} | {r['colorName']} {r['color']} | {r['px']} "
                         f"| {r['depth']} | {r['edgeContact']} | {reason} |")
        (HERE / "tasks" / "fix" / f"{stem}.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

        # 블라인드 과제: 판정 없이 영역 정보만
        insp.render(rgb, aa, info, rotated, f"{name}  —  blind task", show_result=False).save(
            HERE / "tasks" / "blind" / f"{stem}.png")
        lines = [BLIND_HEADER.format(name=name, image=f"tasks/blind/{stem}.png"),
                 "| id | where | color | px | depth | edge |", "|---|---|---|---|---|---|"]
        for r in rows:
            lines.append(f"| {r['id']} | {r['where']} | {r['colorName']} {r['color']} | {r['px']} | {r['depth']} "
                         f"| {r['edgeContact']} |")
        (HERE / "tasks" / "blind" / f"{stem}.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    n = len(truth["frames"])
    regions = sum(len(e["truth"]) for e in truth["frames"].values())
    wrong = sum(1 for e in truth["frames"].values() for k, v in e["truth"].items()
                if e["baseline"][k][0] not in (v if isinstance(v, list) else [v]))
    print(f"{n} frames, {regions} regions; baseline gets {regions - wrong}/{regions} right "
          f"({wrong} regions to fix in the fix task)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--init", nargs="+", metavar="FRAME")
    ap.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    ap.add_argument("--overrides", type=Path, default=DEFAULT_OVERRIDES)
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    if args.init:
        init(args.init, args.source, args.overrides, args.force)
    else:
        generate(args.source)
    return 0


if __name__ == "__main__":
    sys.exit(main())
