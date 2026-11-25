"""
LLM 답 모으기: 버튼마다 받은 답(JSON 파일이나 답을 그대로 붙여 넣은 텍스트)을 보정 파일 하나로 합친다.

  python collect_answers.py <답 폴더 또는 파일> <보정 파일.json>
      [--check <팩 | 추출 결과 폴더> [--profile vanilla|glossy] [--base-plists <GD Resources>]]

- 보정 파일이 이미 있으면 그 위에 합친다 (같은 프레임의 같은 영역은 새 답으로 바뀐다).
- 답이 {} (고칠 것 없음)이면 아무것도 바꾸지 않는다.
- --check: 팩을 다시 나눠 보고 없는 영역 번호나 잘못된 종류 이름을 알려 준다 (잘못된 항목은 넣지 않는다).
"""
import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "benchmark"))

from score_benchmark import load_answers   # noqa: E402

ALIASES = {"rim": "outline", "fill": "body", "glyph": "icon", "original": "keep"}
CLASSES = {"outline", "body", "icon", "keep"}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("answers", type=Path)
    ap.add_argument("overrides", type=Path)
    ap.add_argument("--check", type=Path, help="팩 또는 추출 결과 폴더: 영역 번호가 실제로 있는지 확인")
    ap.add_argument("--profile", choices=["vanilla", "glossy"])
    ap.add_argument("--base-plists", type=Path)
    args = ap.parse_args()

    answers, problems = load_answers(args.answers)
    for p in problems:
        print(f"! {p}")
    data = json.loads(args.overrides.read_text(encoding="utf-8")) if args.overrides.exists() else {}
    frames = data.setdefault("frames", {})

    region_ids = None
    if args.check:
        import inspect_frames as insp
        get, names = insp.load_source(args.check, args.base_plists)
        profile = args.profile
        if not profile and (args.check / "atlas.json").exists():
            profile = json.loads((args.check / "atlas.json").read_text(encoding="utf-8")).get("profile")
        profile = profile if profile in ("vanilla", "glossy") else "vanilla"
        region_ids = {}
        for name in answers:
            if name in names:
                _, _, _, info, _ = insp.analyze(get, name, {}, profile)
                region_ids[name] = {str(r["id"]) for r in info["regions"]}

    changed = 0
    for name, entry in answers.items():
        if region_ids is not None and name not in region_ids:
            print(f"! {name}: no such frame in the pack, skipped")
            continue
        target = frames.setdefault(name, {})
        if "themed" in entry:
            target["themed"] = bool(entry["themed"])
            changed += 1
        for rid, cls in (entry.get("regions") or {}).items():
            c = ALIASES.get(str(cls).strip().lower(), str(cls).strip().lower())
            if c not in CLASSES:
                print(f"! {name}: region {rid}: unknown class '{cls}', skipped")
                continue
            if region_ids is not None and str(rid) not in region_ids[name]:
                print(f"! {name}: region {rid} does not exist, skipped")
                continue
            target.setdefault("regions", {})[str(rid)] = c
            changed += 1
        if not target:
            del frames[name]
    args.overrides.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"{len(answers)} answers read, {changed} changes merged into {args.overrides} "
          f"({len(frames)} frames with overrides)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
