"""마스크 시트마다 거리장(SDF) 텍스쳐를 만든다: 버튼 몸통(테두리+바탕+아이콘 영역) 가장자리까지의 거리.

셰이더가 이걸로 엠보싱(베벨), 안쪽 그림자 같은 모양 효과를 계산한다. 그림자처럼 "원본 유지" 영역은
몸통에 포함되지 않으므로 효과가 버튼 몸통에만 걸린다.

  값 = 0.5 + 거리 / (2 * RANGE)   (안쪽 +, 바깥 -, RANGE 픽셀에서 잘림)
  절반 해상도 RGB PNG: R = 몸통 거리, G = 아이콘 거리 (게임에서는 두 채널 텍스쳐로 올린다)

사용: python make_sdf.py <mod/resources>     (*-mask.png 마다 *-sdf.png 생성)
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

RANGE = 64.0   # 픽셀 (UHD). 셰이더의 sdfAt 배율(2 * RANGE)과 같아야 한다
SCALE = 2      # 해상도 축소 배율
BLUR = 1.5     # 거리값 블러 (픽셀 경계의 계단 → 엠보싱이 쭈글쭈글해 보이던 원인)
# 마스크는 원본 알파가 곱해져 있다 → 반투명 유리판(알파 0.3 안팎)도 몸통으로 보려면 낮은 문턱이 필요하다.
# 0.5로 두면 유리판은 바깥, 불투명한 아이콘/선만 몸통이 되어 엠보싱이 아이콘 둘레를 파낸다
SOLID = 0.15


def encode(region: np.ndarray) -> np.ndarray:
    """영역(bool) → 블러한 부호 거리 → 절반 해상도 8비트"""
    inside = ndimage.distance_transform_edt(region)
    outside = ndimage.distance_transform_edt(~region)
    d = np.where(region, inside - 0.5, -(outside - 0.5))
    d = ndimage.gaussian_filter(d, BLUR)
    h, w = d.shape
    # 절반 해상도 (홀수 크기는 마지막 줄을 복제해 맞춘다)
    H, W = -(-h // SCALE), -(-w // SCALE)
    pad = np.pad(d, ((0, H * SCALE - h), (0, W * SCALE - w)), mode="edge")
    small = pad.reshape(H, SCALE, W, SCALE).mean(axis=(1, 3))
    v = np.clip(0.5 + small / (2 * RANGE), 0, 1)
    return (v * 255 + 0.5).astype(np.uint8)


def sdf_for(mask_path: Path) -> Image.Image:
    """R = 몸통(테두리+바탕+아이콘) 거리, G = 아이콘(글리프) 거리 (아이콘 엠보싱용)"""
    m = np.asarray(Image.open(mask_path).convert("RGB"), dtype=np.float32) / 255
    body = encode(m.sum(-1) > SOLID)
    glyph = encode(m[..., 2] > SOLID)
    return Image.fromarray(np.dstack([body, glyph, body]), "RGB")


def main():
    res = Path(sys.argv[1])
    for mask in sorted(res.glob("*-mask.png")):
        out = mask.with_name(mask.name.replace("-mask.png", "-sdf.png"))
        sdf_for(mask).save(out, optimize=True)
        print(f"{mask.name} -> {out.name}")


if __name__ == "__main__":
    main()
