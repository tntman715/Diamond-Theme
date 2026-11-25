"""웹 도구(브라우저 안 Pyodide)에서 tools/의 파이썬 코드를 부르는 연결부.

가상 파일 시스템 배치 (worker.js가 파일을 써 넣는다):
  /pack/      팩에서 필요한 시트만 (GJ_GameSheet03-uhd.png, .plist 등, 이름만 평평하게)
  /res/       GD Resources의 plist (팩에 plist가 없을 때만)
  /project/   보정 파일(*.json) + 손으로 칠한 마스크. 웹에서 저장/불러오기 하는 "작업 묶음"
  /hand/      수기 분리 작업 폴더 (꺼내기 결과, 가져오기 입력)
  /out/       만든 테마 데이터
"""
import hashlib
import io
import json
import os
import re
import shutil
import sys
import time
import zipfile
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, "/tools")

import extract_masks as em      # noqa: E402
import hand_masks as hm         # noqa: E402
import inspect_frames as insp   # noqa: E402
import make_theme_data as mtd   # noqa: E402

PACK, RES, PROJECT, HAND, OUT = (Path(p) for p in ("/pack", "/res", "/project", "/hand", "/out"))
NEEDED = re.compile(mtd.ONLY.replace(r"-uhd\.png$", r"-uhd\.(png|plist|fnt)$"))
OVERRIDES_NAME = "overrides.json"


def reset_dir(p: Path) -> None:
    shutil.rmtree(p, ignore_errors=True)
    p.mkdir(parents=True)


def zip_dir(root: Path, prefix: str = "") -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for p in sorted(root.rglob("*")):
            if p.is_file():
                z.write(p, prefix + p.relative_to(root).as_posix())
    return buf.getvalue()


def safe_rel(name: str) -> str | None:
    """zip/폴더 안의 경로를 안전한 상대 경로로 (../ 나 절대 경로는 버린다)"""
    parts = [s for s in name.replace("\\", "/").split("/") if s not in ("", ".")]
    if not parts or ".." in parts:
        return None
    return "/".join(parts)


def as_bytes(data) -> bytes:
    """JS에서 넘어온 Uint8Array(JsProxy) → bytes"""
    return data.to_bytes() if hasattr(data, "to_bytes") else bytes(data)


def unzip_to(data, dest: Path, keep=lambda rel: True) -> list[str]:
    """zip을 dest에 푼다. 파일 시각도 zip에 적힌 대로 맞춘다 (어느 쪽을 나중에 고쳤는지 고르는 데 쓴다)"""
    out = []
    with zipfile.ZipFile(io.BytesIO(as_bytes(data))) as z:
        for info in z.infolist():
            rel = safe_rel(info.filename)
            if info.is_dir() or not rel or not keep(rel):
                continue
            p = dest / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(z.read(info))
            t = time.mktime(info.date_time + (0, 0, -1))
            os.utime(p, (t, t))
            out.append(rel)
    return out


# ---------------------------------------------------------------- 팩

def pick_pack_files(paths: list[str]) -> list[str]:
    """팩 안 파일 경로 목록 → 쓸 파일만 (같은 이름이 여러 폴더에 있으면 가장 얕은 것)"""
    best = {}
    for rel in paths:
        name = rel.rsplit("/", 1)[-1]
        if not NEEDED.search(name):
            continue
        if name not in best or rel.count("/") < best[name].count("/"):
            best[name] = rel
    return sorted(best.values())


def load_pack_zip(data: bytes) -> dict:
    """팩 zip → /pack (필요한 시트만)"""
    reset_dir(PACK)
    with zipfile.ZipFile(io.BytesIO(as_bytes(data))) as z:
        names = [i.filename for i in z.infolist() if not i.is_dir()]
        for rel in pick_pack_files(names):
            (PACK / rel.rsplit("/", 1)[-1]).write_bytes(z.read(rel))
    return pack_info()


def pack_info() -> dict:
    pngs = sorted(p.name for p in PACK.glob("*-uhd.png"))
    sheets = [n for n in pngs if not n.startswith(("GJ_button_", "GJ_square"))]
    missing = [n for n in sheets if not (PACK / n.replace(".png", ".plist")).exists()
               and not (PACK / n.replace(".png", ".fnt")).exists()
               and not (RES / n.replace(".png", ".plist")).exists()]
    return {"files": pngs, "missingPlists": missing}


