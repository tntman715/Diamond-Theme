# 텍스쳐 팩 테마 데이터 보정 가이드

Diamond Theme 모드는 텍스쳐 팩의 버튼을 **테두리 / 바탕 / 아이콘**으로 나눠서, 각각 따로 색을 바꾸거나 효과를 입힙니다.
이 "나누기"는 도구가 자동으로 하지만 완벽하지 않습니다. 이 문서는 잘못 나뉜 버튼을 찾아서 고치는 방법을 설명합니다.

> 영어판: [tuning-guide.en.md](tuning-guide.en.md) · LLM에게 맡길 때: [llm-tuning-instructions.md](llm-tuning-instructions.md) · 조각 자체가 잘못 나뉠 때(손으로 칠하기): [hand-masks.ko.md](hand-masks.ko.md)

영역 단위 보정을 지원하는 프로필은 두 가지입니다.
- **vanilla**: 흰 테두리 → 검은 선 → 색 바탕 → 아이콘 구조인 팩 (바닐라 GD와 색만 바꾼 팩들). 검은 선으로 영역을 나눕니다.
- **glossy**: 검은 선이 없는 광택/렌더링 팩 (프루티거 에어로 스타일 등). 색과 밝기가 갑자기 바뀌는 곳으로 영역을 나누고, 판정 규칙은 vanilla와 같습니다.

나누기 어려운 팩을 위한 방식도 있습니다.
- **whole** (분할 없음): 버튼 그림 전체를 원래 색 그대로 바탕 하나로 봅니다. 부위별 색 바꾸기는 안 되지만 전체 색조/채도/밝기, 투명도, 엠보싱, 유리 같은 효과는 모든 버튼에 걸립니다. 보정이 필요 없어서 가장 안전합니다. whole 팩은 **모든 버튼이 한 덩어리**입니다: 손으로 칠한 마스크와 영역 보정은 무시되고, 게임에서는 Geode 버튼도 한 덩어리로 처리합니다(같은 설정에 버튼마다 다르게 반응하지 않도록).
- **섞어 쓰기**: vanilla/glossy에 `--whole-fallback`을 붙이면 나누지 못한 버튼만 whole로 처리합니다. 보정 파일에서 버튼마다 `"whole": true`로 whole을 지정할 수도 있습니다.

아래 명령의 `--profile vanilla`를 팩에 맞게 `--profile glossy`로 바꿔 쓰면 됩니다. 분석 도구는 추출 결과 폴더면 프로필을 자동으로 알아보고, 팩 폴더면 `--profile glossy`를 붙여야 합니다.

---

## 1. 기본 개념

### 네 가지 종류

| 종류 | 뜻 | 예 |
|---|---|---|
| `outline` | 버튼 가장자리의 흰(밝은) 테두리 | 동그란 버튼의 흰 링 |
| `body` | 버튼의 색 바탕 판 | 초록 원, 십자 모양 판, 십자 모서리의 다른 색 칸 |
| `icon` | 바탕 위에 그려진 그림과 글자 | 재생 삼각형, 망치·스패너, 트로피, "FREE" 글자 |
| `keep` | 색을 바꾸지 않고 원본 그대로 둘 부분 | 꼭 원래 색이어야 하는 작은 장식 |

검은 선과 그림자는 어느 종류에도 속하지 않고 항상 원본 그대로입니다.

### 영역 번호

도구는 버튼을 **검은 선으로 막힌 밝은 덩어리(영역)** 단위로 나누고 1부터 번호를 붙입니다.
같은 그림이면 번호는 항상 같습니다. 보정은 "영역 5는 icon" 처럼 이 번호로 합니다.

> 팩 이미지를 수정하면 번호가 바뀔 수 있습니다. 그림을 고친 뒤에는 번호를 다시 확인하세요.

---

## 2. 준비

- Python 3.10 이상
- 라이브러리: `pip install numpy pillow scipy`
- 명령은 모두 `GD-Diamond-Theme` 폴더에서 실행합니다.
- 팩에 `.plist` 파일이 없으면(시트 이미지만 바꾼 팩) GD 기본 `Resources` 폴더 경로가 필요합니다.
  Steam 기본 위치: `C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash\Resources`

아래 예시에서 쓰는 이름:

| 이름 | 예 |
|---|---|
| 팩 폴더 | `C:\GD\geode\config\geode.texture-loader\packs\My Pack` |
| GD Resources | `C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash\Resources` |
| 분석 결과 폴더 | `inspect-out` (아무 폴더나) |
| 보정 파일 | `my-pack-overrides.json` |

---

## 3. 전체 흐름

### 1단계: 테마 데이터 만들기

```
python tools/make_theme_data.py "My Pack 폴더" "My Pack 폴더" --profile vanilla --base-plists "GD Resources"
```

팩 폴더 안에 `theme-data/`가 생깁니다. 이 팩을 Texture Loader로 켜면 모드가 자동으로 읽습니다.

