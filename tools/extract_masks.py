"""
Diamond Pack 마스크 자동 추출기

텍스쳐팩의 각 픽셀을 아래 4종류로 분해해서 "마스크 시트"를 만든다.
  R = 테두리 비율   (투명 영역에 닿는 선. 파스텔 무지개 또는 흰색)
  G = 채우기 비율   (#141414, #111111 같은 어두운 바탕)
  B = 글리프 비율   (바탕 안의 아이콘/글씨. 흰색 또는 그라데이션)
  남는 비율 (1 - R - G - B) = 원본 색 유지 (테마가 적용되지 않은 바닐라 부분)

테두리/글리프가 원래 무지개였는지 흰색이었는지는 atlas.json의 프레임별
"outline" / "glyph" 값에 들어간다. 무지개 = 채도 40%, 명도 100%, 색상 120°→360°.

마스크는 원본 시트와 같은 크기/레이아웃이라 같은 UV로 샘플링하면 된다.
마스크 PNG에는 알파 채널이 없다 (cocos2d-x가 PNG 로드 시 premultiply 해서 RGB가 깨지기 때문).
대신 R/G/B 값에 원본 알파가 미리 곱해져 있으므로, 셰이더에서 원본 텍스쳐 알파로 나눠서 쓴다.

사용법:
  python extract_masks.py <팩.zip 또는 폴더> <출력 폴더> [--overrides overrides.json]
"""

import argparse
import json
import re
import shutil
import sys
import tempfile
import zipfile
import plistlib
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

GRAD_MIN = 0x99 / 255   # 그라데이션 픽셀의 최소 채널 값 (= 1 - 채도 0.4)
GRAD_TOL = 0.08         # 그라데이션 판정 허용 오차 (테두리 안쪽의 살짝 밝은 줄까지 포함)
WHITE_MIN = 0.90        # 흰 글리프 판정 기준
FILL_TOL = 10 / 255     # 순수 채우기 판정 허용 오차
T_MIN = 0.04            # 이보다 작은 혼합 비율은 채우기로 본다
THEMED_GRAD_RATIO = 0.02
THEMED_FILL_RATIO = 0.25    # 그라데이션 없이 흰 테두리 + 검정 바탕인 버튼도 테마 대상
DIAMOND_MIN_SPAN = 100      # diamond: 바탕 없는 프레임이 무지개로 인정되려면 필요한 색상 변화 폭(도)
REFINE_DIST = 4            # 2차 패스: 확실한 픽셀에서 이 거리(px) 안의 미분류 픽셀만 다시 분해
GENERIC_SAT = 0.2           # generic: 이보다 채도가 높으면 그라데이션 후보
GENERIC_MIN_LIFT = 0.12     # generic: 바탕보다 이만큼은 밝아야 글리프/테두리 후보
GENERIC_FILL_RATIO = 0.2    # generic: 불투명 픽셀 중 바탕 비율이 이 이상이면 테마 대상
EDGE_GAIN = 1.6            # 반투명 가장자리/글로우: 이 배율로 키운 밝기 비율만큼 테두리로 본다 (1/1.6 이상이면 전부)
THEMED_EXPLAINED_RATIO = 0.5


# ---------------------------------------------------------------- 입력

def open_pack(src: Path) -> Path:
    """zip이면 임시 폴더에 풀고, -uhd.png 파일들이 있는 폴더를 돌려준다."""
    if src.is_file() and src.suffix.lower() == ".zip":
        tmp = Path(tempfile.mkdtemp(prefix="gdpack_"))
        with zipfile.ZipFile(src) as z:
            for name in z.namelist():
                target = (tmp / name).resolve()
                if not str(target).startswith(str(tmp.resolve())):
                    raise ValueError(f"zip 안에 잘못된 경로가 있습니다: {name}")
            z.extractall(tmp)
        src = tmp
    # 메인 시트가 있는 폴더가 팩 루트다 (아이콘/추가 폴더에도 -uhd.png가 있어서 첫 파일 기준으로 고르면 엉뚱한 폴더가 된다)
    for marker in ("GJ_GameSheet03-uhd.png", "GJ_GameSheet04-uhd.png", "GJ_LaunchSheet-uhd.png"):
        roots = sorted(src.rglob(marker), key=lambda p: len(p.parts))
        if roots:
            return roots[0].parent
    hits = sorted(src.rglob("*-uhd.png"), key=lambda p: len(p.parts))
    if not hits:
        raise FileNotFoundError(f"{src} 안에서 *-uhd.png 를 찾지 못했습니다")
    return hits[0].parent


def parse_nums(s: str) -> list[float]:
    return [float(v) for v in re.findall(r"-?\d+(?:\.\d+)?", s)]


def load_frames(plist_path: Path) -> dict:
    with open(plist_path, "rb") as f:
        data = plistlib.load(f)
    frames = {}
    for name, fr in data["frames"].items():
        x, y, w, h = parse_nums(fr["textureRect"])
        rotated = bool(fr.get("textureRotated", False))
        # 시트 안에서 실제로 차지하는 영역 (회전된 프레임은 가로세로가 바뀐다)
        sw, sh = (h, w) if rotated else (w, h)
        frames[name] = {
            "rect": [int(x), int(y), int(sw), int(sh)],
            "size": [int(w), int(h)],
            "rotated": rotated,
            "offset": parse_nums(fr.get("spriteOffset", "{0,0}")),
            "sourceSize": [int(v) for v in parse_nums(fr.get("spriteSourceSize", f"{{{w},{h}}}"))],
        }
    return frames


def load_font(fnt_path: Path) -> dict:
    text = fnt_path.read_bytes().decode("cp949", errors="replace")
    font = {"chars": {}, "kernings": []}
    for line in text.splitlines():
        tag, _, rest = line.partition(" ")
        kv = {k: v.strip('"') for k, v in re.findall(r'(\w+)=("[^"]*"|\S+)', rest)}
        if tag == "info":
            font["face"] = kv.get("face", "")
            font["size"] = int(kv.get("size", 0))
        elif tag == "common":
            font["lineHeight"] = int(kv["lineHeight"])
            font["base"] = int(kv["base"])
        elif tag == "page":
            font["page"] = kv["file"]
        elif tag == "char":
            font["chars"][kv["id"]] = [int(kv[k]) for k in
                                       ("x", "y", "width", "height", "xoffset", "yoffset", "xadvance")]
        elif tag == "kerning":
            font["kernings"].append([int(kv["first"]), int(kv["second"]), int(kv["amount"])])
    return font


# ---------------------------------------------------------------- 분류

def detect_fill(rgb: np.ndarray, a: np.ndarray, lo=0.03, min_count=100, default=0x14 / 255) -> float:
    """가장 흔한 어두운 회색을 채우기 색으로 본다."""
    mx, mn = rgb.max(-1), rgb.min(-1)
    cand = (a > 0.98) & (mx - mn <= 2 / 255) & (mx >= lo) & (mx <= 0.15)
    if cand.sum() < min_count:
        return default
    vals = np.round(mx[cand] * 255).astype(int)
    return np.bincount(vals).argmax() / 255


def classify(rgb: np.ndarray, a: np.ndarray, f: float, profile: str = "diamond") -> dict:
    """
    픽셀 = t * X + (1 - t) * fill 로 보고 t와 X를 역산한다.

    diamond: X의 최소 채널이 0.6 근처면 그라데이션(채도 40%, 명도 100% 고정), 1 근처면 흰색이다.
    generic: 그라데이션 공식을 가정하지 않는다. 채도가 있는 밝은 색은 전부 그라데이션 후보, 무채색의 밝은 색은 흰색.
             X와 t는 build_frame_mask에서 가장 가까운 "속" 픽셀 색을 기준으로 다시 잡는다.
    """
    mx = rgb.max(-1)
    t = np.clip((mx - f) / (1 - f), 0, 1)
    X = (rgb - (1 - t)[..., None] * f) / np.maximum(t, 1e-6)[..., None]
    visible = a > 0.02
    near_fill = (np.abs(rgb - f).max(-1) < FILL_TOL) & visible

    if profile == "generic":
        sat = (mx - rgb.min(-1)) / np.maximum(mx, 1e-6)
        strong = visible & ~near_fill & (mx > f + GENERIC_MIN_LIFT)
        grad = strong & (sat > GENERIC_SAT)
        white = strong & (sat <= GENERIC_SAT) & (mx > 0.75)
        return {"t": t, "X": X, "grad": grad, "white": white, "fill": near_fill, "sat": sat}

    xmin = X.min(-1)
    tol = GRAD_TOL + (1.5 / 255) / np.maximum(t, 1e-6)   # t가 작으면 역산 오차가 커진다
    strong = (t >= T_MIN) & visible & ~near_fill
    grad = strong & (np.abs(xmin - GRAD_MIN) < tol)
    white = strong & (xmin > WHITE_MIN)
    return {"t": t, "X": X, "grad": grad, "white": white, "fill": near_fill}