# ---------------------------------------------------------------- 작업 묶음 (보정 파일 + 칠한 마스크)

def project_json() -> Path:
    PROJECT.mkdir(parents=True, exist_ok=True)
    found = sorted((p for p in PROJECT.glob("*.json") if p.name != hm.MANIFEST), key=lambda p: p.name)
    if found:
        return found[0]
    p = PROJECT / OVERRIDES_NAME
    p.write_text("{}\n", encoding="utf-8")
    return p


def load_project_zip(data: bytes) -> dict:
    """작업 묶음 zip (보정 파일 .json + 마스크 폴더). 보정 파일이 들어 있는 폴더를 묶음의 뿌리로 본다"""
    tmp = Path("/tmp/project_in")
    reset_dir(tmp)
    unzip_to(data, tmp)
    return adopt_project(tmp)


def adopt_project(tmp: Path) -> dict:
    jsons = sorted((p for p in tmp.rglob("*.json") if p.name != hm.MANIFEST), key=lambda p: (len(p.parts), p.name))
    if not jsons:
        raise ValueError("no overrides .json in it")
    root = jsons[0].parent
    reset_dir(PROJECT)
    for p in root.rglob("*"):
        if p.is_file():
            dest = PROJECT / p.relative_to(root)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(p, dest)
    return project_info()


def load_overrides_json(text: str, name: str) -> dict:
    """보정 파일만 따로 (칠한 마스크 없이)"""
    json.loads(text)   # 형식 확인
    reset_dir(PROJECT)
    (PROJECT / (safe_rel(name) or OVERRIDES_NAME).rsplit("/", 1)[-1]).write_text(text, encoding="utf-8")
    return project_info()


def project_info() -> dict:
    p = project_json()
    data = json.loads(p.read_text(encoding="utf-8") or "{}")
    frames = data.get("frames", {})
    masks = [n for n, e in frames.items() if e.get("mask")]
    missing = [n for n in masks if not (p.parent / frames[n]["mask"]).exists()]
    return {"file": p.name, "masks": len(masks), "regions": sum(1 for e in frames.values() if e.get("regions")),
            "whole": sum(1 for e in frames.values() if e.get("whole")),
            "themed": len(data.get("themed", [])), "plain": len(data.get("plain", [])), "missingMasks": missing}


def project_zip() -> bytes:
    project_json()
    return zip_dir(PROJECT)


def clear_project() -> dict:
    reset_dir(PROJECT)
    return project_info()


# ---------------------------------------------------------------- 테마 데이터 만들기

def build(profile: str, whole_fallback: bool, name: str, pid: str) -> dict:
    reset_dir(OUT)
    argv = ["make_theme_data.py", str(PACK), str(OUT), "--profile", profile, "--name", name, "--id", pid,
            "--overrides", str(project_json())]
    if RES.exists() and any(RES.iterdir()):
        argv += ["--base-plists", str(RES)]
    if whole_fallback:
        argv.append("--whole-fallback")
    sys.argv = argv
    mtd.main()
    dest = OUT / "theme-data"
    print("zipping theme-data ...")
    data = json.loads((dest / "pack.json").read_text(encoding="utf-8"))
    return {
        "zip": zip_dir(dest, "theme-data/"),
        "sheets": [{"name": s["name"], "frames": len(s["frames"]), "mask": s["mask"]} for s in data["sheets"]],
        "singles": [s["file"] for s in data["singles"]],
        "masks": {p.name: p.read_bytes() for p in sorted(dest.glob("*-mask.png"))},
    }


# ---------------------------------------------------------------- 수기 분리

def frame_list() -> list[dict]:
    _, names = insp.load_source(PACK, RES if RES.exists() else None)
    singles = {p.name.replace("-uhd", "") for p in PACK.glob("*-uhd.png")}
    return [{"name": n, "button": em.is_button_name(n, n in singles)} for n in names]


WEB_IMPORT_KO = "- 다 고친 뒤: 웹 도구의 \"칠한 마스크 가져오기\"에 이 폴더(또는 zip으로 묶은 것)를 넣으세요."
WEB_IMPORT_EN = "- When done: drop this folder (or a zip of it) into \"Import painted masks\" in the web tool."


