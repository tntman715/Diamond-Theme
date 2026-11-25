#pragma once

#include "Theme.hpp"

#include <filesystem>
#include <string>
#include <vector>

// 테마 파일 (JSON). 미리보기 툴의 "테마 JSON"과 같은 형식 (schema 1).
// 모든 항목은 생략할 수 있고, 생략한 항목은 기준값(활성 팩의 원본 테마 또는 모드 설정)을 그대로 쓴다.
//
// {
//   "schema": 1, "name": "...", "author": "...",
//   "outline": { "source": "theme" | "original",
//                "hsv": { "start", "range", "saturation", "value" } | "stops": ["#..", "#..", "#.."] | "solid": "#..",
//                "angle", "flow", "spin" },
//   "fill": "original" | "#rrggbb" | { "gradient": ["#..", "#.."], "angle": 90 } | { "theme": 0..1 },
//   "surface": { "opacity", "fillOpacity", "fillOpacityScope": "icons" | "all", "bevel", "bevelSize",
//                "iconBevel", "iconBevelSize", "innerShadow", "innerShadowSize" },
//   "inverted": { "outline", "icon", "fill", "text": true | false }   (뒷배경을 반전해서 보여줄 부분),
//   "glass": { "amount": 0..1, "blur": 0..1, "refraction": 0..1, "rim": 0..1, "edge": 픽셀, "dispersion": 0..1,
//              "scope": "buttons" | "icons" | "all", "listFrames": true | false },
//   "accent": "#rrggbb",
//   "glyph": "auto" | "accent" | "gradient" | "original" | "#rrggbb",
//   "whiteOutline": "accent" | "gradient",
//   "fonts": { "bigFont": "original" | "gradient" | "#rrggbb", "goldFont": ... },
//   "animation": { "press": "gd" | "squash" | "pop" | "invert" | "none", "hoverGlow": 0..1, "idle": "none" | "pulse" | "shimmer" },
//   "lists": { "background": "original" | "dark" | ["#어두운 칸", "#밝은 칸"], "opacity": 0..1, "glass": 0..1 },
//   "active": { "outline": "same" | "#rrggbb" | ["#..", "#.."], "fill": "same" | "#rrggbb" },
//   "geodeButtons": true | false,
//   "packs": { "<팩 id>": { 위와 같은 항목 — 그 팩이 켜져 있을 때만 덮어쓴다 } }
// }
namespace theme::file {

// 설정의 기본 테마 폴더 (<config>/themes)
std::filesystem::path themesDir();

// 처음 실행 때 테마 폴더를 만들고 예제 파일을 넣는다 (이미 있는 파일은 건드리지 않는다)
void installExamples();

// json을 s 위에 덮어쓴다. packId가 "packs"에 있으면 그 내용도 이어서 덮어쓴다.
// 알 수 없는 값은 errors에 모으고 그 항목만 건너뛴다.
void apply(matjson::Value const& json, std::string const& packId, Settings& s, std::vector<std::string>& errors);

// Geode가 저장한 모드 설정 파일(settings.json) 형식인지: 테마 파일 대신 이걸 골라도 동작하게 한다
bool isSettingsExport(matjson::Value const& json);

// 지금 적용 중인 모습을 테마 파일 형식으로
matjson::Value toJson(Settings const& s, std::string const& name);

// 테마 폴더에 새 파일로 저장하고 경로를 돌려준다 (my-look-<날짜시각>.json)
geode::Result<std::filesystem::path> saveCurrent(Settings const& s);

}