def gradient_span(rgb: np.ndarray, sel: np.ndarray) -> float:
    """
    선택된 픽셀의 색상이 어떤 방향을 따라 한쪽으로 꾸준히 변하는지 확인하고, 그 변화 폭(도)을 돌려준다.
    무지개 그라데이션은 큰 값, 영역마다 색이 다른 바닐라 스프라이트(초록 십자 + 노란 글씨 등)는 0에 가깝다.
    """
    ys, xs = np.nonzero(sel)
    if len(ys) < 200:
        return 0.0
    r, g, b = (rgb[..., i][sel] for i in range(3))
    hue = np.arctan2(np.sqrt(3) * (g - b), 2 * r - g - b)
    best = 0.0
    for th in np.radians(np.arange(0, 180, 15)):
        p = xs * np.cos(th) + ys * np.sin(th)
        edges = np.quantile(p, np.linspace(0, 1, 9))
        idx = np.clip(np.searchsorted(edges, p, side="right") - 1, 0, 7)
        means = np.array([np.arctan2(np.sin(hue[idx == k]).mean(), np.cos(hue[idx == k]).mean())
                          for k in range(8) if (idx == k).any()])
        d = np.angle(np.exp(1j * np.diff(means)))
        if len(d) and (np.all(d >= -0.12) or np.all(d <= 0.12)):
            best = max(best, abs(float(d.sum())))
    return float(np.degrees(best))


def merge_neutral(c: dict, rgb: np.ndarray, aa: np.ndarray, f: float) -> None:
    """generic 프로필: 그라데이션 중간의 거의 무채색 구간(예: 주황→초록 사이)도 같은 덩어리면 그라데이션으로 본다."""
    grad, white, fill = c["grad"], c["white"], c["fill"]
    neutral = (aa > 0.02) & ~fill & (rgb.max(-1) > f + 0.2) & ~grad & ~white
    lab, n = ndimage.label(grad | neutral | white, structure=np.ones((3, 3), bool))
    if not n:
        return
    # 덩어리 안에서 그라데이션이 다수면 무채색/흰색 픽셀(그라데이션 중간색, 아이콘 위 하이라이트 자국)도 그라데이션으로.
    # 흰색이 다수인 덩어리(흰 아이콘)는 그대로 흰색.
    n_grad = ndimage.sum(grad, lab, np.arange(1, n + 1))
    n_white = ndimage.sum(white, lab, np.arange(1, n + 1))
    grad_ids = np.nonzero(n_grad > n_white)[0] + 1
    in_grad = np.isin(lab, grad_ids)
    c["grad"] = grad | ((neutral | white) & in_grad)
    c["white"] = white & ~in_grad


def generic_refit(c: dict, rgb: np.ndarray) -> None:
    """generic 프로필: 가장자리 픽셀의 t와 X를 가장 가까운 '속' 픽셀(주변이 전부 같은 분류인 픽셀) 색 기준으로 다시 잡는다."""
    cat = c["grad"] | c["white"]
    core = ndimage.binary_erosion(cat, structure=np.ones((3, 3), bool))
    if not core.any():
        core = cat
    if not core.any():
        return
    _, (iy, ix) = ndimage.distance_transform_edt(~core, return_indices=True)
    Xr = rgb[iy, ix]
    f = c["f"]
    ref = Xr.max(-1)
    c["X"] = Xr
    c["t"] = np.clip((rgb.max(-1) - f) / np.maximum(ref - f, 1e-3), 0, 1)


# ---------------------------------------------------------------- vanilla 프로필
# 바닐라 GD와 바닐라 구조를 따르는 팩(색만 바꾼 팩 등): 흰 테두리 → 검은 선 → 색 바탕 → (검은 선) → 아이콘.
# 영역들이 검은 선으로 나뉘어 있으므로 색 규칙 대신 "선으로 막힌 영역의 위치 관계"로 나눈다.
#   바깥(투명/그림자)에 닿는 밝은 영역 = 테두리(R), 바깥 껍질의 검은 선에 닿는 색 영역 = 바탕(G),
#   바탕 안에 갇힌 영역 = 아이콘(B). 아이콘 안의 구멍으로 보이는 바탕(톱니 가운데 등)은 색이 비슷하면 바탕.
#   검은 선과 그림자는 원본 유지. 바탕은 두 톤 그라데이션이라 "원래 바탕색"은 픽셀 원본 그대로("textured").
VAN_LINE = 0.22          # 이보다 어두우면 검은 선/그림자
VAN_SHADOW_ALPHA = 0.6   # 어두우면서 이보다 투명하면 그림자
VAN_SURE = 0.30          # 이보다 밝은 픽셀만 영역 판정에 쓴다 (검은 선과 섞인 가장자리는 나중에 비율로)
VAN_LIGHT_MIN = 0.75     # 밝은 영역(흰 테두리/흰 아이콘): 가장 어두운 채널
VAN_LIGHT_SAT = 0.2
VAN_HOLE_DIST = 0.2      # 아이콘 안 구멍의 평균색이 바탕과 이 거리 안이면 바탕
VAN_HOLE_CHROMA = 0.25   # 또는 밝기를 뺀 색 방향(최대 채널로 나눈 색)이 바탕 조각 하나와 이 거리 안이면 바탕
                         # (트로피 손잡이 구멍, 글자 안쪽처럼 그늘져서 어둡게 보이는 바탕)
VAN_REFINE = 3           # 확실한 영역에서 이 거리(px) 안의 섞인 픽셀만 비율로 나눈다
# 버튼으로 볼 프레임 이름 (코인/난이도 얼굴처럼 구조는 비슷하지만 버튼이 아닌 그림은 제외.
# 너무 넓게 빼면 GJ_rateDiffBtn(난이도 평가), GJ_starBtn(별 평가) 같은 진짜 버튼까지 빠진다)
VAN_BUTTON_RE = re.compile(r"(Btn|Button|btn|button|_arrow_|infoIcon|check(On|Off)|GJ_square|square0|longBtn|tab)", re.I)
VAN_EXCLUDE_RE = re.compile(r"(diffIcon_|difficulty_|_face|moon|coin|currencyOrb|orbIcon|diamond|GJ_lock|secretDoor)", re.I)
VAN_BODY_CONTACT = 0.2   # 주 바탕 후보: 영역 둘레 중 바깥 껍질(테두리 선)에 닿는 비율
VAN_EDGE_CONTACT = 0.35  # 주 바탕과 색이 달라도 둘레가 이만큼 바깥 가장자리면 바탕 조각
VAN_SPLIT_DE = 25         # 보정 "splitColor": 영역의 주된 색과 이만큼(Lab ΔE) 넘게 다른 픽셀을 새 영역으로
VAN_SPLIT_CONTACT = 0.05  # 바탕과 색이 같은 영역: 둘레가 이만큼만 바깥 가장자리여도 (글자에 잘린) 바탕 조각


# ---------------------------------------------------------------- glossy 프로필 (색/밝기 경계로 나누기)
# 광택/렌더링 스타일 팩(프루티거 에어로 등)은 검은 선이 없어서 vanilla 프로필로는 버튼이 한 덩어리가 된다.
# 대신 색이나 밝기가 갑자기 바뀌는 곳을 경계로 영역을 나눈다. 부드러운 그라데이션과 광택은 한 영역에 남고,
# 비슷한 색의 이웃 영역은 합친다. 나눈 뒤의 판정(테두리/바탕/아이콘, 구멍 등)은 vanilla와 같은 규칙을 쓴다.
GLOSSY_EDGE = 14.0       # 4픽셀 떨어진 두 점의 색 차이(Lab, 살짝 흐리게 한 뒤)가 이보다 크면 경계
GLOSSY_SPAN = 2          # 차이를 재는 거리의 절반 (픽셀)
GLOSSY_ALPHA_WEIGHT = 60.0   # 불투명도 차이 1.0을 Lab 색 차이 몇으로 볼지
GLOSSY_MERGE = 14.0      # 맞닿은 두 영역의 평균색 차이(Lab)가 이보다 작으면 한 영역으로 합친다
GLOSSY_STRONG_EDGE = 28.0   # 두 영역 사이 경계의 색 차이(중간값)가 이 이상이면 평균색이 비슷해도 합치지 않는다
GLOSSY_MIN_PX = 0.0005   # 이보다 작은 조각(불투명 픽셀 대비)은 따로 두지 않고 이웃 영역에 붙인다
GLOSSY_ALPHA = 0.2       # 이보다 불투명하면 버튼의 일부 (반투명 유리판 포함)
GLOSSY_LIGHT_MIN = 0.55   # 테두리로 볼 밝은 영역 기준 (가장 어두운 채널)
GLOSSY_LIGHT_SAT = 0.25