### 2단계: 게임에서 확인

색 조정(예: 고급 설정 → Color 탭에서 바탕 색상 고정)을 크게 걸어 보면 잘못 나뉜 버튼이 바로 보입니다.

- 아이콘이 바탕 색으로 칠해짐 → 그 영역이 `body`로 잘못 판정됨
- 바탕 일부가 아이콘 색으로 칠해짐 → 그 영역이 `icon`으로 잘못 판정됨
- 버튼이 전혀 안 바뀜 → 프레임 전체가 테마 대상이 아님 (`themed`)

### 3단계: 프레임 이름 찾기

버튼 이름(프레임 이름)은 GD 시트의 이름입니다(예: `GJ_playBtn_001.png`). 모르면 목록을 봅니다.

```
python tools/inspect_frames.py "My Pack 폴더" inspect-out --list --base-plists "GD Resources"
python tools/inspect_frames.py "My Pack 폴더" inspect-out --list --match "garage|reward" --base-plists "GD Resources"
```

| frame | themed | why | regions (outline/body/icon/keep) |
|---|---|---|---|
| GJ_garageBtn_001.png | yes | themed | 1/8/4/0 |

`--match`를 빼면 버튼 이름인 프레임 전부를 보여 줍니다.

### 4단계: 버튼 분석

```
python tools/inspect_frames.py "My Pack 폴더" inspect-out --frames GJ_freeChestBtn_001.png --base-plists "GD Resources"
```

`inspect-out/GJ_freeChestBtn_001.png` 그림이 생깁니다.

- **왼쪽**: 원본
- **가운데**: 영역마다 다른 색 + 번호
- **오른쪽**: 결과 (빨강 = outline, 초록 = body, 파랑 = icon, 회색 = keep)

화면에는 같은 내용이 표로 나옵니다 (`.json` 파일에도 저장됩니다).

| id | class | auto | where | color | px | depth | edge | reason |
|---|---|---|---|---|---|---|---|---|
| 6 | body | body | top | blue #3482d9 | 2866 | 10.0 | 0.49 | body: main body (largest region at body depth) |
| 9 | icon | icon | top | yellow #f9bd0a | 1147 | 40.0 | 0.0 | icon: drawn on top of the body |
| 2 | body | body | bottom-left | yellow #f5dc1f | 1232 | 8.9 | 0.58 | body: edge piece (…) |

| 열 | 뜻 |
|---|---|
| `id` | 영역 번호 (보정할 때 쓰는 번호) |
| `class` | 지금 판정 (보정이 있으면 보정 후, **굵게** = 보정으로 바뀜) |
| `auto` | 자동 판정 |
| `where` | 버튼 안 위치 (top-left, center, bottom 등) |
| `color` | 영역 평균색 |
| `px` | 크기 (픽셀 수) |
| `depth` | 버튼 바깥에서 가장 가까운 거리. 바탕 조각들은 보통 비슷한 값이고, 테두리 쪽으로 튀어나온 그림은 더 작고, 안쪽 아이콘은 더 큼 |
| `edge` | 영역 둘레 중 버튼 가장자리에 닿는 비율 (0~1) |
| `reason` | 자동 판정 이유 |

### 5단계: 보정 파일 작성

예: 보물상자 버튼의 "FREE" 글자(영역 2, 8, 10, 13)가 바탕으로 잘못 판정됨 → 아이콘으로.

```json
{
  "frames": {
    "GJ_freeChestBtn_001.png": {
      "regions": { "2": "icon", "8": "icon", "10": "icon", "13": "icon" }
    }
  }
}
```

| 항목 | 쓰는 법 |
|---|---|
| `"regions": {"번호": "종류"}` | 종류는 `outline`, `body`, `icon`, `keep` |
| `"themed": false` | 이 프레임은 아예 건드리지 않음 (버튼이 아닌 그림 등) |
| `"themed": true` | 자동으로 제외된 프레임도 테마 대상으로 (이름 규칙에 안 맞는 버튼 등). 보통 `regions`와 함께 |

적지 않은 영역은 자동 판정을 그대로 씁니다.

### 6단계: 검증

```
python tools/inspect_frames.py "My Pack 폴더" inspect-out --frames GJ_freeChestBtn_001.png --overrides my-pack-overrides.json --base-plists "GD Resources"
```

- 바뀐 영역은 `class`가 **굵게** 표시되고 오른쪽 그림에 반영됩니다.
- 잘못된 내용은 `!`로 알려 줍니다.
  - `frame '…' does not exist in this pack` → 프레임 이름 오타
  - `region 99 does not exist (this frame has regions 1..16)` → 없는 번호
  - `unknown class 'iconn'` → 종류 이름 오타

### 7단계: 다시 만들고 게임에서 확인

