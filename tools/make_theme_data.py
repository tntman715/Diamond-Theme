"""텍스쳐 팩 안에 넣을 테마 데이터(theme-data 폴더)를 만든다.

팩 제작자용: 이 폴더를 팩 루트(GJ_GameSheet03-uhd.png 옆)에 두면, Diamond Theme 모드가 그 팩이 켜져 있을 때
자동으로 읽어 테두리/바탕/아이콘 색 바꾸기와 모양 효과를 적용한다.

  theme-data/
    pack.json                     팩 정보 + 시트별 프레임 표 (모드 내장 데이터와 같은 형식, "format": 1)
    <시트>-mask.png               R=테두리, G=바탕, B=아이콘 (원본 알파가 곱해진 값)
    <시트>-sdf.png                엠보싱/유리용 거리장

사용:
  python make_theme_data.py <팩.zip 또는 폴더> <출력 폴더> --profile vanilla|glossy|whole|generic|diamond [--whole-fallback]
      [--id my-pack] [--name "My Pack"] [--overrides overrides.json] [--base-plists <GD Resources>]
  → <출력 폴더>/theme-data/ 가 생긴다. 팩 폴더 자체를 출력 폴더로 주면 바로 팩 안에 만들어진다.

미리보기에서 확인하려면 extract_masks.py --register --local 로 따로 등록한다.
"""
import argparse
import json
import shutil
import sys
import tempfile
from pathlib import Path

import export_mod_data
import extract_masks
import make_sdf

ONLY = r"^(GJ_GameSheet03|GJ_GameSheet04|GJ_LaunchSheet|GJ_button_0\d|GJ_square0\d)-uhd\.png$"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pack", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--profile", choices=["diamond", "generic", "vanilla", "glossy", "whole"], required=True)
    ap.add_argument("--whole-fallback", action="store_true",
                    help="vanilla/glossy에서 나누지 못한 버튼은 분할 없는 효과(whole)라도 받게")
    ap.add_argument("--id")
    ap.add_argument("--name")
    ap.add_argument("--overrides", type=Path)
    ap.add_argument("--base-plists", type=Path)
    args = ap.parse_args()

    pid = args.id or args.pack.stem.lower().replace(" ", "-")
    name = args.name or args.pack.stem
    work = Path(tempfile.mkdtemp(prefix="themedata_"))
    try:
        # 1) 마스크 추출 (미리보기와 같은 결과물을 임시 폴더에)
        argv = [str(args.pack), str(work / pid), "--profile", args.profile, "--name", name, "--only", ONLY]
        if args.overrides:
            argv += ["--overrides", str(args.overrides)]
        if args.base_plists:
            argv += ["--base-plists", str(args.base_plists)]
        if args.whole_fallback:
            argv += ["--whole-fallback"]
        sys.argv = ["extract_masks.py", *argv]
        extract_masks.main()

        # 2) 팩 데이터 + 마스크
        dest = args.out / "theme-data"
        if dest.exists():
            shutil.rmtree(dest)
        dest.mkdir(parents=True)
        data = export_mod_data.export_pack(work / pid, {"id": pid, "name": name, "profile": args.profile}, dest)
        (dest / "pack.json").write_text(json.dumps(data, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")

        # 3) 거리장
        sys.argv = ["make_sdf.py", str(dest)]
        make_sdf.main()
        print(f"theme data -> {dest}")
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