def rgb_to_lab(rgb: np.ndarray) -> np.ndarray:
    c = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
    xyz = c @ np.array([[0.4124, 0.2126, 0.0193], [0.3576, 0.7152, 0.1192], [0.1805, 0.0722, 0.9505]])
    xyz /= np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def color_regions(rgb: np.ndarray, aa: np.ndarray):
    """색/밝기 경계로 불투명 부분을 영역으로 나눈다 → (labels, n). 번호는 위→아래, 왼쪽→오른쪽으로 처음 나오는 순서"""
    opaque = aa > GLOSSY_ALPHA
    if aa.size == 0 or not opaque.any():   # 빈 프레임
        return np.zeros(aa.shape, np.int32), 0
    n_opaque = max(int(opaque.sum()), 1)
    # 특징: Lab 색 + 불투명도 (반투명 유리판과 불투명한 아이콘 사이도 경계다)
    lab = rgb_to_lab(np.clip(rgb, 0, 1))
    feat = np.concatenate([lab, aa[..., None] * GLOSSY_ALPHA_WEIGHT], -1)
    feat = np.stack([ndimage.gaussian_filter(feat[..., k], 1.0) for k in range(feat.shape[-1])], -1)
    # 4픽셀 떨어진 두 점의 차이: 광택 팩의 경계는 몇 픽셀에 걸쳐 부드럽게 바뀌어서 바로 옆 픽셀끼리는 차이가 작다
    r = GLOSSY_SPAN
    pad = np.pad(feat, ((r, r), (r, r), (0, 0)), mode="edge")
    dx = np.linalg.norm(pad[r:-r, 2 * r:] - pad[r:-r, :-2 * r], axis=-1)
    dy = np.linalg.norm(pad[2 * r:, r:-r] - pad[:-2 * r, r:-r], axis=-1)
    grad = np.maximum(dx, dy)
    seeds = opaque & (grad <= GLOSSY_EDGE)
    labels, n = ndimage.label(seeds)   # 4방향 연결: 대각선 틈으로 새지 않게
    if n == 0:
        return np.zeros(aa.shape, np.int32), 0
    sizes = ndimage.sum(np.ones(aa.shape), labels, np.arange(1, n + 1))
    keep = np.zeros(n + 1, bool)
    keep[1:] = sizes >= max(30, GLOSSY_MIN_PX * n_opaque)
    labels = np.where(keep[labels], labels, 0)
    if not keep.any():
        return np.zeros(aa.shape, np.int32), 0
    # 경계/작은 조각 픽셀은 가장 가까운 영역에
    _, (iy, ix) = ndimage.distance_transform_edt(labels == 0, return_indices=True)
    labels = np.where(opaque, labels[iy, ix], 0)

    # 비슷한 색의 이웃 영역 합치기 (작은 차이부터, 합친 뒤 평균을 다시 계산).
    # 평균색이 비슷해도 둘 사이 경계가 뚜렷하면 합치지 않는다: 구슬 위쪽 광택과 그 아래 은색 트로피는 평균색이
    # 비슷하지만 트로피 윤곽선이 또렷하다 (합치면 트로피 모양이 사라진다)
    while True:
        ids = np.unique(labels[labels > 0])
        if len(ids) <= 1:
            break
        mean = {i: lab[labels == i].mean(0) for i in ids}
        edge_vals = {}
        for a, b, ga, gb in ((labels[:, 1:], labels[:, :-1], grad[:, 1:], grad[:, :-1]),
                             (labels[1:, :], labels[:-1, :], grad[1:, :], grad[:-1, :])):
            m = (a != b) & (a > 0) & (b > 0)
            g = np.maximum(ga[m], gb[m])
            for u, v, gv in zip(a[m], b[m], g):
                edge_vals.setdefault((min(u, v), max(u, v)), []).append(gv)
        weak = [uv for uv, vals in edge_vals.items() if np.median(vals) < GLOSSY_STRONG_EDGE]
        best = min(weak, key=lambda uv: np.linalg.norm(mean[uv[0]] - mean[uv[1]]), default=None)
        if best is None or np.linalg.norm(mean[best[0]] - mean[best[1]]) >= GLOSSY_MERGE:
            break
        labels[labels == best[1]] = best[0]

    # 번호를 처음 나오는 순서로 다시 매긴다 (같은 그림이면 항상 같은 번호)
    flat = labels.ravel()
    ids, first = np.unique(flat, return_index=True)
    order = [i for _, i in sorted(zip(first, ids)) if i > 0]
    remap = np.zeros(int(flat.max()) + 1, np.int32)
    for k, i in enumerate(order, 1):
        remap[i] = k
    return remap[labels], len(order)


# ---------------------------------------------------------------- 손으로 그린 마스크 (수기 분리)
# 팩 제작자가 그림 프로그램에서 칠한 마스크: 빨강 = 테두리, 초록 = 바탕, 파랑 = 아이콘, 검정/지운 곳 = 원본 유지.
# 섞인 색(부드러운 가장자리)은 비율대로 나눈다. 버튼을 바로 세운 방향으로 그린다 (시트에서 돌아가 있는 프레임은 되돌린다).
# 보정 파일: {"frames": {"X.png": {"mask": "masks/X.png"}}} (경로는 보정 파일 기준). tools/hand_masks.py가 만든다.
def build_painted_mask(rgb: np.ndarray, aa: np.ndarray, m: np.ndarray, path: str, rotated: bool) -> dict:
    stats = {"themed": False, "gradRatio": 0.0, "fillRatio": 0.0, "explained": 0.0, "outline": "none", "glyph": "none"}
    img = Image.open(path).convert("RGBA")
    if rotated:   # 그릴 때는 바로 세운 방향 (분석 도구와 같은 rotate(90)) → 시트 방향으로 되돌린다
        img = img.rotate(-90, expand=True)
    paint = np.asarray(img).astype(np.float32) / 255
    if paint.shape[:2] != aa.shape:
        stats["paintError"] = f"painted mask {path} is {img.size[0]}x{img.size[1]}, frame is {aa.shape[1]}x{aa.shape[0]}"
        return stats
    w = paint[..., :3] * paint[..., 3:4]
    total = w.sum(-1, keepdims=True)
    w = np.where(total > 1, w / np.maximum(total, 1e-6), w)
    r, g, b = w[..., 0], w[..., 1], w[..., 2]
    vis = aa > 0.02
    if not (r + g + b)[vis].any():
        return stats   # 전부 원본 유지 = 테마 안 함
    light = rgb.min(-1) >= VAN_LIGHT_MIN
    icon_w = float((b * vis).sum())
    stats.update(themed=True, painted=True, fill="textured",
                 fillRatio=round(float((g * (aa > 0.5)).sum()) / max(int((aa > 0.5).sum()), 1), 3),
                 explained=1.0,
                 outline="white" if float((r * vis).sum()) > 0 else "none",
                 glyph=("white" if float((b * light * vis).sum()) >= 0.5 * icon_w else "gradient") if icon_w > 0 else "none",
                 coverage=round(float(((r + g + b) * vis).sum()) / max(float(vis.sum()), 1), 3))
    m[..., 0] = np.round(r * aa * 255)
    m[..., 1] = np.round(g * aa * 255)
    m[..., 2] = np.round(b * aa * 255)
    return stats


# ---------------------------------------------------------------- whole (분할 없는 효과)
# 나누기 어려운 팩(광택/렌더링 그림)을 위한 가장 안전한 방식: 버튼 그림 전체를 원래 색 그대로 "바탕" 하나로 본다.
# 부위별 색 바꾸기는 없지만 전체 색조/채도/밝기, 투명도, 엠보싱/안쪽 그림자, 유리처럼 버튼 모양만 있으면 되는
# 효과는 모두 걸린다. 모드는 이런 프레임(kind 2)의 바탕색을 항상 원본 픽셀로 칠한다 (바탕색 설정을 무시).
# 바깥의 반투명 검은 그림자는 원본 유지.
OPTIONS = {"whole_fallback": False}   # vanilla/glossy에서 나누지 못한 버튼을 whole로 (--whole-fallback)


def is_button_name(name: str, single: bool) -> bool:
    return single or (bool(VAN_BUTTON_RE.search(name)) and not VAN_EXCLUDE_RE.search(name))


def segment_for(profile: str, override: dict | None) -> str:
    """영역 나누는 방식: 프로필 기본값(vanilla = 검은 선, glossy = 색 경계). 보정 파일의 프레임 항목
    "segment": "color" | "lines"로 버튼 하나만 바꿀 수 있다 (검은 선이 끊겨 아이콘이 바탕과 붙는 버튼 등)"""
    seg = (override or {}).get("segment")
    if seg in ("color", "lines"):
        return seg
    return "color" if profile == "glossy" else "lines"