```
python tools/make_theme_data.py "My Pack 폴더" "My Pack 폴더" --profile vanilla --base-plists "GD Resources" --overrides my-pack-overrides.json
```

게임이 켜져 있으면 보통 1초 안에 자동으로 다시 읽습니다. 반영이 안 되면 GD를 다시 시작하세요.

---

## 4. 판단 기준

헷갈릴 때는 **"이 부분이 버튼의 판(배경)인가, 판 위에 올린 그림인가?"** 로 판단합니다.

| 상황 | 정답 |
|---|---|
| 글자/그림이 테두리 쪽으로 삐져나옴 (FREE STUFF, MORE GAMES, 뉴그라운즈 탱크) | `icon` |
| 글자가 바탕을 둘로 잘라서 생긴 바탕 조각 | `body` |
| 십자 버튼 모서리의 다른 색 칸 | `body` |
| 아이콘 구멍 사이로 보이는 바탕 (톱니 가운데) | `body` |
| 바탕과 비슷한 색이지만 따로 그려진 그림 (Paths 버튼의 초록 보석) | `icon` |
| 아이콘 안의 작은 디테일 (큐브 눈, 입) | `icon` |
| 버튼 가장자리의 흰 링 | `outline` |
| 흰 링이 아닌데 `outline`이 된 흰 글자 | `icon` |
| 꼭 원래 색이어야 하는 부분 | `keep` |
| `px`가 12 미만인 점 | 건드리지 않음 |

수치 참고 (자동 판정이 쓰는 기준):
- 바탕 조각들은 `depth`가 서로 비슷합니다 (보통 ±3 이내). 같은 버튼의 주 바탕보다 `depth`가 확실히 작으면 테두리 위로 튀어나온 그림입니다.
- 바탕 조각은 `edge`가 큰 편입니다 (0.2 이상). `edge`가 0인 영역은 바탕에 둘러싸인 그림입니다(톱니 구멍 같은 예외 제외).

---

## 5. LLM으로 작업하기

[llm-tuning-instructions.md](llm-tuning-instructions.md)를 LLM에게 먼저 주고, 4단계의 그림과 표를 붙여 넣으면 보정 파일 항목을 만들어 줍니다.

- **그림을 볼 수 있는 모델**: 그림 + 표를 같이 주세요. 정확도가 가장 높습니다.
- **텍스트만 다루는 모델**: 표만으로도 할 수 있지만 정확도가 떨어집니다. `where`, `color`, `depth`, `edge`, `reason`을 보고 판단합니다.
- **과제 파일 한 번에 만들기**: 4단계 명령에 `--tasks`를 붙이면 출력 폴더에 버튼마다 `<버튼>.md`(안내 + 표)와 `<버튼>.png`가 생깁니다. 그대로 LLM에게 보내면 됩니다.
- **답 모으기**: 버튼마다 받은 답을 한 폴더에 저장한 뒤 `python tools/collect_answers.py <답 폴더> my-pack-overrides.json --check "My Pack 폴더"`를 실행하면 보정 파일 하나로 합쳐집니다. 없는 영역 번호나 오타는 빼고 알려 줍니다.
- 버튼 하나씩(또는 몇 개씩) 나눠서 주세요. 한 번에 너무 많이 주면 실수가 늘어납니다.
- LLM 결과는 **반드시 6단계로 검증**한 뒤 쓰세요. 도구가 번호/이름 오류를 잡아 줍니다.

---

## 6. 문제 해결

| 증상 | 원인 / 해결 |
|---|---|
| `frame not found` | 이름 오타 (대소문자 포함) |
| 분석은 되는데 게임에서 전혀 안 바뀜 | 테마 데이터는 GJ_GameSheet03/04, GJ_LaunchSheet, GJ_button_0N, GJ_square0N 이미지만 다룸. 다른 시트의 프레임은 대상이 아님 |
| `not themed: not a button name` | 이름 규칙상 버튼이 아니라고 봄 → 버튼이 맞으면 `"themed": true` |
| `not themed: no body found` | 바탕을 못 찾음 → `"themed": true` + 바탕 영역을 `body`로 지정 |
| `not themed: body too small (…)` | 바탕이 버튼의 15% 미만 → 맞는 판정이면 `"themed": true` |
| `not themed: only a body, no outline or icon` | 테두리도 아이콘도 없는 판 → 필요하면 `"themed": true` |
| 보정했는데 게임에 반영 안 됨 | 7단계를 다시 실행했는지 확인, GD 재시작 |
| 번호가 예전과 다름 | 팩 이미지가 바뀜 → 4단계부터 다시 |

**다른 프로필(diamond, generic)**: 영역 번호 보정은 지원하지 않습니다. 보정 파일의 목록 항목만 쓸 수 있습니다.
- `"themed": [...]`: 테마를 강제로 적용
- `"plain": [...]`: 테마를 강제로 끔
- `"icons": [...]`: 테두리에 붙은 아이콘을 떼어 냄