def mask_hash(path: Path) -> str:
    img = hm.Image.open(path).convert("RGBA")
    return hashlib.sha1(img.tobytes()).hexdigest()


def export_hand(frames: list[str], profile: str, whole_fallback: bool) -> dict:
    work = HAND / "work"
    reset_dir(work)
    args = SimpleNamespace(source=PACK, work=work, frames=frames, match=None, profile=profile,
                           overrides=project_json(), whole_fallback=whole_fallback,
                           base_plists=RES if RES.exists() else None)
    if hm.export(args) != 0:
        raise ValueError("no buttons exported")
    # 가져올 때 "고친 파일"을 시각이 아니라 내용으로 알아보도록 꺼낸 마스크의 지문을 남긴다
    manifest_path = work / hm.MANIFEST
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["source"] = "web"
    for info in manifest["frames"].values():
        info["maskSha1"] = mask_hash(work / info["dir"] / "mask.png")
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=1), encoding="utf-8")
    readme = (work / "README.md").read_text(encoding="utf-8")
    readme = re.sub(r"^- 다 고친 뒤: .*$", WEB_IMPORT_KO, readme, flags=re.M)
    readme = re.sub(r"^- When done: .*$", WEB_IMPORT_EN, readme, flags=re.M)
    (work / "README.md").write_text(readme, encoding="utf-8")
    return {"zip": zip_dir(work, "hand-work/"), "count": len(manifest["frames"])}


def import_hand_zip(data: bytes) -> dict:
    src = HAND / "in"
    reset_dir(src)
    unzip_to(data, src)
    return import_hand()


def import_hand() -> dict:
    """/hand/in 의 작업 폴더 → 고친 마스크를 작업 묶음에 등록. 고쳤는지는 꺼낼 때의 지문과 비교해서 안다"""
    found = sorted((HAND / "in").rglob(hm.MANIFEST), key=lambda p: len(p.parts))
    if not found:
        raise ValueError(f"{hm.MANIFEST} not found (use the folder or zip made by Export)")
    work = found[0].parent
    manifest = json.loads(found[0].read_text(encoding="utf-8"))
    pj = project_json()
    data = json.loads(pj.read_text(encoding="utf-8") or "{}")
    frames = data.setdefault("frames", {})
    mask_dir = pj.parent / f"{pj.stem}-masks"
    mask_dir.mkdir(parents=True, exist_ok=True)
    done, unchanged, problems, checks = [], [], [], []
    for name, info in manifest["frames"].items():
        d = work / info["dir"]
        if not d.is_dir():   # 고친 버튼 폴더만 골라 넣은 경우
            unchanged.append(name)
            continue
        cands = []
        for p in (d / f"{info['dir']}.ora", d / "mask.png"):
            if not p.exists():
                continue
            try:
                img = hm.read_ora_mask(p) if p.suffix == ".ora" else hm.Image.open(p).convert("RGBA")
            except Exception as e:   # 잘못 저장된 파일
                problems.append(f"{name}: cannot read {p.name} ({e})")
                continue
            edited = (hashlib.sha1(img.tobytes()).hexdigest() != info["maskSha1"]) if "maskSha1" in info \
                else p.stat().st_mtime > info["exportedAt"] + 1
            cands.append((p.stat().st_mtime, p, img, edited))
        edited = [c for c in cands if c[3]]
        if not edited:
            if cands:
                unchanged.append(name)
            elif not any(name in s for s in problems):
                problems.append(f"{name}: no mask.png or .ora")
            continue
        _, p, img, _ = max(edited, key=lambda c: c[0])
        if list(img.size) != info["size"]:
            problems.append(f"{name}: {p.name} is {img.size[0]}x{img.size[1]}, expected "
                            f"{info['size'][0]}x{info['size'][1]} (don't resize or rotate the image)")
            continue
        out = mask_dir / f"{info['dir']}.png"
        img.save(out)
        entry = frames.setdefault(name, {})
        entry.pop("regions", None)
        entry.pop("whole", None)
        entry["mask"] = out.relative_to(pj.parent).as_posix()
        buf = io.BytesIO()
        hm.check_image(hm.Image.open(d / "original.png"), img).save(buf, "PNG")
        checks.append({"name": name, "file": p.name, "png": buf.getvalue()})
        done.append(name)
    pj.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return {"done": done, "unchanged": unchanged, "problems": problems, "checks": checks,
            "project": project_info()}