def build_whole_mask(rgb: np.ndarray, aa: np.ndarray, m: np.ndarray, name: str, single: bool) -> dict:
    stats = {"themed": False, "gradRatio": 0.0, "fillRatio": 0.0, "explained": 0.0, "outline": "none", "glyph": "none"}
    if aa.size == 0 or not is_button_name(name, single) or not (aa > 0.5).any():
        return stats
    shadow = (rgb.max(-1) < VAN_LINE) & (aa < VAN_SHADOW_ALPHA)
    # 버튼 바깥으로 이어진 그림자(드롭 섀도)만 뺀다. 버튼 안쪽 그림자(그림 아래 그늘 등)까지 빼면
    # 몸통에 구멍이 생겨 엠보싱/안쪽 그림자가 그 둘레를 파낸다
    shadow &= ~ndimage.binary_fill_holes((aa > 0.02) & ~shadow)
    g = np.where(shadow, 0.0, 1.0)
    stats.update(themed=True, whole=True, fill="textured", fillRatio=1.0, explained=1.0,
                 coverage=round(float((g * (aa > 0.02)).sum()) / max(float((aa > 0.02).sum()), 1), 3))
    m[..., 0] = 0
    m[..., 1] = np.round(g * aa * 255)
    m[..., 2] = 0
    return stats


# 영역 종류: 보정 파일/분석 도구에서 쓰는 이름 (별칭 포함)
VAN_CLASSES = ["keep", "outline", "body", "icon"]
VAN_CLASS_ALIASES = {"keep": 0, "original": 0, "outline": 1, "rim": 1, "body": 2, "fill": 2, "icon": 3, "glyph": 3}


