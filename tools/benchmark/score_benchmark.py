"""
LLM 답 채점 (truth.json 기준).

답은 보정 파일 형식 그대로 받는다. 세 가지 중 아무거나:
  - JSON 파일: {"frames": {"GJ_x.png": {"regions": {...}}}} 또는 {"GJ_x.png": {...}, ...}
  - 텍스트/Markdown 파일: LLM 답을 그대로 붙여 넣은 것 (안의 ```json 블록을 모두 읽는다)
  - 폴더: 위 파일들이 여러 개 (버튼마다 답을 따로 저장한 경우)

모드:
  fix    보정 과제. 답에 없는 영역은 기준 분할(baseline) 그대로로 본다 → 틀린 것만 고치면 된다
  blind  블라인드 과제. 답에 없는 영역은 "답 없음"(오답)

사용:
  python score_benchmark.py <답 파일 또는 폴더> --mode fix|blind [--truth truth.json] [--details] [--json report.json]
"""
import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
CLASSES = ["outline", "body", "icon", "keep"]
ALIASES = {"rim": "outline", "fill": "body", "glyph": "icon", "original": "keep"}


def load_answers(path: Path):
    """프레임 → 답 항목, 그리고 읽다가 생긴 문제 목록"""
    problems = []
    files = sorted(p for p in path.rglob("*") if p.is_file()) if path.is_dir() else [path]
    merged = {}
    for f in files:
        text = f.read_text(encoding="utf-8", errors="replace")
        blocks = re.findall(r"```(?:json)?\s*(\{.*?\})\s*```", text, re.S) or [text]
        found = False
        for block in blocks:
            try:
                data = json.loads(block)
            except json.JSONDecodeError as e:
                if block is not text or f.suffix == ".json":
                    problems.append(f"{f.name}: invalid JSON ({e.msg} at line {e.lineno})")
                continue
            if not isinstance(data, dict):
                continue
            if not data:
                # "{}" = 고칠 것 없음. 버튼마다 파일을 따로 저장했으면 파일 이름으로 어느 버튼의 답인지 안다
                # (여러 버튼의 답이 모인 요약 파일 안의 {}는 어느 버튼인지 알 수 없으므로 건너뛴다)
                if len(blocks) == 1:
                    merged.setdefault(f.stem.removesuffix(".png") + ".png", {})
                found = True
                continue
            frames = data.get("frames", data)
            for name, entry in frames.items():
                if isinstance(entry, dict):
                    merged.setdefault(name, {}).update(entry)
                    found = True
        if not found and blocks:
            problems.append(f"{f.name}: no answer found")
    return merged, problems


def normalize(value):
    v = str(value).strip().lower()
    v = ALIASES.get(v, v)
    return v if v in CLASSES else None


def accepted(truth_value):
    return set(truth_value) if isinstance(truth_value, list) else {truth_value}


def score(truth, answers, mode):
    report = {"mode": mode, "frames": {}, "problems": []}
    tot = Counter()
    confusion = Counter()
    for name, entry in truth["frames"].items():
        ans = answers.get(name)
        regions_ans = (ans or {}).get("regions") or {}
        f = Counter()
        errors = []
        for key in regions_ans:
            if str(key) not in entry["truth"]:
                report["problems"].append(f"{name}: region {key} is not scored (does not exist or under 40 px)")
        for rid, truth_value in entry["truth"].items():
            ok_set = accepted(truth_value)
            px = entry["px"][rid]
            base = entry["baseline"][rid][0]
            if rid in regions_ans:
                pred = normalize(regions_ans[rid])
                if pred is None:
                    report["problems"].append(f"{name}: region {rid}: unknown class '{regions_ans[rid]}'")
                    pred = "invalid"
            else:
                pred = base if mode == "fix" else "missing"
            right = pred in ok_set
            f["regions"] += 1
            f["right"] += right
            f["px"] += px
            f["right_px"] += px * right
            primary = truth_value[0] if isinstance(truth_value, list) else truth_value
            confusion[(primary if len(ok_set) == 1 else "either", pred)] += 1
            if mode == "fix":
                base_ok = base in ok_set
                f["base_right"] += base_ok
                if not base_ok and right:
                    f["fixed"] += 1
                elif not base_ok:
                    f["missed"] += 1
                elif not right:
                    f["broken"] += 1
            if not right:
                errors.append(f"region {rid}: expected {'/'.join(sorted(ok_set))}, got {pred}")
        report["frames"][name] = {"answered": ans is not None, "right": f["right"], "regions": f["regions"],
                                  "errors": errors}
        tot.update(f)
        tot["frames"] += 1
        tot["perfect"] += f["right"] == f["regions"]
        tot["answered"] += ans is not None
    for name in answers:
        if name not in truth["frames"]:
            report["problems"].append(f"{name}: not in the answer set")
    report["total"] = dict(tot)
    report["confusion"] = {f"{t} -> {p}": n for (t, p), n in sorted(confusion.items())}
    return report


def print_report(report, details):
    t = report["total"]
    mode = report["mode"]
    print(f"mode: {mode} | frames answered: {t['answered']}/{t['frames']}")
    print(f"region accuracy:      {t['right']}/{t['regions']} = {t['right'] / t['regions']:.1%}")
    print(f"pixel-weighted:       {t['right_px'] / t['px']:.1%}")
    print(f"buttons fully right:  {t['perfect']}/{t['frames']} = {t['perfect'] / t['frames']:.1%}")
    if mode == "fix":
        to_fix = t["regions"] - t["base_right"]
        print(f"baseline (no answer): {t['base_right']}/{t['regions']} = {t['base_right'] / t['regions']:.1%}")
        print(f"fixed {t.get('fixed', 0)}/{to_fix} wrong regions, missed {t.get('missed', 0)}, "
              f"broke {t.get('broken', 0)} correct regions")
    print("\nconfusion (truth -> answer):")
    for k, n in report["confusion"].items():
        mark = "" if k.split(" -> ")[0] in (k.split(" -> ")[1], "either") else "  <- wrong"
        if k.startswith("either"):
            mark = ""
        print(f"  {k:24} {n}{mark}")
    print("\nper button:")
    for name, fr in report["frames"].items():
        flag = "OK " if fr["right"] == fr["regions"] else "   "
        print(f"  {flag}{name:34} {fr['right']}/{fr['regions']}" + ("" if fr["answered"] else "  (no answer)"))
        if details:
            for e in fr["errors"]:
                print(f"       {e}")
    if report["problems"]:
        print("\nproblems in the answers:")
        for p in report["problems"]:
            print(f"  ! {p}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("answers", type=Path)
    ap.add_argument("--mode", choices=["fix", "blind"], required=True)
    ap.add_argument("--truth", type=Path, default=HERE / "truth.json")
    ap.add_argument("--details", action="store_true", help="버튼마다 틀린 영역 목록")
    ap.add_argument("--json", type=Path, help="결과를 JSON으로도 저장 (모델끼리 비교할 때)")
    args = ap.parse_args()

    truth = json.loads(args.truth.read_text(encoding="utf-8"))
    answers, problems = load_answers(args.answers)
    report = score(truth, answers, args.mode)
    report["problems"] = problems + report["problems"]
    print_report(report, args.details)
    if args.json:
        args.json.write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
