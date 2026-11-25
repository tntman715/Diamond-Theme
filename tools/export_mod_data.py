"""
미리보기용 추출 결과(preview/assets/<팩>/)를 Geode 모드 리소스(mod/resources/)로 내보낸다.

모드는 시트 마스크(PNG)와 테마 대상 프레임 정보만 필요하다. Geode는 리소스 파일을 한 폴더에
평평하게 풀어 놓기 때문에, 팩마다 파일 이름 앞에 팩 id를 붙인다.

  resources/packs.json                     팩 id 목록
  resources/<팩id>.json                    팩 정보 + 시트별 프레임 표
  resources/<팩id>__<시트>-mask.png        시트 / 단독 이미지 마스크

단독 이미지 항목: {"file": "GJ_button_01", "size": [w, h], "mask": ..., "style": [outline, glyph, fill]}

프레임 표 한 줄: [x, y, w, h, rotated, sizeW, sizeH, offX, offY, srcW, srcH, outline, glyph, fill, kind]
"names": 프레임 표와 같은 순서의 프레임 이름. 모드가 실제 시트의 배치와 비교해, 팩이 업데이트되어 배치가
  바뀌었으면 옛 데이터를 쓰지 않는다 (다른 버튼의 마스크가 엉뚱한 버튼에 칠해지지 않게)
  x, y, w, h  시트 안 영역 (픽셀, 회전된 프레임은 시트 기준 가로세로)
  outline / glyph  0 없음, 1 그라데이션, 2 흰색
  fill        원래 바탕색 (회색 0~255, -1 = 단색 아님: 픽셀 원본 색)
  kind        0 일반, 1 리스트 테두리 (GJ_table_*), 2 분할 없음 (그림 전체가 원래 색의 바탕)

사용법: python export_mod_data.py <preview/assets 폴더> <mod/resources 폴더>
"""

import json
import shutil
import sys
from pathlib import Path

STYLE = {"none": 0, "gradient": 1, "white": 2}


def fill_value(fill: str) -> int:
    """원래 바탕색 회색값 0~255. "textured"(바닐라 구조: 바탕이 단색이 아님)는 -1 → 셰이더가 픽셀 원본을 쓴다"""
    return -1 if fill == "textured" else int(fill[1:3], 16)


def kind_of(name: str, frame: dict | None = None) -> int:
    """프레임 종류: 0 일반, 1 리스트 테두리(레벨/리스트 목록을 둘러싼 판, 유리 적용 범위에 쓴다),
    2 분할 없음(whole: 그림 전체가 원래 색의 바탕)"""
    if frame and frame.get("whole"):
        return 2
    return 1 if name.startswith("GJ_table_") else 0


def export(assets: Path, out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    packs = json.loads((assets / "packs.json").read_text(encoding="utf-8"))
    # 팩 데이터만 지운다: 같은 폴더의 Geode 버튼 데이터(geode*, geode_masks.py)와 내장 테마(theme-*.json,
    # my-theme.json)는 다른 도구/손으로 만든 파일이라 남겨야 한다
    old_ids = {e["id"] for e in packs}
    if (out / "packs.json").exists():
        old_ids |= set(json.loads((out / "packs.json").read_text(encoding="utf-8")))
    for pid in old_ids:
        for old in list(out.glob(f"{pid}__*.png")) + [out / f"{pid}.json"]:
            if old.exists():
                old.unlink()

    # "local": 미리보기에서만 쓰는 팩 (다른 사람의 팩 분석 등). 모드 리소스로 내보내지 않는다
    packs = [e for e in packs if not e.get("local")]
    for entry in packs:
        pid = entry["id"]
        data = export_pack(assets / pid, entry, out, f"{pid}__")
        (out / f"{pid}.json").write_text(json.dumps(data, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    (out / "packs.json").write_text(json.dumps([p["id"] for p in packs]), encoding="utf-8")


def export_pack(src: Path, entry: dict, out: Path, prefix: str = "") -> dict:
    """미리보기 팩 폴더(atlas.json + masks/) 하나 → 모드용 팩 데이터. 마스크는 out에 prefix를 붙여 복사한다.
    entry: {"id", "name", "profile", "match"?}. "match": "vanilla" = 게임 기본 리소스에서 온 시트에만 적용"""
    pid = entry["id"]
    atlas = json.loads((src / "atlas.json").read_text(encoding="utf-8"))
    sheets, singles = [], []
    for tex in atlas["textures"]:
        if tex["kind"] == "single":
            # 단독 이미지 (GJ_button_*, GJ_square*, 슬라이더 등): 텍스쳐 전체가 프레임 하나
            f = next(iter(tex["frames"].values()))
            if not f.get("themed") or tex["name"].startswith("BlankSheet"):
                continue
            mask_name = f"{prefix}{Path(tex['mask']).name}"
            shutil.copy2(src / tex["mask"], out / mask_name)
            single = {"file": tex["name"].replace("-uhd", ""), "size": tex["size"], "mask": mask_name,
                      "style": [STYLE[f["outline"]], STYLE[f["glyph"]], fill_value(f.get("fill", tex["fill"]))]}
            if f.get("whole"):
                single["kind"] = 2
            singles.append(single)
            continue
        rows, names, probe = [], [], None
        for name, f in tex["frames"].items():
            if not f.get("themed"):
                continue
            probe = probe or name
            names.append(name)
            fill = fill_value(f.get("fill", tex["fill"]))
            rows.append([*f["rect"], int(f["rotated"]), *f["size"], *f["offset"], *f["sourceSize"],
                         STYLE[f["outline"]], STYLE[f["glyph"]], fill, kind_of(name, f)])
        if not rows:
            continue
        mask_name = f"{prefix}{Path(tex['mask']).name}"
        shutil.copy2(src / tex["mask"], out / mask_name)
        sheets.append({"name": tex["name"], "size": tex["size"], "mask": mask_name,
                       "probe": probe, "frames": rows, "names": names})
        print(f"{pid}: {tex['name']} {len(rows)} frames")
    print(f"{pid}: {len(singles)} single textures ({', '.join(s['file'] for s in singles)})")
    data = {"format": 1, "id": pid, "name": entry["name"], "profile": entry["profile"],
            "sheets": sheets, "singles": singles}
    if entry.get("match"):
        data["match"] = entry["match"]
    return data


if __name__ == "__main__":
    export(Path(sys.argv[1]), Path(sys.argv[2]))