def build_vanilla_mask(rgb: np.ndarray, aa: np.ndarray, m: np.ndarray, force, name: str, single: bool,
                       override: dict | None = None, info: dict | None = None, segment: str = "lines") -> dict:
    """
    override: 보정 파일의 프레임 항목 {"themed": bool, "regions": {"영역 번호": "outline|body|icon|keep"}}
    info: 넘기면 분석용 정보(영역 번호 지도, 영역별 수치/판정/이유)를 채운다 (inspect_frames.py)
    segment: "lines" = 검은 선으로 나눈다 (vanilla 프로필), "color" = 색/밝기 경계로 나눈다 (glossy 프로필)
    """
    st8 = np.ones((3, 3), bool)
    # 버튼 안쪽으로 볼 최소 알파: 광택 팩은 반투명 유리판(알파 0.3 정도)도 버튼의 일부다
    solid = GLOSSY_ALPHA if segment == "color" else 0.5
    opaque = aa > solid
    mx, mn = rgb.max(-1), rgb.min(-1)
    dark = mx < VAN_LINE
    line = dark & (aa >= VAN_SHADOW_ALPHA)
    shadow = dark & (aa > 0.02) & (aa < VAN_SHADOW_ALPHA)
    sure = opaque & (mx >= VAN_SURE)
    n_opaque = max(int(opaque.sum()), 1)
    stats = {"themed": False, "gradRatio": 0.0, "fillRatio": 0.0, "explained": 0.0, "outline": "none", "glyph": "none"}

    # 영역 번호: 검은 선/그림자로 나뉜 밝은 덩어리마다 1부터 (같은 그림이면 항상 같은 번호)
    if segment == "color":
        # 광택 팩: 어두운 유리 바탕도 영역이다 (검은 선이 아니다). 선이 없으므로 선 두께는 0
        labels, n = color_regions(rgb, aa)
        sure = labels > 0
        line = np.zeros_like(line)
    else:
        labels, n = ndimage.label(sure, structure=st8)
    # 보정 파일 "splitColor": [영역 번호, ...] — 그 영역 안에서 영역의 주된 색(중앙값)과 크게 다른 픽셀을 새 영역으로
    # 떼어 낸다. 검은 선이 끊겨 아이콘(빨간 금지 표시, 노란 사람, 두루마리)이 바탕과 한 덩어리가 된 버튼용.
    # 새 영역 번호는 기존 번호 뒤에 붙으므로 다른 영역 번호는 바뀌지 않는다
    split_ids = [int(v) for v in (override or {}).get("splitColor", [])]
    if split_ids:
        lab = rgb_to_lab(rgb)
        for rid in split_ids:
            if not 1 <= rid <= n:
                continue
            comp = labels == rid
            ref = np.median(lab[comp], axis=0)
            far = ndimage.binary_opening(comp & (np.linalg.norm(lab - ref, axis=-1) > VAN_SPLIT_DE))
            sub, k = ndimage.label(far, structure=st8)
            for j in range(1, k + 1):
                piece = sub == j
                if piece.sum() >= 12:
                    n += 1
                    labels[piece] = n
    if info is not None:
        info.update(labels=labels, n=n, regions=[], themed=False, errors=[])
    if n == 0:
        return stats
    idx = np.arange(1, n + 1)
    size = ndimage.sum(np.ones_like(mx), labels, idx)
    mean = np.stack([ndimage.mean(rgb[..., c], labels, idx) for c in range(3)], -1)
    mean_min, mean_max = mean.min(-1), mean.max(-1)
    # 광택 팩의 은색 테두리(베젤)는 바닐라의 흰 링보다 어둡고 살짝 푸르다
    light_min, light_sat = (GLOSSY_LIGHT_MIN, GLOSSY_LIGHT_SAT) if segment == "color" else (VAN_LIGHT_MIN, VAN_LIGHT_SAT)
    light = (mean_min >= light_min) & ((mean_max - mean_min) / np.maximum(mean_max, 1e-6) <= light_sat)

    exterior = np.pad((aa <= solid) | shadow, 1, constant_values=True)
    ext_near = ndimage.binary_dilation(exterior, iterations=2)[1:-1, 1:-1]
    touch_ext = np.zeros(n + 1, bool)
    touch_ext[np.unique(labels[ext_near & sure])] = True
    touch_ext[0] = False

    rim = touch_ext[1:] & light
    # 바깥 가장자리 띠: 실루엣(투명/그림자)에서 "테두리 두께 + 바깥 검은 선 두께" 안쪽까지.
    # 선 연결로 판단하면 안 된다: 바닐라 버튼은 안쪽 선(십자 팔 사이, 아이콘 외곽선)이 바깥 선과 이어져 있다
    dist_ext = ndimage.distance_transform_edt(np.pad(~((aa <= solid) | shadow), 1))[1:-1, 1:-1]
    rim_px = np.isin(labels, idx[rim])
    rim_t = 2.0 * float(np.median(dist_ext[rim_px])) if rim_px.any() else 0.0
    line_t = 2.0 * float(np.median(ndimage.distance_transform_edt(line)[line])) if line.any() else 0.0
    # 깊이(바깥에서의 최소 거리)
    depth = ndimage.minimum(dist_ext, labels, idx)

    # 영역마다 둘레 중 바깥 가장자리 띠에 닿는 비율
    def contacts(outer_near):
        contact = np.zeros(n)
        for i in np.nonzero(~rim)[0]:
            comp = labels == i + 1
            ring = ndimage.binary_dilation(comp, iterations=3) & ~comp
            contact[i] = (ring & outer_near).sum() / max(ring.sum(), 1)
        return contact

    contact = contacts(dist_ext <= rim_t + 1.5 * line_t + 4)
    if not (~rim & (contact >= VAN_BODY_CONTACT)).any():
        # 바깥 검은 선이 유난히 두꺼운 버튼(편집/공유)은 위 띠가 바탕까지 닿지 않는다 → 흰 테두리/바깥에서
        # 가장 가까운 큰 안쪽 영역까지의 실제 거리로 띠를 다시 잡는다 (항상 쓰면 테두리 가까이 놓인 글자가 바탕이 된다)
        dist_out = ndimage.distance_transform_edt(np.pad(~((aa <= solid) | shadow | rim_px), 1))[1:-1, 1:-1]
        gap = ndimage.minimum(dist_out, labels, idx)
        big = ~rim & (size >= 0.02 * n_opaque)
        if big.any():
            contact = contacts(dist_out <= float(gap[big].min()) + 4)

    reasons = ["" for _ in range(n)]
    for i in np.nonzero(rim)[0]:
        reasons[i] = "outline: light region touching the outside"
    # 바탕: 바깥 껍질에 닿는 가장 큰 영역(주 바탕) + 둘레 대부분이 바깥 가장자리인 영역(플레이 버튼 십자
    #       모서리의 다른 색 조각) + 아이콘에 둘러싸인 구멍 중 바탕과 색이 비슷한 것(톱니 가운데).
    # 나머지(바탕에 둘러싸였거나 가장자리에 살짝만 닿는 큰 그림: 차고 얼굴, 크리에이터 도구)는 아이콘.
    # 구멍은 "둘러싼 것이 아이콘"일 때만 바탕으로 본다: 바탕 옆에 놓인 같은 계열 색 그림(Paths 버튼의 초록 보석)은
    # 아이콘이다. 색은 주 바탕 하나가 아니라 바탕 전체 평균과 비교한다 (명암 때문에 양쪽 눈이 다르게 갈리지 않게)
    body = np.zeros(n, bool)
    cand = np.nonzero(~rim & (contact >= VAN_BODY_CONTACT))[0]
    main, floor = -1, 0.0
    if len(cand):
        # 깊이: 바탕은 흰 테두리 + 검은 선 안쪽에서 시작한다. 테두리 위로 삐져나온 그림
        # (뉴그라운즈 버튼의 탱크, 체크박스의 체크, FREE STUFF / MORE GAMES 글자)은 바탕보다 바깥에 가까우므로
        # 주 바탕/바탕 조각이 아니다. 기준 깊이는 버튼의 큰 영역(10% 이상, 없으면 1% 이상)에서 가장 깊은 값:
        # 작은 글자(COPY)는 바탕보다 깊어도 기준이 되지 않는다. 그 깊이 근처의 가장 큰 영역이 주 바탕.
        # 같은 깊이의 다른 색 조각(Rainbow 십자 모서리의 흰 칸, 플레이 버튼 십자 모서리)은 바탕 조각
        ref = cand[size[cand] >= 0.10 * n_opaque]
        if not len(ref):
            ref = cand[size[cand] >= 0.01 * n_opaque]
        if not len(ref):
            ref = cand
        dmax = float(depth[ref].max())
        floor = dmax - max(2.0, 0.2 * dmax)   # 이보다 바깥에 가까우면 테두리 위로 삐져나온 그림
        pool = cand[depth[cand] >= floor]
        main = pool[np.argmax(size[pool])]
        body[main] = True
        reasons[main] = "body: main body (largest region at body depth)"
        for i in np.nonzero(~rim)[0]:
            if i != main and contact[i] >= VAN_EDGE_CONTACT and floor <= depth[i] <= depth[main] + 3:
                body[i] = True
                reasons[i] = f"body: edge piece (edge contact {contact[i]:.2f}, same depth as body)"
        body_mean = (mean[body] * size[body, None]).sum(0) / size[body].sum()
        icon_cand = ~rim & ~body
        reach = int(np.ceil(2 * line_t)) + 3   # 구멍을 감싼 검은 선 너머까지
        body_ids = np.nonzero(body)[0]
        for i in np.nonzero(icon_cand)[0]:
            # 글자/아이콘에 잘려 나뉜 바탕 조각: 가장자리에 조금이라도 닿고, 바탕 조각 중 하나와 색이 같다
            # (바탕은 위아래로 밝기가 달라서 전체 평균이 아니라 가장 가까운 조각과 비교한다)
            if contact[i] >= VAN_SPLIT_CONTACT and floor <= depth[i] <= depth[main] + 3 and \
                    np.linalg.norm(mean[body_ids] - mean[i], axis=-1).min() < VAN_HOLE_DIST:
                body[i] = True
                reasons[i] = "body: piece of the body cut off by an icon or text (same color, touches the edge)"
                continue
            # 밝기를 뺀 색 비교는 색이 분명한 경우만 (무채색은 흰색과 회색이 같은 "색"이 되어 흰 글자가 회색 바탕의
            # 구멍으로 잡힌다: Cobblestone COPY 버튼)
            chroma = mean[i] / max(float(mean[i].max()), 1e-6)
            body_chroma = mean[body_ids] / np.maximum(mean[body_ids].max(-1, keepdims=True), 1e-6)
            body_sat = 1 - body_chroma.min(-1)
            chroma_ok = (1 - chroma.min() >= 0.2) & (body_sat >= 0.2) &                 (np.linalg.norm(body_chroma - chroma, axis=-1) < VAN_HOLE_CHROMA)
            if np.linalg.norm(body_mean - mean[i]) >= VAN_HOLE_DIST and not chroma_ok.any():
                continue
            comp = labels == i + 1
            around = labels[ndimage.binary_dilation(comp, structure=st8, iterations=reach) & ~comp]
            around = around[around > 0] - 1
            if len(around) and icon_cand[around].mean() >= 0.9:
                # 둘러싼 아이콘 자체가 바탕과 같은 색 계열이면 구멍이 아니라 그 아이콘의 안쪽 면이다
                # (Paths 버튼의 초록 보석 안쪽). 진짜 구멍은 다른 색 아이콘(노란 트로피, 흰 음표)에 뚫려 있다
                host = np.bincount(around[icon_cand[around]]).argmax()
                host_chroma = mean[host] / max(float(mean[host].max()), 1e-6)
                if np.linalg.norm(body_chroma - host_chroma, axis=-1).min() < VAN_HOLE_CHROMA:
                    continue
                body[i] = True
                reasons[i] = "body: hole inside the icon showing the body (same color as body, shading ignored)"
    icon = ~rim & ~body
    tiny = size < 12   # 점 같은 잡티는 아이콘으로 치지 않는다
    for i in np.nonzero(icon)[0]:
        if tiny[i]:
            reasons[i] = "keep: tiny speck (< 12 px)"
        elif main < 0:
            reasons[i] = "keep: no body found in this frame"
        elif contact[i] >= VAN_BODY_CONTACT and depth[i] < floor:
            reasons[i] = "icon: reaches out over the rim (closer to the edge than the body)"
        elif contact[i] >= VAN_BODY_CONTACT and depth[i] > depth[main] + 3:
            reasons[i] = "icon: near the edge but deeper than the body"
        else:
            reasons[i] = "icon: drawn on top of the body"
    icon &= ~tiny

    auto = np.zeros(n, np.int8)
    auto[rim], auto[body], auto[icon] = 1, 2, 3
    final = auto.copy()
    keep_ids = []
    if override:
        for key, value in (override.get("regions") or {}).items():
            try:
                rid = int(key)
            except (TypeError, ValueError):
                rid = -1
            c = VAN_CLASS_ALIASES.get(str(value).lower())
            if not 1 <= rid <= n:
                msg = f"{name}: region {key} does not exist (this frame has regions 1..{n})"
            elif c is None:
                msg = f"{name}: region {key}: unknown class '{value}' (use outline, body, icon or keep)"
            else:
                final[rid - 1] = c
                reasons[rid - 1] = f"override: {VAN_CLASSES[c]} (auto: {VAN_CLASSES[auto[rid - 1]]})"
                if c == 0:
                    keep_ids.append(rid)
                continue
            stats.setdefault("overrideErrors", []).append(msg)
            if info is not None:
                info["errors"].append(msg)
    rim, body, icon = final == 1, final == 2, final == 3

    body_px = float(size[body].sum())
    stats["fillRatio"] = round(body_px / n_opaque, 3)
    is_button = single or (bool(VAN_BUTTON_RE.search(name)) and not VAN_EXCLUDE_RE.search(name))
    themed = body.any() and body_px / n_opaque >= 0.15 and (rim.any() or icon.any() or single) and is_button
    if force is not None:
        themed = force
    if override and override.get("themed") is not None:
        themed = bool(override["themed"])
    if info is not None:
        info["themed"] = bool(themed)
        info["why"] = ("themed" if themed else
                       "not a button name" if not is_button else
                       "no body found" if not body.any() else
                       f"body too small ({body_px / n_opaque:.0%} of the button, needs 15%)" if body_px / n_opaque < 0.15 else
                       "only a body, no outline or icon")
        for i in range(n):
            info["regions"].append({
                "id": i + 1, "class": VAN_CLASSES[final[i]], "auto": VAN_CLASSES[auto[i]], "reason": reasons[i],
                "px": int(size[i]), "color": "#%02x%02x%02x" % tuple(int(round(v * 255)) for v in mean[i]),
                "depth": round(float(depth[i]), 1), "edgeContact": round(float(contact[i]), 2),
                "light": bool(light[i]),
            })
    if not themed:
        stats["themed"] = False
        return stats

    cls = np.zeros(n + 1, np.int8)
    cls[1:] = final
    pix = cls[labels]   # 확실한 픽셀의 종류 (0 = 아님)

    # 섞인 가장자리: 가장 가까운 확실한 픽셀 색 X와 검정(선/그림자) 사이의 비율 t만큼 그 종류로
    dist, (iy, ix) = ndimage.distance_transform_edt(pix == 0, return_indices=True)
    X = rgb[iy, ix]
    t = np.clip((rgb * X).sum(-1) / np.maximum((X * X).sum(-1), 1e-6), 0, 1)
    near_cls = pix[iy, ix]
    w = np.where(pix > 0, 1.0, np.where((dist <= VAN_REFINE) & (aa > 0.02) & ~shadow, t, 0.0))
    if keep_ids:   # 보정 파일에서 "keep"으로 지정한 영역은 가장자리 섞임도 원본 그대로
        w = np.where(np.isin(labels, keep_ids), 0.0, w)
    cl = np.where(pix > 0, pix, near_cls)
    r = np.where(cl == 1, w, 0.0)
    g = np.where(cl == 2, w, 0.0)
    b = np.where(cl == 3, w, 0.0)

    if rim.any():
        stats["outline"] = "white"
    if icon.any():
        icon_light = float(size[icon & light].sum()) >= 0.5 * float(size[icon].sum())
        stats["glyph"] = "white" if icon_light else "gradient"
    stats.update(themed=True, fill="textured",
                 explained=round(float(((pix > 0) | line) [opaque].mean()), 3),
                 coverage=round(float(((r + g + b) * (aa > 0.02)).sum()) / max(float((aa > 0.02).sum()), 1), 3))
    m[..., 0] = np.round(r * aa * 255)
    m[..., 1] = np.round(g * aa * 255)
    m[..., 2] = np.round(b * aa * 255)
    return stats


