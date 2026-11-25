# 버튼 분할 LLM 벤치마크 / Button-split LLM benchmark

[한국어](#한국어) · [English](#english)

---

## 한국어

LLM이 [llm-tuning-instructions.md](../../docs/llm-tuning-instructions.md) 지침만 보고 버튼 분할을 얼마나 잘 고치는지 점수로 재는 도구입니다.
모델 수준별로 돌려 보고, 지침을 고칠 때마다 점수가 오르는지 확인하는 데 씁니다.

### 들어 있는 것

| 파일 | 내용 |
|---|---|
| `truth.json` | 정답 세트: 바닐라 GD 버튼 32개, 영역 279개. 직접 그림을 보고 확인한 정답 |
| `tasks/fix/` | 보정 과제: 일부러 약한 기준 분할을 보여 주고 틀린 영역만 고치게 함 (실제 작업과 같은 형식) |
| `tasks/blind/` | 블라인드 과제: 판정 없이 모든 영역을 분류하게 함 (더 어려움) |
| `make_benchmark.py` | 과제 다시 만들기, 정답 초안 추가 |
| `score_benchmark.py` | 채점 |

> 공개 저장소에는 `tasks/` 그림이 없습니다(게임 원본 그림을 잘라 낸 것이라). 바닐라 GD 자료를 `preview/assets/vanilla-gd`에 만든 뒤
> `python tools/benchmark/make_benchmark.py`로 직접 만드세요.

- **기준 분할(baseline)**: 흰 테두리 = outline, 가장자리에 닿는 가장 큰 영역 하나 = body, 나머지 전부 icon.
  답을 하나도 안 줘도 77.8%(217/279)가 맞고, 고쳐야 할 영역이 62개입니다.
- **채점 대상**: 40 px 이상인 영역만입니다.
- **애매한 영역**: 두 답 모두 정답으로 칩니다(예: 보물상자의 주황 테두리 장식은 body/icon 둘 다 인정).
- **참고 점수** (보정 과제): 추출기 v0.12.3 보정 파일 없이 95.7% / `overrides/vanilla-gd.json` 포함 100%, Gemini Flash 3.8 99.3% (2026-10-21, `answers/gemini-flash-3.8`).

### 모델 하나 테스트하기

1. 새 대화를 열고 `docs/llm-tuning-instructions.md` 전체를 첫 메시지로 보냅니다.
2. `tasks/fix/<버튼>.md` 내용을 보냅니다. 이미지를 볼 수 있는 모델이면 같은 이름의 `.png`도 첨부합니다.
   한 번에 1~4개씩 보내는 걸 권장합니다.
3. 모델의 답을 그대로 `answers/<모델 이름>/<버튼>.md`에 붙여 넣어 저장합니다. 파일 이름은 아무거나 괜찮습니다.
4. 채점합니다.

```
python tools/benchmark/score_benchmark.py answers/<모델 이름> --mode fix --details
```

블라인드 과제는 `tasks/blind/`를 쓰고 `--mode blind`로 채점합니다.

### 결과 읽기

```
region accuracy:      277/279 = 99.3%     영역 정확도
pixel-weighted:       99.6%               큰 영역에 더 무게를 둔 정확도 (화면에서 보이는 정도에 가까움)
buttons fully right:  30/32 = 93.8%       모든 영역이 맞은 버튼
baseline (no answer): 217/279 = 77.8%     아무것도 안 고쳤을 때
fixed 60/62 wrong regions, missed 2, broke 0 correct regions
```

- `fixed`: 틀린 영역을 맞게 고친 수
- `missed`: 틀린 영역을 못 고친 수
- `broke`: 맞던 영역을 틀리게 바꾼 수 (지침이 "확실하지 않으면 건드리지 말라"고 하므로 작을수록 좋음)
- `confusion`: 정답 → 답 종류별 개수 (`<- wrong` 줄이 실수 유형)
- `--details`: 버튼마다 틀린 영역, `--json report.json`: 결과 저장 (모델끼리 비교할 때)
- 답이 `{}`(고칠 것 없음)인 파일은 파일 이름으로 버튼을 알아봅니다 → 버튼마다 `<버튼 이름>.json`/`.md`로 저장하세요
- 모델 답이 틀렸다고 나와도 정답 세트가 틀렸을 수 있습니다. Gemini 결과로 정답 8개를 바로잡았습니다(아이콘 구멍으로 보이는 바탕)

### 권장 실험 방법

- 모델마다 같은 순서, 같은 묶음 크기로 보냅니다. 묶음마다 새 대화를 여는 쪽이 공정합니다.
- 소형 / 중형 / 상위 모델을 각각 돌려서 `region accuracy`와 `broke`를 표로 비교합니다.
- 자주 틀리는 유형(confusion)을 보고 지침에 예시나 규칙을 추가한 뒤 다시 돌립니다.

### 정답 세트 늘리기

```
python tools/benchmark/make_benchmark.py --init GJ_someBtn_001.png    # 지금 추출기 결과로 초안 추가
python tools/benchmark/make_benchmark.py                              # 과제 다시 만들기
```

초안은 추출기 결과일 뿐입니다. `inspect_frames.py`로 그림을 보고 `truth.json`의 `truth`를 직접 고치세요.
애매하면 목록으로(`["body", "icon"]`), 이유는 `note`에 적습니다.

> 과제 그림은 GD 기본 리소스에서 만든 것이므로 로컬 테스트용으로만 쓰세요.

---

## English

Measures how well an LLM fixes button splits using only [llm-tuning-instructions.md](../../docs/llm-tuning-instructions.md).
Run it for several model tiers, and re-run it whenever you change the instructions to see whether the score goes up.

### Contents

| File | What |
|---|---|
| `truth.json` | Answer set: 32 vanilla GD buttons, 279 regions, checked by hand against the images |
| `tasks/fix/` | Fix tasks: a deliberately weak baseline split; correct only the wrong regions (same format as real work) |
| `tasks/blind/` | Blind tasks: no current split; classify every region (harder) |
| `make_benchmark.py` | Rebuild tasks, add draft answers |
| `score_benchmark.py` | Scoring |

> The public repository does not include `tasks/` (they are crops of the game's own art). Build the vanilla GD data into
> `preview/assets/vanilla-gd`, then run `python tools/benchmark/make_benchmark.py`.

- **Baseline**: white ring = outline, the single largest region touching the edge = body, everything else = icon.
  With no answer at all it scores 77.8% (217/279), leaving 62 regions to fix.
- **Scored regions**: only regions of 40 px or more.
- **Ambiguous regions**: accept either answer (e.g. the orange trim on the treasure chests: body or icon).
- **Reference scores** (fix task): extractor v0.12.3 95.7% without / 100% with `overrides/vanilla-gd.json`, Gemini Flash 3.8 99.3% (2026-10-21, `answers/gemini-flash-3.8`).

### Testing one model

1. Open a new chat and send the whole of `docs/llm-tuning-instructions.md` as the first message.
2. Send the contents of `tasks/fix/<button>.md`. For vision models, attach the `.png` of the same name.
   1–4 buttons per message is recommended.
3. Paste each reply as-is into `answers/<model name>/<button>.md` (any file name works).
4. Score:

```
python tools/benchmark/score_benchmark.py answers/<model name> --mode fix --details
```

For blind tasks use `tasks/blind/` and score with `--mode blind`.

### Reading the result

- `region accuracy`: share of regions right
- `pixel-weighted`: weighted by region size (closer to how much looks wrong on screen)
- `buttons fully right`: buttons with every region right
- `fixed` / `missed` / `broke`: wrong regions fixed / wrong regions left wrong / right regions made wrong
  (the instructions say "if unsure, leave it", so `broke` should be low)
- `confusion`: counts of truth → answer (`<- wrong` lines are the mistake types)
- `--details` lists the wrong regions per button; `--json report.json` saves the result for comparing models
- A `{}` answer ("nothing to change") is matched to its button by file name → save one `<button name>.json`/`.md` per button
- A "wrong" answer can mean the answer set is wrong. The Gemini run corrected 8 truth regions (body showing through icon holes)

### Suggested protocol

- Send the same tasks in the same order and batch size to every model; a fresh chat per batch is fairest.
- Run a small, a mid-tier and a top model and compare `region accuracy` and `broke`.
- Look at the frequent mistake types (confusion), add examples or rules to the instructions, and re-run.

### Growing the answer set

```
python tools/benchmark/make_benchmark.py --init GJ_someBtn_001.png    # add a draft from the current extractor
python tools/benchmark/make_benchmark.py                              # rebuild the tasks
```

A draft is only the extractor's output. Check it against the image with `inspect_frames.py` and edit `truth` in
`truth.json` by hand. Use a list for ambiguous regions (`["body", "icon"]`) and explain in `note`.

> The task images are made from GD's own resources; use them for local testing only.