def build_frame_mask(rgb: np.ndarray, aa: np.ndarray, f: float, m: np.ndarray, force=None,
                     profile: str = "diamond", split_icon: bool = False, name: str = "", single: bool = False,
                     override: dict | None = None) -> dict:
    if profile == "whole":
        # 분할 없는 팩은 모든 버튼을 한 덩어리로 (손으로 칠한 마스크/영역 보정이 섞이면 같은 설정에 버튼마다 다르게 반응한다)
        if force is False or (override or {}).get("themed") is False:
            return {"themed": False, "gradRatio": 0.0, "fillRatio": 0.0, "explained": 0.0,
                    "outline": "none", "glyph": "none"}
        stats = build_whole_mask(rgb, aa, m, name, single)
        if (override or {}).get("mask") or (override or {}).get("regions"):
            stats["overrideIgnored"] = True
        return stats
    if (override or {}).get("mask"):   # 손으로 그린 마스크가 있으면 어떤 프로필이든 그걸 쓴다
        if force is False or override.get("themed") is False:
            return {"themed": False, "gradRatio": 0.0, "fillRatio": 0.0, "explained": 0.0,
                    "outline": "none", "glyph": "none"}
        return build_painted_mask(rgb, aa, m, override["mask"], bool(override.get("_rotated")))
    if profile == "whole" or (override or {}).get("whole"):
        if force is False or (override or {}).get("themed") is False:
            return {"themed": False, "gradRatio": 0.0, "fillRatio": 0.0, "explained": 0.0,
                    "outline": "none", "glyph": "none"}
        return build_whole_mask(rgb, aa, m, name, single)
    if profile in ("vanilla", "glossy"):
        stats = build_vanilla_mask(rgb, aa, m, force, name, single, override, segment=segment_for(profile, override))
        # 나누지 못한 버튼은 분할 없는 효과라도 받게 (보정 파일에서 "themed": false로 뺀 것은 제외)
        if OPTIONS["whole_fallback"] and not stats["themed"] and force is not False and \
                (override or {}).get("themed") is not False:
            whole = build_whole_mask(rgb, aa, m, name, single)
            if whole["themed"]:
                return whole
        return stats
    """프레임 하나(시트에서 잘라낸 영역)의 마스크를 m에 써 넣고 통계를 돌려준다."""
    c = classify(rgb, aa, f, profile)
    t, grad, white, fill = c["t"], c["grad"], c["white"], c["fill"]
    opaque = aa > 0.5
    n_opaque = max(int(opaque.sum()), 1)
    grad_ratio = float((grad & (t > 0.5) & opaque).sum()) / n_opaque
    fill_ratio = float((fill & opaque).sum()) / n_opaque
    explained = float(((grad | white | fill) & opaque).sum()) / n_opaque

    if profile == "generic":
        merge_neutral(c, rgb, aa, f)
        grad = c["grad"]
        explained = float(((grad | white | fill) & opaque).sum()) / n_opaque
        # 아무 색이나 그라데이션 후보가 되므로 바닐라 스프라이트(얼굴, 코인 등)를 걸러야 한다:
        # 어두운 바탕이 충분히 있거나, 바탕 없이도 색상이 한 방향으로 무지개처럼 변해야 테마 대상.
        span = gradient_span(rgb, grad & opaque & (c["sat"] > 0.3))
        themed = explained >= THEMED_EXPLAINED_RATIO and (
            fill_ratio >= GENERIC_FILL_RATIO or (grad_ratio >= 0.3 and span >= 100))
    else:
        themed = ((grad_ratio >= THEMED_GRAD_RATIO or fill_ratio >= THEMED_FILL_RATIO)
                  and explained >= THEMED_EXPLAINED_RATIO)
        # 어두운 바탕이 거의 없는 프레임은 "파스텔 색"이라는 것만으로는 부족하다: 바닐라 난이도 얼굴
        # (Easy 하늘색, Normal 초록, Insane 분홍)도 채도 40%/명도 100% 조건에 걸린다.
        # 이런 프레임은 색상이 한 방향으로 무지개처럼 변할 때만 테마 대상으로 본다.
        if themed and fill_ratio < THEMED_FILL_RATIO:
            span = gradient_span(rgb, grad & (t > 0.5) & opaque)
            themed = span >= DIAMOND_MIN_SPAN
    if force is not None:
        themed = force
    stats = {"themed": themed, "gradRatio": round(grad_ratio, 3), "fillRatio": round(fill_ratio, 3),
             "explained": round(explained, 3), "outline": "none", "glyph": "none"}
    if profile == "generic":
        stats["hueSpan"] = round(span)
    if not themed:
        return stats

    # 프레임마다 바탕색이 다를 수 있다 (예: 로고 글자는 순검정). 테마 대상으로 확정된 뒤에만 검정까지 포함해 다시 찾는다.
    ff = detect_fill(rgb, aa, lo=0.0, min_count=50, default=f)
    if ff != f:
        f = ff
        c = classify(rgb, aa, f, profile)
        if profile == "generic":
            merge_neutral(c, rgb, aa, f)
    if profile == "generic":
        c["f"] = f
        generic_refit(c, rgb)
    t, grad, white, fill = c["t"], c["grad"], c["white"], c["fill"]
    stats["fill"] = "#%02x%02x%02x" % ((round(f * 255),) * 3)

    # 테두리와 안쪽 글리프 구분: 바깥(투명 영역 또는 그림자)에 닿는 덩어리 = 테두리, 바탕 안에 떠 있는 덩어리 = 글리프.
    # 버튼 아래 그림자는 알파가 0.5 이상인 경우가 있어서, 바탕색이 아닌 검정 픽셀도 바깥으로 본다.
    # 안 그러면 그림자에 닿은 테두리 조각이 글리프로 분류되어 글리프 색(예: 흰색)으로 칠해진다.
    # 버튼 안쪽의 검은 디테일은 제외: 투명 영역과 이어진 검은 덩어리만 그림자로 본다.
    dark = ~fill & ~grad & ~white & (rgb.max(-1) < 0.06) & (aa >= 0.5)
    transparent = np.pad(aa < 0.5, 1, constant_values=True)
    dark_lab, _ = ndimage.label(dark, structure=np.ones((3, 3), bool))
    near_out = ndimage.binary_dilation(transparent, iterations=1)[1:-1, 1:-1]
    shadow_ids = np.unique(dark_lab[near_out & dark])
    shadow = np.isin(dark_lab, shadow_ids[shadow_ids > 0])
    exterior = np.pad((aa < 0.5) | shadow, 1, constant_values=True)
    exterior = ndimage.binary_dilation(exterior, iterations=3)[1:-1, 1:-1]

    def split(cat):
        core = cat & (t > 0.5) & opaque
        labels, _ = ndimage.label(core, structure=np.ones((3, 3), bool))
        touching = np.unique(labels[exterior & core])
        outline_core = np.isin(labels, touching[touching > 0])
        # 테두리에서 2px 안에 붙어 있는 조각은 끊긴 테두리 일부로 본다 (진짜 글리프는 바탕만큼 떨어져 있다)
        near = ndimage.binary_dilation(outline_core, iterations=2)
        attached = np.unique(labels[near & core & ~outline_core])
        outline_core |= np.isin(labels, attached[attached > 0])
        inner_core = core & ~outline_core
        inner = cat & ndimage.binary_dilation(inner_core, iterations=2) & ~outline_core
        return cat & ~inner, inner, int(outline_core.sum()), int(inner_core.sum())

    g_out, g_in, n_g_out, n_g_in = split(grad)
    w_out, w_in, n_w_out, n_w_in = split(white)

    # 아이콘이 테두리에 닿아 있으면(크리에이터 버튼의 망치/스패너 등) 위 연결 규칙으로는 전부 테두리가 된다.
    # overrides의 "icons"에 적은 프레임만: 바깥에서 테두리 두께보다 한참 안쪽에 있는 테두리 픽셀을 아이콘으로 떼어 낸다.
    # (자동으로 하면 모서리 장식, 상단 바처럼 두꺼운 장식까지 아이콘이 되어 색 규칙이 바뀐다)
    icon_zone = None
    if split_icon and profile == "generic":
        # 윤곽선이 없는 팩(Diamond Vortex)은 테두리 띠가 없으므로, 바깥에 닿아 테두리로 묶인 그림 전체가 아이콘이다
        # (예: 원 가장자리까지 뻗은 Newgrounds 탱크)
        g_in, g_out = g_in | g_out, np.zeros_like(g_out)
        w_in, w_out = w_in | w_out, np.zeros_like(w_out)
        n_g_in, n_g_out = n_g_in + n_g_out, 0
        n_w_in, n_w_out = n_w_in + n_w_out, 0
        icon_zone = (aa > 0.02) & ~fill
        stats["iconSplit"] = n_g_in + n_w_in
    elif split_icon and not (n_g_in or n_w_in) and n_g_out:
        # 거리는 투명 영역에서만 잰다 (아이콘 자체의 그림자가 바깥 그림자와 이어져 있으면 "가장자리"로 잡히므로)
        # 프레임 경계 밖도 투명으로 본다 (테두리가 프레임 끝에 붙어 있으므로)
        dist = ndimage.distance_transform_edt(np.pad(aa >= 0.5, 1))[1:-1, 1:-1]
        ring = g_out & (t > 0.5) & opaque
        width = 2.0 * float(np.median(dist[ring]))
        far = ring & (dist > max(width * 1.8, width + 8))
        if far.sum() >= max(60, 0.04 * ring.sum()):
            # 테두리 띠(바깥에서 두께 정도)를 뺀 나머지 중 확실히 안쪽(far)과 이어진 조각 전체를 아이콘으로.
            # 테두리 가까이 뻗은 망치 머리/스패너 끝까지 한 덩어리로 옮겨야 줄무늬처럼 갈리지 않는다
            cand = ring & (dist > width * 1.15)
            lab, _ = ndimage.label(cand, structure=np.ones((3, 3), bool))
            ids = np.unique(lab[far])
            core_icon = np.isin(lab, ids[ids > 0])
            inner = g_out & ndimage.binary_dilation(core_icon, iterations=2) & (dist > width * 1.0)
            # 아이콘 선의 밝은 하이라이트/가장자리 픽셀은 흰색 쪽이나 2차 패스에서 테두리로 갈 수 있다 → 마지막에 정리
            icon_zone = ndimage.binary_dilation(core_icon, iterations=8) & (dist > width * 1.2)
            g_in = g_in | inner
            g_out = g_out & ~inner
            n_g_in = int((inner & (t > 0.5)).sum())
            n_g_out = int((g_out & (t > 0.5) & opaque).sum())
            stats["iconSplit"] = n_g_in

    # 채널마다 다수파 스타일 하나만 담는다 (프레임 단위 메타데이터로 원래 색을 복원)
    if n_g_out or n_w_out:
        stats["outline"] = "gradient" if n_g_out >= n_w_out else "white"
    if n_g_in or n_w_in:
        stats["glyph"] = "gradient" if n_g_in > n_w_in else "white"
    outline = g_out if stats["outline"] == "gradient" else w_out if stats["outline"] == "white" else None
    glyph = g_in if stats["glyph"] == "gradient" else w_in if stats["glyph"] == "white" else None
    outline = np.zeros_like(grad) if outline is None else outline.copy()
    glyph = np.zeros_like(grad) if glyph is None else glyph.copy()

    # 소수파 픽셀은 같은 스타일을 가진 채널이 있으면 그쪽으로, 없으면 원본 유지
    for cat, style in ((grad, "gradient"), (white, "white")):
        rest = cat & ~outline & ~glyph
        if stats["outline"] == style:
            outline |= rest
        elif stats["glyph"] == style:
            glyph |= rest

    r = np.where(outline, t, 0)
    b = np.where(glyph, t, 0)
    g = np.where(outline | glyph, 1 - t, 0) + np.where(fill, 1.0, 0)

    # 2차 패스: 1차에서 분류 못 한 가장자리 픽셀 처리.
    # 알파가 낮은 바깥 안티앨리어싱 픽셀은 PNG 최적화로 색이 양자화되어 있고, 바탕과 섞인 어두운 픽셀은
    # 혼합 비율이 작아 색상 역산이 안 된다. 그대로 두면 테두리 색을 바꿔도 무지개 잔상으로 남는다.
    # 가장 가까운 "확실한" 테두리/글리프 픽셀의 색(X)을 기준으로 다시 분해한다.
    sure = (outline | glyph) & (t > 0.5) & opaque
    unresolved = (aa > 0.02) & ~(outline | glyph | fill)
    if sure.any() and unresolved.any():
        dist, (iy, ix) = ndimage.distance_transform_edt(~sure, return_indices=True)
        cand = unresolved & (dist <= REFINE_DIST)
        Xr = c["X"][iy, ix]
        d = Xr - f
        s = np.clip(((rgb - f) * d).sum(-1) / np.maximum((d * d).sum(-1), 1e-6), 0, 1)
        resid_mix = np.linalg.norm(rgb - (f + s[..., None] * d), axis=-1)
        solid = aa >= 0.9
        # 반투명 바깥 픽셀은 X가 검정 쪽으로 어두워진 색으로 저장된 경우가 많다 (예: 연두 X의 47% 밝기).
        # 검정→X 직선에 투영해서 X 비율만 테두리로 넘기고, 남는 어두운 부분은 원본(검정)으로 둔다.
        # 그림자처럼 검은 반투명 픽셀은 비율이 0에 가까워 그대로 그림자로 남는다.
        s_edge = np.clip((rgb * Xr).sum(-1) / np.maximum((Xr * Xr).sum(-1), 1e-6), 0, 1)
        resid_edge = np.linalg.norm(rgb - s_edge[..., None] * Xr, axis=-1)
        # 흰색 쪽으로 밝아진 그라데이션 (X와 흰색의 혼합): 같은 채널의 한 픽셀로 본다
        dw = Xr - 1.0
        s_w = np.clip(((rgb - 1.0) * dw).sum(-1) / np.maximum((dw * dw).sum(-1), 1e-6), 0, 1)
        resid_w = np.linalg.norm(rgb - (1.0 + s_w[..., None] * dw), axis=-1)
        ok_light = cand & solid & (resid_w < 0.12) & (resid_w < resid_mix)
        ok_mix = cand & solid & ~ok_light & (resid_mix < 0.2)
        ok_edge = cand & ~solid & (resid_edge < 0.2 + 0.9 * (1 - aa))
        # 남는 어두운 부분을 원본으로 두면 그 원본 색(무지개)이 다시 섞여 보인다. 테두리 색이 분명한 가장자리는
        # 전부 테두리로 넘긴다 (원본보다 가장자리가 아주 살짝 밝아지는 대신 잔상이 없다).
        edge_share = np.clip(s_edge * EDGE_GAIN, 0, 1)
        share = np.where(ok_light, 1.0, np.where(ok_mix, s, np.where(ok_edge, edge_share, 0.0)))
        to_outline = outline[iy, ix]
        r = r + np.where(to_outline, share, 0)
        b = b + np.where(to_outline, 0, share)
        g = g + np.where(ok_mix, 1 - s, 0)
        unresolved &= ~(ok_mix | ok_edge | ok_light)
        stats["refined"] = int((ok_mix | ok_edge | ok_light).sum())

    # 3차: 테두리에서 멀리 퍼진 반투명 무지개 글로우 (상단 바, 테이블 테두리, 별 글로우 등).
    # 색은 낮은 알파에서 심하게 양자화되어 있어 역산할 수 없으므로, 밝기만큼 테두리 색으로 본다.
    # 검은 반투명 그림자는 밝기가 0이라 영향이 없다.
    glow = unresolved & (aa < 0.6) & (rgb.max(-1) - rgb.min(-1) > 0.1)
    if glow.any():
        glow_share = np.where(glow, np.clip(rgb.max(-1) * EDGE_GAIN, 0, 1), 0)
        if stats["outline"] != "none" or stats["glyph"] == "none":
            r = r + glow_share
        else:
            b = b + glow_share
        stats["glow"] = int(glow.sum())

    if icon_zone is not None:
        b = b + np.where(icon_zone, r, 0)
        r = np.where(icon_zone, 0, r)

    total = float(((r + g + b) * (aa > 0.02)).sum()) / max(float((aa > 0.02).sum()), 1)
    stats["coverage"] = round(total, 3)

    # 원본 알파를 곱해서 저장한다 (premultiplied). 셰이더에서 텍스쳐 알파로 나눠 복원하면
    # 밉맵/필터링으로 가장자리가 섞여도 비율이 유지된다. cocos2d-x도 텍스쳐를 premultiply 한다.
    m[..., 0] = np.round(r * aa * 255)
    m[..., 1] = np.round(np.clip(g, 0, 1) * aa * 255)
    m[..., 2] = np.round(b * aa * 255)
    return stats


def debug_image(mask: np.ndarray, a: np.ndarray, themed_area: np.ndarray) -> Image.Image:
    """마스크 확인용: R/G/B = 테두리/채우기/글리프, 회색 = 원본 유지, 어두운 회색 = 테마 미적용 프레임."""
    img = mask.astype(np.float32) / np.maximum(a, 1 / 255)[..., None]
    img = np.clip(img, 0, 255)
    leftover = 1 - img.sum(-1) / 255
    orig = (a > 0.5) & (leftover > 0.5)
    img[orig & themed_area] = (150, 150, 150)
    img[orig & ~themed_area] = (45, 45, 45)
    return Image.fromarray(img.astype(np.uint8), "RGB")


# ---------------------------------------------------------------- 메인

def process_texture(png: Path, frames: dict, out_dir: Path, overrides: dict, kind: str, profile: str) -> dict:
    im = np.asarray(Image.open(png).convert("RGBA")).astype(np.float32) / 255
    rgb, a = im[..., :3], im[..., 3]
    f = detect_fill(rgb, a)
    mask = np.zeros(rgb.shape, np.uint8)
    themed_area = np.zeros(a.shape, bool)

    ignored = []
    for name, fr in frames.items():
        x, y, w, h = fr["rect"]
        sl = (slice(y, y + h), slice(x, x + w))
        force = True if name in overrides["themed"] else False if name in overrides["plain"] else None
        over = overrides.get("frames", {}).get(name)
        if over and over.get("mask"):
            over = {**over, "_rotated": fr["rotated"]}
        fr.update(build_frame_mask(rgb[sl], a[sl], f, mask[sl], force, profile,
                                   name in overrides.get("icons", []), name, kind == "single", over))
        if "paintError" in fr:
            print(f"  [overrides] {name}: {fr.pop('paintError')}")
        if fr.pop("overrideIgnored", False):
            ignored.append(name)
        for err in fr.pop("overrideErrors", []):
            print(f"  [overrides] {err}")
        if fr["themed"]:
            themed_area[sl] = True

    if ignored:
        print(f"  [overrides] profile whole keeps every button in one piece: painted masks / region fixes of "
              f"{len(ignored)} buttons ignored")
    stem = png.stem
    Image.fromarray(mask, "RGB").save(out_dir / "masks" / f"{stem}-mask.png", optimize=True)
    debug_image(mask, a, themed_area).save(out_dir / "debug" / f"{stem}-debug.png")
    shutil.copy2(png, out_dir / "textures" / png.name)

    return {"name": stem, "kind": kind, "image": f"textures/{png.name}", "mask": f"masks/{stem}-mask.png",
            "size": [im.shape[1], im.shape[0]], "fill": "#%02x%02x%02x" % ((round(f * 255),) * 3),
            "frames": frames}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pack", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--overrides", type=Path,
                    help='{"themed": [...], "plain": [...], "icons": [...], "frames": {...}} 형식으로 자동 판정을 덮어쓴다. '
                         'icons: 테두리에 닿아 테두리로 묶인 아이콘을 따로 떼어 낼 프레임. '
                         'frames (vanilla 프로필): {"프레임.png": {"themed": true, "regions": {"5": "icon"}}} — '
                         '영역 번호는 inspect_frames.py로 확인')
    ap.add_argument("--profile", choices=["diamond", "generic", "vanilla", "glossy", "whole"], default="diamond",
                    help="diamond: 파스텔 무지개 테두리 공식 사용 (Diamond Pack). "
                         "generic: 공식 없이 어두운 바탕 위의 색/흰색 요소를 분리 (Diamond Vortex 등). "
                         "vanilla: 바닐라 구조(흰 테두리/검은 선/색 바탕/아이콘)를 선으로 나눈 영역으로 분리 "
                         "(바닐라 GD, 색만 바꾼 팩). "
                         "glossy: 검은 선 없이 색/밝기 경계로 나눈 뒤 vanilla와 같은 규칙으로 판정 (광택/렌더링 팩). "
                         "whole: 나누지 않고 버튼 그림 전체를 원래 색 그대로 바탕 하나로 (분할 없는 효과만)")
    ap.add_argument("--whole-fallback", action="store_true",
                    help="vanilla/glossy에서 나누지 못한 버튼 이름의 프레임은 whole로 (분할 없는 효과라도 받게)")
    ap.add_argument("--name", help="미리보기에 표시할 팩 이름 (기본: 파일 이름)")
    ap.add_argument("--base-plists", type=Path,
                    help="팩에 .plist가 없는 시트는 이 폴더(보통 GD Resources)의 같은 이름 plist를 쓴다 "
                         "(시트 이미지만 바꾸고 배치는 바닐라 그대로인 팩)")
    ap.add_argument("--only", help="이 정규식에 맞는 파일 이름만 처리 (예: 'GJ_GameSheet0[34]|GJ_button|GJ_square')")
    ap.add_argument("--local", action="store_true",
                    help="--register 할 때 미리보기 전용으로 표시 (export_mod_data.py가 모드로 내보내지 않음). "
                         "다른 사람의 팩을 분석/테스트할 때")
    ap.add_argument("--register", action="store_true",
                    help="출력 폴더의 상위 폴더 packs.json에 이 팩을 추가해 미리보기 팩 목록에 띄운다")
    args = ap.parse_args()
    OPTIONS["whole_fallback"] = args.whole_fallback

    overrides = {"themed": [], "plain": []}
    if args.overrides:
        overrides.update(json.loads(args.overrides.read_text(encoding="utf-8")))
        # 손으로 그린 마스크 경로는 보정 파일 기준 → 절대 경로로
        for entry in overrides.get("frames", {}).values():
            if entry.get("mask"):
                entry["mask"] = str((args.overrides.parent / entry["mask"]).resolve())

    pack = open_pack(args.pack)
    for sub in ("masks", "debug", "textures"):
        (args.out / sub).mkdir(parents=True, exist_ok=True)

    name = args.name or args.pack.stem
    atlas = {"source": args.pack.name, "name": name, "profile": args.profile, "textures": [], "fonts": []}
    for png in sorted(pack.glob("*-uhd.png")):
        if args.only and not re.search(args.only, png.name):
            continue
        plist = png.with_suffix(".plist")
        if not plist.exists() and args.base_plists and (args.base_plists / plist.name).exists():
            plist = args.base_plists / plist.name
        fnt = png.with_suffix(".fnt")
        if fnt.exists():
            font = load_font(fnt)
            shutil.copy2(png, args.out / "textures" / png.name)
            font.update(name=png.stem, image=f"textures/{png.name}")
            atlas["fonts"].append(font)
            print(f"[font ] {png.name}: {len(font['chars'])} chars ({font.get('face', '')})")
            continue
        if plist.exists():
            frames, kind = load_frames(plist), "sheet"
        else:
            w, h = Image.open(png).size
            frames = {png.name.replace("-uhd", ""): {"rect": [0, 0, w, h], "size": [w, h], "rotated": False,
                                                     "offset": [0, 0], "sourceSize": [w, h]}}
            kind = "single"
        tex = process_texture(png, frames, args.out, overrides, kind, args.profile)
        atlas["textures"].append(tex)

        themed = [n for n, fr in frames.items() if fr["themed"]]
        mixed = [n for n in themed if frames[n].get("coverage", 1) < 0.9]
        print(f"[{kind:5}] {png.name}: fill {tex['fill']}, themed {len(themed)}/{len(frames)} frames"
              + (f", partly original {len(mixed)}" if mixed else ""))

    (args.out / "atlas.json").write_text(json.dumps(atlas, ensure_ascii=False), encoding="utf-8")
    report = {t["name"]: {n: {k: fr[k] for k in ("themed", "whole", "painted", "gradRatio", "fillRatio", "explained", "outline", "glyph", "fill", "refined", "glow", "coverage") if k in fr}
                          for n, fr in t["frames"].items()} for t in atlas["textures"]}
    (args.out / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")

    if args.register:
        reg_path = args.out.parent / "packs.json"
        packs = json.loads(reg_path.read_text(encoding="utf-8")) if reg_path.exists() else []
        entry = {"id": args.out.name, "name": name, "profile": args.profile}
        if args.local:
            entry["local"] = True
        packs = [p for p in packs if p["id"] != entry["id"]] + [entry]
        reg_path.write_text(json.dumps(packs, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"registered '{name}' in {reg_path}")
    print(f"done -> {args.out}")


if __name__ == "__main__":
    sys.exit(main())
