#include "Theme.hpp"
#include "Advanced.hpp"
#include "Glass.hpp"
#include "ThemeFile.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <unordered_set>

using namespace geode::prelude;

namespace theme {

namespace {

constexpr char const* PROGRAM_KEY = "tntman.diamond-theme/theme";
constexpr unsigned REFRESH_FRAMES = 60;    // 실제 로드된 시트 텍스쳐를 다시 확인하는 주기
constexpr unsigned SETTINGS_FRAMES = 15;   // 설정을 다시 읽는 주기

// cocos2d-x가 CC_MVPMatrix 등의 uniform 선언을 셰이더 앞에 자동으로 붙인다.
// 데스크톱 GLSL 1.10 호환: 정수 리터럴 대신 실수 리터럴, 정밀도 한정자 없음.
constexpr char const* VERTEX_SHADER = R"(
attribute vec4 a_position;
attribute vec2 a_texCoord;
attribute vec4 a_color;
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
varying vec2 v_pos;
void main() {
    gl_Position = CC_MVPMatrix * a_position;
    v_fragmentColor = a_color;
    v_texCoord = a_texCoord;
    v_pos = a_position.xy;
}
)";

// preview/app.js의 프래그먼트 셰이더를 옮긴 것. 차이점:
// - 노드 안 좌표(loc): 개별 스프라이트는 프레임 영역 + 트림 정보로, 배치 노드(9분할/라벨)는 정점 위치 범위로 계산
// - 라벨 모드: 마스크 없이 글자 밝기(최대 채널)에 목표 색을 곱한다 (검은 외곽선/음영은 유지)
// - 스프라이트 색/투명도(v_fragmentColor)를 마지막에 곱한다
constexpr char const* FRAGMENT_SHADER = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
varying vec2 v_pos;
uniform sampler2D CC_Texture0;
uniform sampler2D u_mask;
uniform sampler2D u_sdf;
uniform sampler2D u_bg;
uniform sampler2D u_bgSharp;

uniform vec4 u_rect;
uniform vec4 u_trim;
uniform float u_rotated;
uniform vec3 u_style;
uniform float u_posMode;
uniform vec4 u_bounds;
uniform float u_text;
uniform float u_textMode;
uniform vec3 u_textColor;
uniform float u_glow;
uniform float u_shine;
uniform float u_phase;
uniform float u_noMask;
uniform float u_invert;
uniform float u_vanilla;
uniform float u_packVanilla;
uniform float u_plain;
uniform float u_active;
uniform float u_activeMode;
uniform float u_activeFill;
uniform vec3 u_a1;
uniform vec3 u_a2;
uniform vec3 u_aFill;
uniform float u_hasSdf;
uniform vec2 u_texel;

uniform float u_time;
uniform float u_gradMode;
uniform float u_hueStart;
uniform float u_hueRange;
uniform float u_sat;
uniform float u_val;
uniform float u_angle;
uniform float u_flow;
uniform float u_flowBody;   // 원본 그림 바탕도 흐름을 받는지
uniform float u_spin;
uniform float u_use3;
uniform vec3 u_c1;
uniform vec3 u_c2;
uniform vec3 u_c3;
uniform vec3 u_fill;
uniform vec3 u_accent;
uniform vec3 u_glyphColor;
uniform float u_glyphMode;
uniform float u_whiteOutline;
uniform float u_fillMode;
uniform vec3 u_fill2;
uniform float u_fillAngle;
uniform float u_fillTheme;
uniform float u_outlineSrc;
uniform float u_opacity;
uniform float u_fillOpacity;
uniform float u_bevel;
uniform float u_bevelSize;
uniform float u_innerShadow;
uniform float u_innerShadowSize;
uniform float u_iconBevel;
uniform float u_iconBevelSize;
uniform float u_fillOpIcons;
uniform float u_glassOn;
uniform vec4 u_vp;
uniform float u_glass;
uniform float u_glassRefract;
uniform float u_glassRim;
uniform float u_glassEdge;
uniform float u_glassDispersion;
uniform float u_frost;
uniform vec3 u_light;    // xy = 마우스 위치 (화면 비율 0..1, y-up), z = 1이면 마우스 쪽에서 빛이 온다
uniform float u_spec;    // 반사광 세기
uniform float u_glassSat;     // 비치는 배경의 채도
uniform float u_glassNoise;   // 노이즈 입자
uniform float u_squish;  // 쫀득한 누름으로 찌그러진 정도 → 굴절/빛을 키운다
uniform float u_gray;
uniform float u_pass;
uniform float u_invOutline;
uniform float u_invIcon;
uniform float u_invFill;
uniform float u_invText;
uniform float u_adjOn;
uniform vec4 u_adjKeep;
uniform vec4 u_adjOutline;
uniform vec4 u_adjFill;
uniform vec4 u_adjIcon;
uniform float u_rangeOn;
uniform float u_colorizeGray;
uniform float u_whole;
uniform float u_packWhole;
uniform float u_hueAnim;
uniform float u_fxHue;
uniform float u_activeAdjOn;
uniform vec4 u_activeAdj;
uniform vec3 u_range[6];

// 스프라이트 좌표(y-down)의 벡터 ↔ 시트 UV 방향. 회전 저장된 프레임은 시계 방향 90°로 들어 있다.
vec2 spriteToUV(vec2 v) {
    return u_rotated > 0.5 ? vec2(-v.y, v.x) : v;
}
vec2 uvToSprite(vec2 v) {
    return u_rotated > 0.5 ? vec2(v.y, -v.x) : v;
}

// 거리장: 배율은 tools/make_sdf.py의 2 * RANGE와 같아야 한다. 안쪽이 +, 단위는 UHD 픽셀
// x = 몸통(테두리+바탕+아이콘) 가장자리까지, y = 아이콘 가장자리까지
vec2 sdf2(vec2 p) {
    vec4 t = texture2D(u_sdf, clamp(p, u_rect.xy, u_rect.zw));
    return (vec2(t.r, t.a) - 0.5) * 128.0;
}
float sdfAt(vec2 p) {
    return sdf2(p).x;
}

// 거리장 기울기 (스프라이트 좌표, 안쪽을 향함). 바로 옆 텍셀 차이만 쓰면 거리장 계단이 보여 쭈글쭈글해지므로
// 효과 크기에 비례한 간격의 3x3(소벨) 평균으로 부드럽게 잡는다. xy = 몸통, zw = 아이콘
vec4 sdfSlope(vec2 uv, float size) {
    vec2 e = u_texel * max(2.0, size * 0.2);
    vec2 tl = sdf2(uv + vec2(-e.x, -e.y)), tt = sdf2(uv + vec2(0.0, -e.y)), tr = sdf2(uv + vec2(e.x, -e.y));
    vec2 ll = sdf2(uv + vec2(-e.x, 0.0)), rr = sdf2(uv + vec2(e.x, 0.0));
    vec2 bl = sdf2(uv + vec2(-e.x, e.y)), bb = sdf2(uv + vec2(0.0, e.y)), br = sdf2(uv + vec2(e.x, e.y));
    vec2 gx = (tr + 2.0 * rr + br) - (tl + 2.0 * ll + bl);
    vec2 gy = (bl + 2.0 * bb + br) - (tl + 2.0 * tt + tr);
    return vec4(uvToSprite(vec2(gx.x, gy.x)), uvToSprite(vec2(gx.y, gy.y)));
}

// 베벨 음영: 가장자리에서 size 안쪽까지, 바깥을 향한 면이 빛(왼쪽 위)을 보면 +, 반대면 -
float bevelShade(vec2 slope, float d, float size) {
    float len = length(slope);
    if (len < 0.0001) return 0.0;
    float facing = dot(-slope / len, vec2(-0.7071, -0.7071));
    return facing * (1.0 - smoothstep(0.0, max(size, 1.0), d));
}

// 유리 배경: 블러 전 원본과 블러본을 Glass Blur 비율로 섞는다 (0 = 선명한 배경 → 굴절/색수차가 또렷하게 보인다)
vec3 glassBg(vec2 p) {
    p = clamp(p, 0.0, 1.0);
    return mix(texture2D(u_bgSharp, p).rgb, texture2D(u_bg, p).rgb, u_frost);
}

vec3 applyShade(vec3 col, float shade) {
    return shade > 0.0 ? mix(col, vec3(1.0), shade * 0.7) : mix(col, vec3(0.0), -shade * 0.6);
}

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + 1e-10)), d / (q.x + 1e-10), q.x);
}

vec3 hueShift(vec3 c, float turns) {
    vec3 h = rgb2hsv(c);
    h.x = fract(h.x + turns);
    return hsv2rgb(h);
}

// 색 조정: k = (색상(바퀴), 채도 배율, 밝기 배율, 1이면 색상 고정 / 0이면 회전)
// 색상 범위 조정을 먼저: 빨강~마젠타 6개 범위(60° 간격)를 원래 색상으로 고른다. 범위 사이는 부드럽게 섞이고
// (가중치 합 = 1), 회색에 가까운 색은 색상이 없으므로 영향을 덜 받는다
vec3 adjustHsv(vec3 h, vec4 k);
vec3 adjustCol(vec3 c, vec4 k) {
    vec3 h = rgb2hsv(c);
    if (u_rangeOn > 0.5) {
        vec3 r = vec3(0.0);
        for (int i = 0; i < 6; i++) {
            float d = abs(h.x - float(i) / 6.0);
            d = min(d, 1.0 - d);
            r += (1.0 - smoothstep(0.041667, 0.125, d)) * (u_range[i] - vec3(0.0, 1.0, 1.0));
        }
        r *= smoothstep(0.02, 0.15, h.y);
        h = vec3(h.x + r.x, clamp(h.y * (1.0 + r.y), 0.0, 1.0), clamp(h.z * (1.0 + r.z), 0.0, 1.0));
    }
    return adjustHsv(h, k);
}

// 색상 회전/고정 + 채도/밝기 (색상 범위 단계 없이)
vec3 adjustHsv(vec3 h, vec4 k) {
    if (k.w > 0.5) {
        // 색상 고정 (포토샵 색상화): 밝기(HSL 명도)는 그대로 두고 색상만 바꾼다. 무채색(은색 트로피, 회색 탭)은
        // 원래 채도가 0이라 그대로면 색이 안 들어가므로 u_colorizeGray만큼 채도를 올린다.
        // 명도 기준이라 흰색/검은색은 채도를 올려도 흰색/검은색 그대로다
        vec3 c2 = hsv2rgb(h);
        float mx = max(max(c2.r, c2.g), c2.b), mn = min(min(c2.r, c2.g), c2.b);
        float l = (mx + mn) * 0.5;
        float span = 1.0 - abs(2.0 * l - 1.0);
        float s = span > 0.0001 ? (mx - mn) / span : 0.0;
        s = clamp(max(s, u_colorizeGray) * k.y, 0.0, 1.0);
        vec3 pure = hsv2rgb(vec3(fract(k.x), 1.0, 1.0));
        return clamp((l + span * s * (pure - 0.5)) * k.z, 0.0, 1.0);
    }
    return hsv2rgb(vec3(fract(h.x + k.x), clamp(h.y * k.y, 0.0, 1.0), clamp(h.z * k.z, 0.0, 1.0)));
}

vec3 gradAt(vec2 loc) {
    float a = radians(u_angle + u_time * u_spin * 360.0);
    vec2 d = vec2(cos(a), sin(a));
    float u = dot(loc - 0.5, d) / (abs(d.x) + abs(d.y)) + 0.5;
    if (u_gradMode < 0.5) {
        float h = (u_hueStart + u_hueRange * u) / 360.0 + u_time * u_flow;
        return hsv2rgb(vec3(fract(h), u_sat, u_val));
    }
    if (u_gradMode < 1.5) {
        float p = 1.0 - abs(1.0 - fract((u + u_time * u_flow) * 0.5) * 2.0);
        if (u_use3 > 0.5) {
            return p < 0.5 ? mix(u_c1, u_c2, p * 2.0) : mix(u_c2, u_c3, p * 2.0 - 1.0);
        }
        return mix(u_c1, u_c2, p);
    }
    return u_c1;
}

// Diamond Vortex 아이콘의 원본 색: 네 모서리 색(왼쪽 위 분홍, 오른쪽 위 노랑, 왼쪽 아래 보라, 오른쪽 아래 초록)을
// 섞은 것 (팩 아이콘에서 잰 값). 기본 스타일 아이콘(Geode 로고)을 볼텍스 아이콘처럼 칠할 때 쓴다.
vec3 cornerGrad(vec2 loc) {
    vec3 tl = hsv2rgb(vec3(0.99, 0.48, 0.94));
    vec3 tr = hsv2rgb(vec3(0.165, 0.50, 0.91));
    vec3 bl = hsv2rgb(vec3(0.73, 0.43, 0.86));
    vec3 br = hsv2rgb(vec3(0.415, 0.49, 0.85));
    vec2 p = clamp(loc, 0.0, 1.0);
    return mix(mix(tl, tr, p.x), mix(bl, br, p.x), p.y);
}

// 켜진 버튼 색: 1 단색, 2 두 색 그라데이션 (방향/흐름/회전은 테마와 같이)
vec3 activeAt(vec2 loc) {
    if (u_activeMode < 1.5) return u_a1;
    float a = radians(u_angle + u_time * u_spin * 360.0);
    vec2 d = vec2(cos(a), sin(a));
    float u = dot(loc - 0.5, d) / (abs(d.x) + abs(d.y)) + 0.5;
    float p = 1.0 - abs(1.0 - fract((u + u_time * u_flow) * 0.5) * 2.0);
    return mix(u_a1, u_a2, p);
}
)"
// MSVC는 문자열 리터럴 하나의 길이를 약 16KB로 제한한다 → 두 조각으로 나눠 이어 붙인다
R"(
void main() {
    vec2 uv = clamp(v_texCoord, u_rect.xy, u_rect.zw);
    vec4 base = texture2D(CC_Texture0, uv);
    float a = base.a;
    if (a < 0.002) {
        gl_FragColor = base * v_fragmentColor;
        return;
    }

    vec2 loc;
    if (u_posMode > 0.5) {
        // 노드 공간 정점 위치 → 0..1 (y-down)
        loc = vec2((v_pos.x - u_bounds.x) / max(u_bounds.z, 0.001), 1.0 - (v_pos.y - u_bounds.y) / max(u_bounds.w, 0.001));
    } else {
        vec2 t = clamp((uv - u_rect.xy) / max(u_rect.zw - u_rect.xy, vec2(0.000001)), 0.0, 1.0);
        vec2 lt = u_rotated > 0.5 ? vec2(t.y, 1.0 - t.x) : t;
        loc = u_trim.xy + lt * u_trim.zw;
    }

    vec3 straight = base.rgb / a;
    if (u_text > 0.5) {
        float lum = max(max(straight.r, straight.g), straight.b);
        // 3 = 원래 색 그대로 (배경 반전만 쓰는 라벨)
        vec3 tc = u_textMode > 2.5 ? straight
                : clamp((u_textMode < 1.5 ? gradAt(loc) : u_textColor) * lum, 0.0, 1.0);
        tc = mix(tc, 1.0 - tc, u_invert);
        // 배경 반전: 글자 안쪽(밝은 부분)만. 검은 외곽선은 그대로 둬서 읽히게
        float w = u_invText * lum;
        if (u_pass > 1.5) {
            gl_FragColor = vec4(vec3(w * a), w * a) * v_fragmentColor;
            return;
        }
        float ta = u_pass > 0.5 ? a * (1.0 - w) : a;
        gl_FragColor = vec4(tc * ta, ta) * v_fragmentColor;
        return;
    }
    // 마스크 없이 효과만 거는 경우(테마 대상이 아닌 텍스쳐): 전부 원본 유지
    vec3 m = u_noMask > 0.5 ? vec3(0.0) : clamp(texture2D(u_mask, uv).rgb / a, 0.0, 1.0);
    // 기본 스타일(Geode) 버튼을 팩 스타일로 맞춰 칠할지. 바닐라 구조 팩이 켜져 있으면 Geode 버튼도 이미 같은
    // 스타일이므로 일반 프레임처럼 다룬다 ("원본"이면 원래 모습 그대로)
    bool vanilla = u_vanilla > 0.5 && u_packVanilla < 0.5;
    bool plainVanilla = vanilla && u_plain > 0.5;
    // 윤곽선 없는 팩(볼텍스): 기본 스타일 버튼의 흰 테두리를 바탕에 합친다
    if (plainVanilla) {
        m.g += m.r;
        m.r = 0.0;
    }
    // 분할 없는 프레임, 또는 분할 없는 팩(whole)이 켜져 있을 때의 모든 버튼(Geode 버튼 포함):
    // 부위를 나누지 않고 전부 원래 색의 바탕 하나로 → 같은 팩 안 모든 버튼이 같은 효과를 받는다
    bool wholeLike = u_whole > 0.5 || u_packWhole > 0.5;
    if (wholeLike) m = vec3(0.0, m.r + m.g + m.b, 0.0);
    float keep = clamp(1.0 - m.r - m.g - m.b, 0.0, 1.0);
    bool active = u_active > 0.5;
    bool activeLine = active && u_activeMode > 0.5;
    vec3 grad = activeLine ? activeAt(loc) : gradAt(loc);

    // u_style.z < 0: 바탕이 단색이 아니다 (바닐라 구조 팩) → 원본 색 = 픽셀 그대로
    bool textured = u_style.z < 0.0;
    vec3 nonFill = textured ? straight : clamp((straight - m.g * vec3(u_style.z)) / max(1.0 - m.g, 0.05), 0.0, 1.0);
    nonFill = hueShift(nonFill, u_time * u_flow);

    // 기본 스타일 스프라이트는 원본 색이 GD 기본색이므로 "원본 색" 모드에서도 테마 그라데이션을 쓴다
    float outlineSrc = vanilla ? 0.0 : u_outlineSrc;
    float glyphMode = (vanilla && u_glyphMode > 3.5) ? 2.0 : u_glyphMode;
    // 바탕이 원본 그림인 팩(바닐라 구조)에서 "자동" 아이콘 색은 원본 색: 아이콘이 흰색/회색/컬러로 제각각이라
    // 컬러 아이콘만 테마 그라데이션으로 바뀌고 흰/회색 아이콘은 그대로 남아 버튼마다 들쭉날쭉해진다.
    // 색을 바꾸고 싶으면 색 조정(색조/채도/밝기)이 모든 아이콘에 똑같이 걸린다
    if (textured && !vanilla && glyphMode < 0.5) glyphMode = 4.0;

    vec3 outlineCol = outlineSrc > 0.5 ? nonFill
                    : ((u_style.x > 1.5 && u_whiteOutline < 0.5) ? u_accent : grad);
    vec3 glyphCol = glyphMode < 0.5 ? (u_style.y > 1.5 ? u_accent : grad)
                  : (glyphMode < 1.5 ? u_accent
                  : (glyphMode < 2.5 ? grad
                  : (glyphMode < 3.5 ? u_glyphColor : nonFill)));
    // 볼텍스 "원본 색"에서 기본 스타일 아이콘은 볼텍스 아이콘 색으로 (원본 흐름이면 같이 흐른다)
    if (plainVanilla && u_glyphMode > 3.5 && !activeLine) glyphCol = hueShift(cornerGrad(loc), u_time * u_flow);
    // 켜진 버튼: 색이 있는 부분(흰색이 아닌 테두리/아이콘)은 원본 색 모드여도 활성 색으로.
    // 바닐라 구조 팩은 아이콘이 흰색/회색/컬러로 제각각이라(아이콘 키트 탭 등) 스타일과 상관없이 아이콘 전체를 활성 색으로
    if (activeLine) {
        if (u_style.x < 1.5) outlineCol = grad;
        if ((u_style.y < 1.5 || u_packVanilla > 0.5) && glyphMode > 3.5) glyphCol = grad;
    }
    // 색 조정 (위에서 정한 색 기준). 켜진 버튼 색은 사용자가 직접 고른 색이라 그대로 둔다.
    // 색조 애니메이션(시간)과 호버 색조는 모든 부위의 색상 값에 더해진다
    vec4 hueOff = vec4(u_hueAnim + u_fxHue, 0.0, 0.0, 0.0);
    bool adj = (u_adjOn > 0.5 || abs(hueOff.x) > 0.00001) && u_noMask < 0.5;
    if (adj && !activeLine) {
        if (m.r > 0.0) outlineCol = adjustCol(outlineCol, u_adjOutline + hueOff);
        if (m.b > 0.0) glyphCol = adjustCol(glyphCol, u_adjIcon + hueOff);
    }
    // 켜진 버튼의 바탕 (Diamond Vortex처럼 테두리가 없는 팩은 이걸로 켜짐 상태가 보인다)
    vec3 fillCol;
    // 분할 없는 프레임(whole): 바탕 = 그림 전체이므로 바탕색 설정 대신 항상 원래 그림 (색 조정/효과는 그대로)
    if (wholeLike) fillCol = straight;
    else if (active && u_activeFill > 0.5) fillCol = u_aFill;
    // (원본 흐름: 원본 그림 바탕도 아이콘과 같이 색상이 흐른다)
    else if (u_fillMode < 0.5) fillCol = textured ? hueShift(straight, u_time * u_flow * u_flowBody) : vec3(u_style.z);
    else if (u_fillMode < 1.5) fillCol = u_fill;
    else if (u_fillMode < 2.5) {
        // 두 색 그라데이션 (테두리와 별개의 방향)
        float fa = radians(u_fillAngle);
        vec2 fd = vec2(cos(fa), sin(fa));
        float fu = clamp(dot(loc - 0.5, fd) / (abs(fd.x) + abs(fd.y)) + 0.5, 0.0, 1.0);
        fillCol = mix(u_fill, u_fill2, fu);
    }
    else fillCol = grad * u_fillTheme;   // 테두리 그라데이션을 어둡게 깐 바탕
    if (adj && m.g > 0.0 && !(active && u_activeFill > 0.5)) fillCol = adjustCol(fillCol, u_adjFill + hueOff);

    // 유리: 바탕 자리에 버튼 뒤 배경(블러)을 깐다. 가장자리에서는 배경이 렌즈처럼 휘고(굴절) 테두리 빛이 돈다
    if (u_glassOn > 0.5 && m.g > 0.0) {
        vec2 suv = (gl_FragCoord.xy - u_vp.xy) / u_vp.zw;
        vec2 off = vec2(0.0);
        vec2 disp = vec2(0.0);   // 색수차: 빨강/파랑을 굴절 방향으로 서로 반대로 더/덜 꺾는다
        float rim = 0.0;
        float spec = 0.0;
        // 빛 방향 (화면 y-up): 마우스가 있으면 이 픽셀에서 마우스 쪽, 없으면 왼쪽 위.
        // 배경이 멈춰 있어도 마우스를 움직이면 빛이 따라 돌아서 "계산된 유리"인 게 보인다
        vec2 toLight = vec2(-0.7071, 0.7071);
        vec2 dpx = vec2(0.0);
        float near = 0.6;
        if (u_light.z > 0.5) {
            dpx = (u_light.xy - suv) * u_vp.zw;
            float dl = length(dpx);
            if (dl > 1.0) toLight = dpx / dl;
            near = 0.45 + 0.55 * exp(-dl / (0.35 * u_vp.w));
        }
        float wobble = 1.0 + u_squish;
        if (u_hasSdf > 0.5) {
            float edge = max(u_glassEdge, 1.0);
            vec2 sl = sdfSlope(uv, edge).xy;
            float len = length(sl);
            float k = 1.0 - smoothstep(0.0, edge, sdf2(uv).x);
            if (len > 0.0001) {
                vec2 n = sl / len;   // 안쪽 방향 (스프라이트 좌표, y-down)
                // 화면 좌표(y-up, 화면 픽셀 단위)로 바꿔 바깥쪽 배경을 끌어온다
                vec2 nd = vec2(-n.x, n.y) / u_vp.zw;
                off = nd * u_glassRefract * 60.0 * k * sqrt(k) * wobble;
                // 색수차는 가장자리 폭 전체에 걸쳐 넓게 (바탕은 테두리 안쪽부터라 제곱으로 줄이면 거의 안 보인다)
                disp = nd * u_glassDispersion * 36.0 * k * wobble;
                // 바깥 방향(화면 y-up)이 빛을 볼수록 밝게
                float facing = dot(vec2(-n.x, n.y), toLight);
                rim = k * k * (0.35 + 0.65 * max(facing, 0.0)) * u_glassRim * wobble;
                // 반사광: 빛을 정면으로 보는 가장자리에만 좁고 밝게 (마우스가 가까울수록 진하게)
                spec = pow(max(facing, 0.0), 6.0) * k * u_spec * near * 0.9 * wobble;
            }
        }
        vec3 bg;
        if (u_glassDispersion > 0.0) {
            bg.r = glassBg(suv + off + disp).r;
            bg.g = glassBg(suv + off).g;
            bg.b = glassBg(suv + off - disp).b;
        }
        else bg = glassBg(suv + off);
        bg = mix(vec3(dot(bg, vec3(0.299, 0.587, 0.114))), bg, u_glassSat);
        // 마우스 바로 아래 유리면이 은은하게 밝아진다 (손전등처럼)
        float sheen = u_light.z > 0.5 ? u_spec * 0.14 * exp(-dot(dpx, dpx) / (2.0 * pow(0.1 * u_vp.w, 2.0))) : 0.0;
        fillCol = mix(bg, fillCol, 1.0 - u_glass) + rim + spec + sheen;
        // 젖빛 유리 노이즈: 화면 픽셀에 고정된 아주 고운 입자 (움직이지 않는다)
        if (u_glassNoise > 0.0) {
            float n = fract(52.9829189 * fract(dot(floor(gl_FragCoord.xy), vec2(0.06711056, 0.00583715)))) - 0.5;
            fillCol += n * u_glassNoise * 0.12;
        }
    }

    // 원본 유지 부분(테마가 나누지 못한 음영/장식 등)은 "전체" 조정만 받는다
    vec3 keepCol = (adj && keep > 0.0) ? adjustCol(straight, u_adjKeep + hueOff) : straight;
    vec3 col = m.r * outlineCol + m.g * fillCol + m.b * glyphCol + keep * keepCol;
    // 켜진 버튼의 색조 변화: 완성된 그림 전체에 (분할 없는 팩도 켜짐 상태가 보이게)
    if (active && u_activeAdjOn > 0.5 && u_noMask < 0.5) col = adjustHsv(rgb2hsv(clamp(col, 0.0, 1.0)), u_activeAdj);
    // 윤곽선 없는 팩에서는 테두리로 상태를 보여 줄 수 없으므로 비활성 느낌 베이스는 전체를 어둡게
    if (plainVanilla && u_vanilla > 1.5) col *= 0.55;

    // 표면 효과: 몸통(테두리+바탕+아이콘) 가장자리 거리로 계산. 그림자 같은 원본 유지 영역은 건드리지 않는다
    float body = m.r + m.g + m.b;
    if (u_hasSdf > 0.5 && body > 0.0 && (u_bevel > 0.0 || u_innerShadow > 0.0 || u_iconBevel > 0.0)) {
        vec2 light = vec2(-0.7071, -0.7071);   // 왼쪽 위에서 오는 빛 (y-down)
        vec2 dd = sdf2(uv);
        float d = dd.x;
        // 베벨: 가장자리에서 안쪽으로 비스듬히 깎인 면. 바깥을 향하는 면이 빛을 보면 밝게, 반대면 어둡게
        if (u_bevel > 0.0 && m.r + m.g > 0.0) {
            vec4 sl = sdfSlope(uv, u_bevelSize);
            col = applyShade(col, bevelShade(sl.xy, d, u_bevelSize) * u_bevel * (m.r + m.g));
        }
        // 아이콘 엠보싱: 아이콘 자체의 가장자리 기준 (바탕 위로 도드라져 보이게)
        if (u_iconBevel > 0.0 && m.b > 0.0) {
            vec4 sl = sdfSlope(uv, u_iconBevelSize);
            col = applyShade(col, bevelShade(sl.zw, dd.y, u_iconBevelSize) * u_iconBevel * m.b);
        }
        if (u_innerShadow > 0.0) {
            // 안쪽 그림자: 빛 쪽 가장자리가 안으로 드리우는 그림자 (빛 쪽으로 옮긴 점이 가장자리에 가까울수록 진하게)
            vec2 off = spriteToUV(light * u_innerShadowSize * 0.5) * u_texel;
            float ds = sdfAt(uv + off);
            float sh = (1.0 - smoothstep(0.0, max(u_innerShadowSize * 0.5, 1.0), ds)) * u_innerShadow * body;
            col *= 1.0 - sh * 0.75;
        }
    }

    // 빛남 (호버/누름/펄스): 테두리를 자기 색으로 밝게, 글리프는 조금, 테마 영역 전체는 아주 살짝
    col += u_glow * ((m.r + 0.35 * m.b) * outlineCol * 0.9 + 0.06 * (m.r + m.g + m.b));
    // 반짝임: 대각선 빛 띠가 버튼마다 다른 박자로 지나간다 (테두리/글리프에만)
    if (u_shine > 0.0) {
        float s = fract(u_time * 0.35 + u_phase) * 1.6 - 0.3;
        float d = (loc.x + loc.y) * 0.5 - s;
        col += exp(-d * d * 120.0) * u_shine * (m.r + m.b) * 0.6;
    }
    // 잠긴 버튼(GD의 흑백 스프라이트): 테마/효과를 다 입힌 뒤 흑백으로 (잠김 표시는 유지)
    col = mix(col, vec3(dot(col, vec3(0.299, 0.587, 0.114))), u_gray);
    // 누름 색 반전
    col = clamp(col, 0.0, 1.0);
    col = mix(col, 1.0 - col, u_invert);
    // 투명도: 바탕만 따로, 그다음 버튼 전체
    // (효과만 거는 테마 대상 아닌 그림은 제외)
    // 바탕 투명도 범위가 "아이콘 있는 버튼만"이면 아이콘 없는 버튼(9분할 배경, 체크박스 등)은 바탕을 그대로 둔다
    // 유리 바탕은 배경을 직접 깔았으므로 불투명으로 그린다
    // (분할 없는 버튼은 아이콘이 따로 없으므로 "아이콘 있는 버튼만" 범위에서는 바탕 투명도를 받지 않는다)
    float fillOp = (u_glassOn > 0.5 || (u_fillOpIcons > 0.5 && (u_style.y < 0.5 || wholeLike))) ? 1.0 : u_fillOpacity;
    // 버튼 그림자(원본 유지 영역 중 어둡고 반투명한 픽셀)는 바탕과 함께 흐려진다: 바탕이 사라졌는데 그림자만 남지 않게
    // Geode 베이스(기본 스타일, 테두리 있는 프레임)는 원본 유지 영역이 전부 그림자다 (geode_masks.py가 그렇게 나눈다).
    // 원 가장자리와 겹친 그림자 픽셀은 알파가 높아 아래 판별에서 빠지므로 통째로 그림자로 본다
    float shadow = (u_vanilla > 0.5 && u_style.x > 0.5) ? keep
                 : keep * (1.0 - smoothstep(0.08, 0.25, max(max(straight.r, straight.g), straight.b)))
                        * (1.0 - smoothstep(0.9, 0.99, a));
    float alpha = u_noMask > 0.5 ? a
                : a * (keep - shadow + shadow * fillOp + m.r + m.b + m.g * fillOp) * u_opacity;

    // 배경 반전 (2번에 나눠 그린다): 1번째는 반전할 부분을 비우고 나머지를, 2번째는 반전할 부분만
    // 흰색으로 내보내고 블렌드(1 - 화면색)로 이미 그려진 뒷배경을 뒤집는다
    if (u_pass > 0.5) {
        float w = u_noMask > 0.5 ? 0.0 : clamp(m.r * u_invOutline + m.b * u_invIcon + m.g * u_invFill, 0.0, 1.0);
        if (u_pass > 1.5) {
            float wa = w * a * u_opacity;
            gl_FragColor = vec4(vec3(wa), wa) * v_fragmentColor;
            return;
        }
        alpha *= 1.0 - w;
    }
    gl_FragColor = vec4(col * alpha, alpha) * v_fragmentColor;
}
)";

struct Uniforms {
	GLint rect, trim, rotated, style, mask, posMode, bounds, text, textMode, textColor, glow, shine, phase;
	GLint noMask, invert, vanilla, plain, active, activeMode, activeFill, a1, a2, aFill;
	GLint sdf, hasSdf, texel, fill2, fillAngle, fillTheme, opacity, fillOpacity, bevel, bevelSize, innerShadow,
		innerShadowSize, iconBevel, iconBevelSize, fillOpIcons;
	GLint bg, glassOn, vp, glass, glassRefract, glassRim, glassEdge, glassDispersion, bgSharp, frost, gray;
	GLint pass, invOutline, invIcon, invFill, invText, packVanilla;
	GLint adjOn, adjKeep, adjOutline, adjFill, adjIcon, rangeOn, range, colorizeGray, whole;
	GLint hueAnim, fxHue, activeAdjOn, activeAdj, packWhole, light, spec, squish, glassSat, glassNoise, flowBody;
	GLint time, gradMode, hueStart, hueRange, sat, val, angle, flow, spin, use3;
	GLint c1, c2, c3, fill, accent, glyphColor, glyphMode, whiteOutline, fillMode, outlineSrc;
};

std::vector<Pack> g_packs;
std::vector<Sheet> g_addons;   // Geode 버튼 시트 (팩이 켜져 있을 때만 같이 칠한다)
Pack* g_active = nullptr;
std::unordered_map<CCTexture2D*, Sheet*> g_bound;    // 실제 로드된 시트 텍스쳐 → 우리 시트 정보
std::unordered_map<CCTexture2D*, Single*> g_boundSingles;
// 단독 이미지 이름 → 마지막으로 로드된 텍스쳐 (retain. GL 컨텍스트가 바뀌면 release하지 않고 버린다 → rebuildGL)
std::unordered_map<std::string, CCTexture2D*> g_loaded;
std::unordered_set<std::string> g_singleNames;
unsigned g_lastRefresh = ~0u;

// 텍스쳐 캐시 훅은 로딩 스레드에서 불릴 수 있으므로 기록만 해 두고 메인 스레드에서 처리한다
std::mutex g_pendingMutex;
std::vector<std::pair<std::string, CCTexture2D*>> g_pending;

CCGLProgram* g_program = nullptr;
bool g_programFailed = false;
Uniforms g_u{};
unsigned g_lastGlobals = ~0u;

// GL 컨텍스트 재생성 감지: 창 포인터가 바뀌면 이전 컨텍스트의 GL 객체는 모두 사라진 것
void* g_window = nullptr;
unsigned g_lastGLCheck = ~0u;

Settings g_settings;
unsigned g_lastSettings = ~0u;

Stats g_stats;
float g_statsSince = 0;

auto const g_start = std::chrono::steady_clock::now();

unsigned frameCount() {
	return CCDirector::sharedDirector()->getTotalFrames();
}

float seconds() {
	return std::chrono::duration<float>(std::chrono::steady_clock::now() - g_start).count();
}

double num(matjson::Value const& v) {
	return v.asDouble().unwrapOr(0.0);
}

// 배열 원소 (정수 리터럴 인덱스는 string_view 키와 모호해서 size_t로 고정)
float at(matjson::Value const& arr, std::size_t i) {
	return static_cast<float>(num(arr[i]));
}

// 프레임 표 한 줄 → 셰이더 값
FrameInfo makeFrame(matjson::Value const& row, int sheetW, int sheetH) {
	float x = at(row, 0), y = at(row, 1), w = at(row, 2), h = at(row, 3);
	bool rotated = at(row, 4) != 0;
	float sizeW = at(row, 5), sizeH = at(row, 6);
	float offX = at(row, 7), offY = at(row, 8);
	float srcW = std::max(at(row, 9), 1.f), srcH = std::max(at(row, 10), 1.f);

	FrameInfo f{};
	f.rect[0] = (x + 0.5f) / sheetW;
	f.rect[1] = (y + 0.5f) / sheetH;
	f.rect[2] = (x + w - 0.5f) / sheetW;
	f.rect[3] = (y + h - 0.5f) / sheetH;
	// 트림된 이미지는 원본 크기 중앙에서 offset만큼(y-up) 옮겨진 위치에 있다
	f.trim[0] = ((srcW - sizeW) / 2 + offX) / srcW;
	f.trim[1] = ((srcH - sizeH) / 2 - offY) / srcH;
	f.trim[2] = sizeW / srcW;
	f.trim[3] = sizeH / srcH;
	f.rotated = rotated ? 1.f : 0.f;
	f.style[0] = at(row, 11);
	f.style[1] = at(row, 12);
	f.style[2] = at(row, 13) / 255.f;
	return f;
}

Sheet parseSheet(matjson::Value const& s, bool addon, std::filesystem::path const& dir) {
	Sheet sheet;
	sheet.dir = dir;
	sheet.name = s["name"].asString().unwrapOr("");
	sheet.width = static_cast<int>(at(s["size"], 0));
	sheet.height = static_cast<int>(at(s["size"], 1));
	sheet.maskFile = s["mask"].asString().unwrapOr("");
	sheet.probe = s["probe"].asString().unwrapOr("");
	sheet.addon = addon;
	auto const& names = s["names"];
	bool named = names.isArray() && names.size() == s["frames"].size();
	std::size_t i = 0;
	for (auto const& row : s["frames"]) {
		auto x = static_cast<uint32_t>(at(row, 0)), y = static_cast<uint32_t>(at(row, 1));
		auto frame = makeFrame(row, sheet.width, sheet.height);
		if (addon) frame.vanilla = at(row, 14) != 0 ? 2.f : 1.f;
		else frame.kind = at(row, 14);
		sheet.byOrigin[(x << 16) | y] = frame;
		if (named) {
			auto name = names[i].asString().unwrapOr("");
			if (!name.empty()) {
				sheet.checks.push_back({name, static_cast<int>(x), static_cast<int>(y), static_cast<int>(at(row, 2)),
				                        static_cast<int>(at(row, 3)), at(row, 4) != 0});
			}
		}
		++i;
	}
	return sheet;
}

bool sizeIs(CCTexture2D* tex, int w, int h) {
	return tex && static_cast<int>(tex->getPixelsWide()) == w && static_cast<int>(tex->getPixelsHigh()) == h;
}

// 마스크는 GD 텍스쳐 캐시를 거치지 않고 직접 만든다: GD가 그래픽 설정 변경 때 캐시를 비우거나
// 다시 채워도 영향받지 않고, 컨텍스트가 바뀌면 우리가 직접 다시 만든다.
CCTexture2D* loadMask(std::filesystem::path const& dir, std::string const& file, int w, int h) {
	auto path = theme::pathString(dir / file);
	CCImage image;
	if (!image.initWithImageFile(path.c_str())) {
		log::error("mask load failed: {}", path);
		return nullptr;
	}
	auto tex = new CCTexture2D();
	if (!tex->initWithImage(&image)) {
		log::error("mask upload failed: {}", path);
		tex->release();
		return nullptr;
	}
	if (!sizeIs(tex, w, h)) {
		log::error("mask size mismatch for {}: {}x{}", file, tex->getPixelsWide(), tex->getPixelsHigh());
		tex->release();
		return nullptr;
	}
	return tex;   // new로 만든 참조 1개를 우리가 소유
}

// 거리장: 마스크와 같은 배치의 절반 해상도 PNG (R = 몸통, G = 아이콘) → 두 채널(밝기+알파, AI88) 텍스쳐.
// 셰이더에서 .r = 몸통 거리, .a = 아이콘 거리.
// 파일이 없으면 nullptr (모양 효과만 꺼진다)
CCTexture2D* loadSdf(std::filesystem::path const& dir, std::string const& maskFile, int w, int h) {
	auto file = maskFile;
	if (auto at = file.rfind("-mask.png"); at != std::string::npos) file.replace(at, 9, "-sdf.png");
	auto path = dir / file;
	std::error_code ec;
	if (!std::filesystem::exists(path, ec)) return nullptr;
	CCImage image;
	if (!image.initWithImageFile(theme::pathString(path).c_str())) {
		log::warn("sdf load failed: {}", file);
		return nullptr;
	}
	int sw = image.getWidth(), sh = image.getHeight();
	if (sw != (w + 1) / 2 || sh != (h + 1) / 2 || image.getBitsPerComponent() != 8) {
		log::warn("sdf size mismatch for {}: {}x{}", file, sw, sh);
		return nullptr;
	}
	int bpp = image.hasAlpha() ? 4 : 3;
	auto src = image.getData();
	std::size_t count = static_cast<std::size_t>(sw) * sh;
	std::vector<unsigned char> la(count * 2);
	for (std::size_t i = 0; i < count; ++i) {
		la[i * 2] = src[i * bpp];           // 몸통
		la[i * 2 + 1] = src[i * bpp + 1];   // 아이콘
	}
	auto tex = new CCTexture2D();
	if (!tex->initWithData(la.data(), kCCTexture2DPixelFormat_AI88, sw, sh, CCSize(sw, sh))) {
		log::warn("sdf upload failed: {}", file);
		tex->release();
		return nullptr;
	}
	return tex;
}

// 마스크 + 거리장 (없는 것만). 저품질 설정에서 RGBA4444/RGB565로 줄어들면 마스크 비율이 뭉개지므로 형식을 고정한다
void loadTextures(std::filesystem::path const& dir, std::string const& maskFile, int w, int h, CCTexture2D*& mask,
                  CCTexture2D*& sdf) {
	auto prev = CCTexture2D::defaultAlphaPixelFormat();
	CCTexture2D::setDefaultAlphaPixelFormat(kCCTexture2DPixelFormat_RGBA8888);
	if (!mask) mask = loadMask(dir, maskFile, w, h);
	if (mask && !sdf) sdf = loadSdf(dir, maskFile, w, h);
	CCTexture2D::setDefaultAlphaPixelFormat(prev);
}

void loadMasks(Pack& pack) {
	for (auto& sheet : pack.sheets) {
		loadTextures(sheet.dir, sheet.maskFile, sheet.width, sheet.height, sheet.mask, sheet.sdf);
	}
	for (auto& single : pack.singles) {
		loadTextures(single.dir, single.maskFile, single.width, single.height, single.mask, single.sdf);
	}
}

// 같은 GL 컨텍스트에서 만든 마스크/거리장을 놓는다 (팩 데이터를 다시 읽거나 바꿀 때)
void releaseMasks(Pack& pack) {
	auto drop = [](CCTexture2D*& t) {
		if (t) t->release();
		t = nullptr;
	};
	for (auto& sheet : pack.sheets) {
		drop(sheet.mask);
		drop(sheet.sdf);
	}
	for (auto& single : pack.singles) {
		drop(single.mask);
		drop(single.sdf);
	}
}

// "C:/.../GJ_button_01-uhd.png" → "GJ_button_01"
std::string baseName(char const* path) {
	std::string_view s = path;
	if (auto slash = s.find_last_of("/\\"); slash != std::string_view::npos) s.remove_prefix(slash + 1);
	if (auto dot = s.rfind('.'); dot != std::string_view::npos) s = s.substr(0, dot);
	for (std::string_view suffix : {"-uhd", "-hd"}) {
		if (s.size() > suffix.size() && s.substr(s.size() - suffix.size()) == suffix) {
			s.remove_suffix(suffix.size());
			break;
		}
	}
	return std::string(s);
}

// 텍스쳐 캐시 훅이 쌓아 둔 단독 이미지 기록을 메인 스레드에서 반영
void processPending() {
	std::vector<std::pair<std::string, CCTexture2D*>> pending;
	{
		std::lock_guard lock(g_pendingMutex);
		pending.swap(g_pending);
	}
	for (auto& [name, texture] : pending) {
		auto& slot = g_loaded[name];
		if (slot == texture) continue;
		// 팩을 바꾸면 텍스쳐가 새로 만들어진다. 이전 것은 놓고 새 것을 잡아 둔다 (포인터 재사용 혼동 방지)
		texture->retain();
		if (slot) slot->release();
		slot = texture;
	}
}

// 훅을 못 거친 단독 이미지(비동기 로딩 등)는 캐시에서 직접 찾아본다
CCTexture2D* findCachedSingle(Single const& single) {
	auto full = CCFileUtils::sharedFileUtils()->fullPathForFilename((single.file + "-uhd.png").c_str(), true);
	return CCTextureCache::sharedTextureCache()->textureForKey(full.c_str());
}

// 프레임 캐시를 직접 조회한다. spriteFrameByName은 없는 프레임에 대해 Geode가 경고를 남기고 대체 텍스쳐를
// 돌려주므로, 시트가 아직 로드되지 않은 로딩 화면/리로드 중에 로그가 지저분해진다.
CCSpriteFrame* findFrame(char const* name) {
	auto dict = CCSpriteFrameCache::sharedSpriteFrameCache()->m_pSpriteFrames;
	return dict ? static_cast<CCSpriteFrame*>(dict->objectForKey(name)) : nullptr;
}

// 팩 데이터(json) → Pack. 모드 내장 데이터와 텍스쳐 팩 안 theme-data/pack.json이 같은 형식이다.
// dir: 마스크/거리장 파일이 있는 폴더
Pack parsePack(matjson::Value const& root, std::string const& fallbackId, std::filesystem::path const& dir) {
	Pack pack;
	pack.id = root["id"].asString().unwrapOr(fallbackId);
	pack.name = root["name"].asString().unwrapOr(pack.id);
	pack.profile = root["profile"].asString().unwrapOr("diamond");
	pack.match = root["match"].asString().unwrapOr("");
	for (auto const& s : root["sheets"]) pack.sheets.push_back(parseSheet(s, false, dir));
	for (auto const& s : root["singles"]) {
		Single single;
		single.dir = dir;
		single.file = s["file"].asString().unwrapOr("");
		single.width = static_cast<int>(at(s["size"], 0));
		single.height = static_cast<int>(at(s["size"], 1));
		single.maskFile = s["mask"].asString().unwrapOr("");
		float w = static_cast<float>(single.width), h = static_cast<float>(single.height);
		single.frame = FrameInfo{
			{0.5f / w, 0.5f / h, 1.f - 0.5f / w, 1.f - 0.5f / h},
			{0.f, 0.f, 1.f, 1.f},
			0.f,
			{at(s["style"], 0), at(s["style"], 1), at(s["style"], 2) / 255.f},
		};
		single.frame.kind = static_cast<float>(s["kind"].asDouble().unwrapOr(0.0));
		g_singleNames.insert(single.file);
		pack.singles.push_back(std::move(single));
	}
	// 팩 스타일 요약: 윤곽선 없는 프레임이 절반을 넘으면 윤곽선 없는 팩, 바탕색은 가장 흔한 값
	std::size_t total = 0, noOutline = 0;
	std::unordered_map<int, int> fills;
	for (auto const& sheet : pack.sheets) {
		for (auto const& [_, f] : sheet.byOrigin) {
			++total;
			if (f.style[0] == 0) ++noOutline;
			++fills[static_cast<int>(std::lround(f.style[2] * 255))];
		}
	}
	pack.plain = total && noOutline * 2 > total;
	if (!fills.empty()) {
		auto best = std::max_element(fills.begin(), fills.end(), [](auto const& a, auto const& b) {
			return a.second < b.second;
		});
		pack.fill = best->first / 255.f;
	}
	log::info("loaded pack data: {} ({} sheets, {} single textures, {} profile, {} style, fill {})", pack.name,
	          pack.sheets.size(), pack.singles.size(), pack.profile, pack.plain ? "no-outline" : "outline",
	          static_cast<int>(std::lround(pack.fill * 255)));
	return pack;
}

// 데이터의 프레임 배치가 실제 로드된 시트와 같은지 (이름마다 위치/크기/회전 비교). 텍스쳐가 바뀔 때만 다시 확인한다.
// 이름이 없는 옛 데이터는 확인할 수 없으므로 예전처럼 크기만으로 판단한다
// external: 팩 폴더 안 theme-data면 true. 모드 내장 데이터가 안 맞는 건 대개 다른 팩일 뿐이라 조용히 넘긴다
bool layoutMatches(Sheet& sheet, CCTexture2D* tex, bool external) {
	if (sheet.checkedTex == tex) return sheet.layoutOk;
	sheet.checkedTex = tex;
	int bad = 0;
	std::string first;
	for (auto const& c : sheet.checks) {
		auto frame = findFrame(c.name.c_str());
		bool ok = false;
		if (frame && frame->getTexture() == tex) {
			auto r = frame->getRectInPixels();
			bool rot = frame->isRotated();
			// 회전된 프레임: 데이터는 시트 기준 가로세로, 프레임은 원래 그림 기준
			auto w = std::lround(rot ? r.size.height : r.size.width), h = std::lround(rot ? r.size.width : r.size.height);
			ok = rot == c.rotated && std::lround(r.origin.x) == c.x && std::lround(r.origin.y) == c.y && w == c.w &&
			     h == c.h;
		}
		if (!ok && bad++ == 0) first = c.name;
	}
	sheet.layoutOk = bad == 0;
	if (!sheet.layoutOk && !external) {
		log::info("built-in data for {} does not match the loaded sheet ({} of {} buttons differ): another pack",
		          sheet.name, bad, sheet.checks.size());
	}
	else if (!sheet.layoutOk) {
		log::warn("theme data out of date: {} — {} of {} buttons moved or missing (first: {}). The pack was probably "
		          "updated; rebuild its theme data with the pack tool. This sheet is left unthemed.",
		          sheet.name, bad, sheet.checks.size(), first);
	}
	return sheet.layoutOk;
}

// 옛 데이터를 만난 팩은 한 번만 화면에 알린다 (그리는 도중이므로 다음 틱에 띄운다)
void reportStale(std::string const& packName) {
	static std::unordered_set<std::string> reported;
	if (!reported.insert(packName).second) return;
	Loader::get()->queueInMainThread([packName] {
		Notification::create(fmt::format("Diamond Theme: theme data for \"{}\" is out of date (the pack changed). "
		                                 "Rebuild it with the pack tool.", packName),
		                     NotificationIcon::Warning, 4.f)
			->show();
	});
}

// 텍스쳐 팩 안 테마 데이터 (지금 켜진 메인 시트가 들어 있는 팩 폴더의 theme-data/pack.json)
std::unique_ptr<Pack> g_external;
std::filesystem::path g_sheetDir;   // 지금 쓰는 메인 시트 파일이 있는 폴더 (변화 감지용)

// 메인 시트를 실제로 어느 폴더에서 읽었는지 (Texture Loader가 팩 폴더를 검색 경로 앞에 넣는다)
std::filesystem::path mainSheetDir() {
	auto full = CCFileUtils::sharedFileUtils()->fullPathForFilename("GJ_GameSheet03-uhd.png", true);
	std::filesystem::path p(std::string(full.c_str()));
	return p.has_parent_path() ? p.parent_path() : std::filesystem::path{};
}

// 팩 폴더의 theme-data를 (다시) 읽는다. 없어졌거나 바뀌면 이전 데이터를 놓는다
void updateExternal(std::filesystem::path const& sheetDir) {
	std::error_code ec;
	auto dataFile = sheetDir.empty() ? std::filesystem::path{} : sheetDir / "theme-data" / "pack.json";
	bool exists = !dataFile.empty() && std::filesystem::exists(dataFile, ec);
	auto mtime = exists ? std::filesystem::last_write_time(dataFile, ec) : std::filesystem::file_time_type{};
	if (g_external && exists && g_external->dataFile == dataFile && g_external->dataTime == mtime) return;
	if (!g_external && !exists) return;

	if (g_external) {
		if (g_active == g_external.get()) g_active = nullptr;
		g_bound.clear();
		g_boundSingles.clear();
		releaseMasks(*g_external);
		log::info("theme data unloaded: {}", g_external->name);
		g_external.reset();
	}
	if (!exists) return;
	auto json = utils::file::readJson(theme::geodePath(dataFile));
	if (!json) {
		log::warn("theme-data/pack.json in {}: {}", theme::pathString(sheetDir), json.unwrapErr());
		return;
	}
	auto pack = std::make_unique<Pack>(parsePack(json.unwrap(), sheetDir.filename().string(), dataFile.parent_path()));
	pack->dataFile = dataFile;
	pack->dataTime = mtime;
	log::info("theme data found in texture pack: {}", theme::pathString(dataFile.parent_path()));
	g_external = std::move(pack);
}

bool sameDir(std::filesystem::path const& a, std::filesystem::path const& b) {
	std::error_code ec;
	return !a.empty() && !b.empty() && std::filesystem::equivalent(a, b, ec);
}

// Geode 버튼 시트를 실제 로드된 Geode 시트 텍스쳐에 연결한다. 크기와 기준 프레임 위치가 둘 다 맞아야 한다
// (Geode가 업데이트되어 시트 배치가 바뀌었거나 텍스쳐 팩이 Geode 스프라이트를 직접 그렸으면 건드리지 않는다).
int bindAddons() {
	static bool mismatchReported = false;
	int bound = 0;
	for (auto& sheet : g_addons) {
		auto frame = findFrame(sheet.probe.c_str());
		auto tex = frame ? frame->getTexture() : nullptr;
		if (!tex) continue;
		auto origin = frame->getRectInPixels().origin;
		auto key = (static_cast<uint32_t>(origin.x) << 16) | static_cast<uint32_t>(origin.y);
		if (!sizeIs(tex, sheet.width, sheet.height) || !sheet.byOrigin.contains(key)) {
			if (!mismatchReported) {
				mismatchReported = true;
				log::info("Geode buttons not themed: {} is {}x{} (expected {}x{}; different Geode version or a pack "
				          "that redraws Geode sprites)", sheet.name, tex->getPixelsWide(), tex->getPixelsHigh(),
				          sheet.width, sheet.height);
			}
			continue;
		}
		if (!sheet.mask) loadTextures(sheet.dir, sheet.maskFile, sheet.width, sheet.height, sheet.mask, sheet.sdf);
		if (!sheet.mask) continue;
		g_bound[tex] = &sheet;
		++bound;
	}
	return bound;
}

float g_offSince = -1.f;   // 지원 팩이 안 보이기 시작한 시각 (보이면 -1)
bool g_offReported = false;

// 어느 팩 데이터를 쓸지 판별한다. 순서:
//  1) 지금 메인 시트가 들어 있는 텍스쳐 팩 폴더의 theme-data (팩 제작자가 넣은 데이터)
//  2) 모드 내장 데이터 (Diamond Pack, Diamond Vortex, 바닐라). 시트 크기 + 기준 프레임으로 맞춰 보고,
//     바닐라 데이터("match": "vanilla")는 시트를 게임 기본 Resources 폴더에서 읽었을 때만 쓴다
//     (시트 이미지만 바꾸고 배치는 바닐라 그대로인 팩도 크기가 같으므로)
void refreshBindings() {
	processPending();
	g_bound.clear();
	auto sheetDir = mainSheetDir();
	if (sheetDir != g_sheetDir) {
		g_sheetDir = sheetDir;
		log::info("main sheet folder: {}", theme::pathString(sheetDir));
	}
	updateExternal(sheetDir);
	bool fromGame = sameDir(sheetDir, theme::stdPath(dirs::getResourcesDir()));

	std::vector<Pack*> candidates;
	if (g_external) candidates.push_back(g_external.get());
	for (auto& pack : g_packs) {
		if (pack.match == "vanilla" && !fromGame) continue;
		candidates.push_back(&pack);
	}

	Pack* found = nullptr;
	for (auto* candidate : candidates) {
		auto& pack = *candidate;
		std::vector<std::pair<CCTexture2D*, Sheet*>> matches;
		bool stale = false;
		for (auto& sheet : pack.sheets) {
			auto frame = findFrame(sheet.probe.c_str());
			auto tex = frame ? frame->getTexture() : nullptr;
			if (tex && static_cast<int>(tex->getPixelsWide()) == sheet.width &&
			    static_cast<int>(tex->getPixelsHigh()) == sheet.height) {
				// 크기는 맞는데 배치가 다르면 같은 팩의 다른 버전: 이 시트는 쓰지 않는다
				if (layoutMatches(sheet, tex, candidate == g_external.get())) matches.emplace_back(tex, &sheet);
				else stale = true;
			}
		}
		// 팩 안에 들어 있던 데이터가 안 맞으면 팩이 업데이트된 것 → 사용자에게 알린다
		if (stale && candidate == g_external.get()) reportStale(pack.name);
		// 메인 시트(GameSheet03)가 맞아야 그 팩으로 본다
		bool mainOk = std::any_of(matches.begin(), matches.end(), [](auto const& m) {
			return m.second->name == "GJ_GameSheet03-uhd";
		});
		if (mainOk) {
			found = &pack;
			for (auto& [tex, sheet] : matches) g_bound[tex] = sheet;
			break;
		}
	}
	bool changed = found != g_active;
	if (changed) {
		g_active = found;
		g_lastSettings = ~0u;   // 팩마다 기본 테마가 다르다
		if (found) loadMasks(*found);
	}

	// 단독 이미지: 텍스쳐 캐시 훅이 기록해 둔 텍스쳐 중 크기가 맞는 것만.
	// 훅을 못 거친 텍스쳐를 캐시에서 직접 찾는 건 팩이 바뀌었을 때와 10번에 한 번만: 경로 찾기(fullPathForFilename)는
	// 텍스쳐 로더가 팩 폴더를 디스크에서 뒤질 수 있어서, 아직 안 쓰인 이미지마다 매초 부르면 가끔 프레임이 튄다
	static unsigned scan = 0;
	bool deepScan = changed || ++scan % 10 == 0;
	g_boundSingles.clear();
	if (found) {
		for (auto& single : found->singles) {
			if (!single.mask) continue;
			auto it = g_loaded.find(single.file);
			auto tex = it != g_loaded.end() ? it->second : nullptr;
			if (!sizeIs(tex, single.width, single.height) && deepScan) tex = findCachedSingle(single);
			if (sizeIs(tex, single.width, single.height)) g_boundSingles[tex] = &single;
		}
	}

	if (found) {
		int addons = g_settings.geodeButtons ? bindAddons() : 0;
		g_offSince = -1.f;
		g_offReported = false;
		if (changed) {
			log::info("active pack: {} ({} sheets, {}/{} single textures, {} Geode sheets bound)",
			          found->name, g_bound.size() - addons, g_boundSingles.size(), found->singles.size(), addons);
		}
		return;
	}

	// 시작 직후 로딩 화면이나 전체 화면 전환 후 리로드 중에는 시트가 잠깐 없다.
	// 3초 넘게 계속 없을 때만 "꺼짐"으로 기록한다.
	auto now = seconds();
	if (g_offSince < 0) g_offSince = now;
	if (!g_offReported && now - g_offSince >= 3.f && now > 5.f) {
		g_offReported = true;
		auto frame = findFrame("GJ_playBtn_001.png");
		auto tex = frame ? frame->getTexture() : nullptr;
		// Low/Medium 품질은 -hd/sd 시트를 쓰는데, 두 팩은 -uhd만 들어 있어서 팩 자체가 적용되지 않는다.
		// 이때 엔진은 그리기에 손대지 않는다.
		log::info("theme off: no matching theme data for the loaded UHD sheets (pack without theme-data, or "
		          "texture quality below High). GJ_GameSheet04 is {}x{}, main sheet folder: {}",
		          tex ? tex->getPixelsWide() : 0, tex ? tex->getPixelsHigh() : 0, theme::pathString(sheetDir));
	}
}

// 설정값을 읽는 곳: 모드 설정(json == nullptr) 또는 Geode가 저장한 설정 파일(settings.json)의 내용.
// 모드 설정 중 고급 설정 키는 Geode 설정이 아니라 고급 설정 저장소(advanced.json)에서 읽는다.
// 없는 키는 지금 값(cur)을 그대로 둔다.
struct Source {
	matjson::Value const* json = nullptr;

	// Geode 설정 창에 있는 키인지 (고급 설정 키나 모르는 키는 아니다)
	bool fromMod(char const* key) const {
		return !json && !advanced::find(key) && Mod::get()->hasSetting(key);
	}

	matjson::Value const* find(char const* key) const {
		auto data = json ? json : (advanced::find(key) ? &advanced::values() : nullptr);
		if (!data) return nullptr;
		auto const& v = (*data)[key];
		return v.isNull() ? nullptr : &v;
	}

	std::string text(char const* key, std::string cur = "") const {
		if (fromMod(key)) return Mod::get()->getSettingValue<std::string>(key);
		auto v = find(key);
		return v ? v->asString().unwrapOr(cur) : cur;
	}

	float number(char const* key, float cur) const {
		if (fromMod(key)) return static_cast<float>(Mod::get()->getSettingValue<double>(key));
		auto v = find(key);
		return v && v->isNumber() ? static_cast<float>(v->asDouble().unwrapOr(cur)) : cur;
	}

	bool flag(char const* key, bool cur) const {
		if (fromMod(key)) return Mod::get()->getSettingValue<bool>(key);
		auto v = find(key);
		return v && v->isBool() ? v->asBool().unwrapOr(cur) : cur;
	}

	ccColor3B color(char const* key, ccColor3B cur) const {
		if (fromMod(key)) return Mod::get()->getSettingValue<ccColor3B>(key);
		auto v = find(key);
		if (!v) return cur;
		if (v->isObject()) {   // Geode 저장 형식: {"r": .., "g": .., "b": ..}
			auto ch = [&](char const* c, GLubyte d) {
				return static_cast<GLubyte>(std::clamp<int64_t>((*v)[c].asInt().unwrapOr(d), 0, 255));
			};
			return {ch("r", cur.r), ch("g", cur.g), ch("b", cur.b)};
		}
		if (auto s = v->asString(); s && !s.unwrap().empty()) {
			if (auto c = cc3bFromHexString(s.unwrap())) return c.unwrap();
		}
		return cur;
	}

	// 선택지 번호 (없거나 모르는 값이면 cur)
	int choice(char const* key, std::initializer_list<char const*> options, int cur = 0) const {
		auto value = text(key, "\x01");
		int i = 0;
		for (auto o : options) {
			if (value == o) return i;
			++i;
		}
		return cur;
	}
};

// 내장 프리셋 테마 (resources/theme-<이름>.json)
std::unordered_map<std::string, matjson::Value> g_builtinThemes;

// 사용자 테마 파일: 경로와 수정 시각이 바뀔 때만 다시 읽는다 (설정을 읽을 때마다 확인 → 저장하면 바로 반영)
struct ThemeFileState {
	std::filesystem::path path;
	std::filesystem::file_time_type mtime{};
	matjson::Value json;
	bool ok = false;
	bool missingReported = false;
};
ThemeFileState g_themeFile;
bool g_reportTheme = false;   // 새로 읽은 테마의 오류/이름을 한 번만 로그에 남긴다

matjson::Value const* themeFileJson() {
	auto path = theme::stdPath(Mod::get()->getSettingValue<asp::fs::Path>("theme-file"));
	auto& f = g_themeFile;
	std::error_code ec;
	auto mtime = std::filesystem::last_write_time(path, ec);
	if (ec) {
		if (f.path != path || !f.missingReported) {
			log::warn("theme file not found: {} (using the pack's original theme)", theme::pathString(path));
			f = ThemeFileState{path, {}, {}, false, true};
		}
		return nullptr;
	}
	if (path != f.path || mtime != f.mtime) {
		bool samePath = path == f.path;
		f.path = path;
		f.mtime = mtime;
		f.missingReported = false;
		auto res = utils::file::readJson(theme::geodePath(path));
		if (res) {
			f.json = std::move(res).unwrap();
			f.ok = true;
			g_reportTheme = true;
		}
		else {
			// 편집 중 잘못 저장된 경우: 같은 파일이면 직전에 읽은 내용을 계속 쓴다
			log::warn("theme file {}: {}", theme::pathString(path), res.unwrapErr());
			if (!samePath) f.ok = false;
		}
	}
	return f.ok ? &f.json : nullptr;
}

void fillSettings(Source const& src, Settings& s, bool generic, int depth);

// Preset = file: 테마 파일(schema 1) 또는 Geode가 저장한 설정 파일(settings.json 복사본)
void applyThemeFile(Settings& s, bool generic, int depth) {
	auto json = themeFileJson();
	if (!json) return;
	bool report = g_reportTheme;
	g_reportTheme = false;
	if (file::isSettingsExport(*json)) {
		// 설정 파일 안의 프리셋이 다시 "file"이면 원본으로 (자기 자신을 계속 불러오지 않도록)
		if (depth == 0) fillSettings(Source{json}, s, generic, depth + 1);
		if (report) {
			log::info("theme file loaded (Geode settings format): {}", theme::pathString(g_themeFile.path));
		}
		return;
	}
	std::vector<std::string> errors;
	file::apply(*json, g_active ? g_active->id : "", s, errors);
	if (report) {
		log::info("theme file loaded: {} ({})", (*json)["name"].asString().unwrapOr("unnamed"),
		          theme::pathString(g_themeFile.path));
		for (auto const& e : errors) log::warn("theme file: {}", e);
	}
}

// 프리셋은 preview/app.js의 PRESETS와 같게 유지한다
void fillSettings(Source const& src, Settings& s, bool generic, int depth) {
	s.pressStyle = src.choice("press-style", {"gd", "squash", "pop", "none", "invert", "jelly"}, s.pressStyle);
	s.jelly = src.number("jelly-strength", s.jelly);
	s.flowBody = src.choice("flow-scope", {"whole-button", "icons"}, s.flowBody ? 0 : 1) == 0;
	s.hoverGlow = src.number("hover-glow", s.hoverGlow);
	s.idle = src.choice("idle-effect", {"none", "pulse", "shimmer"}, s.idle);
	s.listMode = src.choice("list-color-mode", {"original", "dark", "custom"}, s.listMode);
	if (s.listMode == 2) {
		s.list1 = src.color("list-color-1", s.list1);
		s.list2 = src.color("list-color-2", s.list2);
	}
	else if (s.listMode == 1) {   // 어둡게 = 기본 색
		s.list1 = Settings{}.list1;
		s.list2 = Settings{}.list2;
	}
	s.geodeButtons = src.flag("geode-buttons", s.geodeButtons);
	s.activeMode = src.choice("active-mode", {"same", "solid", "gradient"}, s.activeMode);
	s.active1 = src.color("active-color-1", s.active1);
	s.active2 = src.color("active-color-2", s.active2);
	s.activeFill = src.choice("active-fill-mode", {"same", "custom"}, s.activeFill ? 1 : 0) == 1;
	s.activeFillColor = src.color("active-fill-color", s.activeFillColor);
	s.opacity = src.number("button-opacity", s.opacity);
	s.fillOpacity = src.number("fill-opacity", s.fillOpacity);
	s.bevel = src.number("bevel", s.bevel);
	s.bevelSize = src.number("bevel-size", s.bevelSize);
	s.innerShadow = src.number("inner-shadow", s.innerShadow);
	s.innerShadowSize = src.number("inner-shadow-size", s.innerShadowSize);
	s.fillOpacityIconsOnly = src.choice("fill-opacity-scope", {"all", "icon-buttons"}, s.fillOpacityIconsOnly ? 1 : 0) == 1;
	s.iconBevel = src.number("icon-emboss", s.iconBevel);
	s.iconBevelSize = src.number("icon-emboss-size", s.iconBevelSize);
	s.glass = src.number("glass", s.glass);
	s.glassBlur = src.number("glass-blur", s.glassBlur);
	s.glassBlurSize = src.number("glass-blur-size", s.glassBlurSize);
	s.glassSaturation = src.number("glass-saturation", s.glassSaturation);
	s.glassNoise = src.number("glass-noise", s.glassNoise);
	s.glassRefract = src.number("glass-refraction", s.glassRefract);
	s.glassRim = src.number("glass-rim", s.glassRim);
	s.glassEdge = src.number("glass-edge", s.glassEdge);
	s.glassDispersion = src.number("glass-dispersion", s.glassDispersion);
	s.glassSpecular = src.number("glass-specular", s.glassSpecular);
	s.glassFollowMouse = src.flag("glass-follow-mouse", s.glassFollowMouse);
	s.glassScope = src.choice("glass-scope", {"buttons", "icon-buttons", "all"}, s.glassScope);
	s.glassListFrames = src.flag("glass-list-frames", s.glassListFrames);
	s.listGlass = src.number("list-glass", s.listGlass);
	s.listOpacity = src.number("list-opacity", s.listOpacity);
	s.invOutline = src.flag("invert-outline", s.invOutline);
	s.invIcon = src.flag("invert-icon", s.invIcon);
	s.invFill = src.flag("invert-fill", s.invFill);
	s.invText = src.flag("invert-text", s.invText);
	auto adjust = [&](char const* prefix, Adjust& a) {
		a.hue = src.number(fmt::format("{}hue", prefix).c_str(), a.hue);
		a.sat = src.number(fmt::format("{}saturation", prefix).c_str(), a.sat);
		a.val = src.number(fmt::format("{}brightness", prefix).c_str(), a.val);
		a.colorize = src.choice(fmt::format("{}mode", prefix).c_str(), {"shift", "colorize"}, a.colorize ? 1 : 0) == 1;
	};
	adjust("adjust-", s.adjAll);
	adjust("adjust-outline-", s.adjOutline);
	adjust("adjust-fill-", s.adjFill);
	adjust("adjust-icon-", s.adjIcon);
	s.colorizeGray = src.number("adjust-colorize-grays", s.colorizeGray);
	s.hueSpeed = src.number("adjust-hue-speed", s.hueSpeed);
	s.hoverHue = src.number("hover-hue", s.hoverHue);
	adjust("active-adjust-", s.activeAdj);
	for (int i = 0; i < 6; ++i) adjust(fmt::format("range-{}-", ADJUST_RANGES[i]).c_str(), s.adjRanges[i]);
	auto preset = src.text("preset", "original");
	if (preset == "file" && depth > 0) preset = "original";

	if (preset == "original" || preset == "original-flow") {
		if (generic) {
			s.outlineSrc = 1;
			s.glyphMode = 4;
		}
		if (preset == "original-flow") {
			// Diamond: 가로 그라데이션이라 색상 회전은 옆으로 미끄러져 보인다 → 방향을 회전
			if (generic) s.flow = 0.1f;
			else s.spin = 0.12f;
		}
	}
	else if (preset == "file" || g_builtinThemes.contains(preset)) {
		// 테마 파일: 생략한 항목은 팩 원본 테마 + 모드 설정(애니메이션/리스트/켜진 버튼)을 그대로 쓴다
		if (generic) {
			s.outlineSrc = 1;
			s.glyphMode = 4;
		}
		if (preset == "file") applyThemeFile(s, generic, depth);
		else {
			std::vector<std::string> errors;
			file::apply(g_builtinThemes[preset], g_active ? g_active->id : "", s, errors);
		}
	}
	else {   // custom
		s.outlineSrc = static_cast<float>(src.choice("outline-source", {"theme", "original"}, int(s.outlineSrc)));
		s.gradMode = static_cast<float>(src.choice("gradient-mode", {"rainbow", "colors", "solid"}, int(s.gradMode)));
		s.hueStart = src.number("hue-start", s.hueStart);
		s.hueRange = src.number("hue-range", s.hueRange);
		s.sat = src.number("saturation", s.sat);
		s.val = src.number("brightness", s.val);
		s.c1 = src.color("color-1", s.c1);
		s.c2 = src.color("color-2", s.c2);
		s.c3 = src.color("color-3", s.c3);
		s.use3 = src.flag("use-color-3", s.use3);
		s.angle = src.number("angle", s.angle);
		s.flow = src.number("flow", s.flow);
		s.spin = src.number("spin", s.spin);
		s.fillMode = static_cast<float>(
			src.choice("fill-mode", {"original", "custom", "gradient", "theme"}, int(s.fillMode)));
		s.fill = src.color("fill-color", s.fill);
		s.fill2 = src.color("fill-color-2", s.fill2);
		s.fillAngle = src.number("fill-angle", s.fillAngle);
		s.fillTheme = src.number("fill-theme-strength", s.fillTheme);
		s.accent = src.color("accent-color", s.accent);
		s.glyphMode = static_cast<float>(
			src.choice("glyph-mode", {"auto", "accent", "gradient", "custom", "original"}, int(s.glyphMode)));
		s.glyphColor = src.color("glyph-color", s.glyphColor);
		s.whiteOutline = static_cast<float>(src.choice("white-outline", {"accent", "gradient"}, int(s.whiteOutline)));
		s.bigFont = src.choice("font-big", {"original", "gradient", "custom"}, s.bigFont);
		s.goldFont = src.choice("font-gold", {"original", "gradient", "custom"}, s.goldFont);
		s.textColor = src.color("font-color", s.textColor);
		s.goldColor = s.textColor;
	}
}

void readSettings() {
	Settings s;   // 기본값 = Diamond 원본 공식
	s.enabled = Mod::get()->getSettingValue<bool>("enabled");
	s.debugStats = Mod::get()->getSettingValue<bool>("debug-stats");
	// diamond 외 프로필(generic, vanilla)은 공식이 없으므로 "원본" = 원본 색 그대로
	fillSettings(Source{}, s, g_active && g_active->profile != "diamond", 0);
	g_settings = s;
}

void set3(GLint loc, ccColor3B c) {
	g_program->setUniformLocationWith3f(loc, c.r / 255.f, c.g / 255.f, c.b / 255.f);
}

void applyGlobals() {
	auto& s = g_settings;
	auto p = g_program;
	p->setUniformLocationWith1f(g_u.time, seconds());
	p->setUniformLocationWith1f(g_u.gradMode, s.gradMode);
	p->setUniformLocationWith1f(g_u.hueStart, s.hueStart);
	p->setUniformLocationWith1f(g_u.hueRange, s.hueRange);
	p->setUniformLocationWith1f(g_u.sat, s.sat);
	p->setUniformLocationWith1f(g_u.val, s.val);
	p->setUniformLocationWith1f(g_u.angle, s.angle);
	p->setUniformLocationWith1f(g_u.flow, s.flow);
	p->setUniformLocationWith1f(g_u.flowBody, s.flowBody ? 1.f : 0.f);
	p->setUniformLocationWith1f(g_u.spin, s.spin);
	p->setUniformLocationWith1f(g_u.use3, s.use3 ? 1.f : 0.f);
	set3(g_u.c1, s.c1);
	set3(g_u.c2, s.c2);
	set3(g_u.c3, s.c3);
	set3(g_u.fill, s.fill);
	set3(g_u.accent, s.accent);
	set3(g_u.glyphColor, s.glyphColor);
	set3(g_u.a1, s.active1);
	set3(g_u.a2, s.active2);
	set3(g_u.aFill, s.activeFillColor);
	p->setUniformLocationWith1f(g_u.activeMode, static_cast<float>(s.activeMode));
	p->setUniformLocationWith1f(g_u.activeFill, s.activeFill ? 1.f : 0.f);
	p->setUniformLocationWith1f(g_u.plain, g_active && g_active->plain ? 1.f : 0.f);
	// 바닐라 구조 팩과 광택 팩(같은 판정 규칙)은 Geode 버튼도 이미 같은 스타일이라 일반 프레임처럼 다룬다
	bool packVanilla = g_active && (g_active->profile == "vanilla" || g_active->profile == "glossy" ||
	                                g_active->profile == "whole");
	p->setUniformLocationWith1f(g_u.packVanilla, packVanilla ? 1.f : 0.f);
	p->setUniformLocationWith1f(g_u.packWhole, g_active && g_active->profile == "whole" ? 1.f : 0.f);
	p->setUniformLocationWith1f(g_u.glyphMode, s.glyphMode);
	p->setUniformLocationWith1f(g_u.whiteOutline, s.whiteOutline);
	p->setUniformLocationWith1f(g_u.fillMode, s.fillMode);
	set3(g_u.fill2, s.fill2);
	p->setUniformLocationWith1f(g_u.fillAngle, s.fillAngle);
	p->setUniformLocationWith1f(g_u.fillTheme, s.fillTheme);
	p->setUniformLocationWith1f(g_u.outlineSrc, s.outlineSrc);
	p->setUniformLocationWith1f(g_u.opacity, s.opacity);
	p->setUniformLocationWith1f(g_u.fillOpacity, s.fillOpacity);
	p->setUniformLocationWith1f(g_u.bevel, s.bevel);
	p->setUniformLocationWith1f(g_u.bevelSize, s.bevelSize);
	p->setUniformLocationWith1f(g_u.innerShadow, s.innerShadow);
	p->setUniformLocationWith1f(g_u.innerShadowSize, s.innerShadowSize);
	p->setUniformLocationWith1f(g_u.iconBevel, s.iconBevel);
	p->setUniformLocationWith1f(g_u.iconBevelSize, s.iconBevelSize);
	p->setUniformLocationWith1f(g_u.fillOpIcons, s.fillOpacityIconsOnly ? 1.f : 0.f);
	p->setUniformLocationWith1f(g_u.glass, s.glass);
	p->setUniformLocationWith1f(g_u.glassRefract, s.glassRefract);
	p->setUniformLocationWith1f(g_u.glassRim, s.glassRim);
	p->setUniformLocationWith1f(g_u.glassEdge, s.glassEdge);
	p->setUniformLocationWith1f(g_u.glassDispersion, s.glassDispersion);
	p->setUniformLocationWith1f(g_u.frost, s.glassBlur);
	p->setUniformLocationWith1f(g_u.spec, s.glassSpecular);
	p->setUniformLocationWith1f(g_u.glassSat, s.glassSaturation);
	p->setUniformLocationWith1f(g_u.glassNoise, s.glassNoise);
#ifdef GEODE_IS_DESKTOP
	if (s.glassFollowMouse) {
		// 마우스 위치 → 화면 비율 (유리 배경 좌표와 같은 0..1, y-up)
		auto win = CCDirector::sharedDirector()->getWinSize();
		auto mouse = geode::cocos::getMousePos();
		p->setUniformLocationWith3f(g_u.light, mouse.x / win.width, mouse.y / win.height, 1.f);
	}
	else p->setUniformLocationWith3f(g_u.light, 0.f, 0.f, 0.f);
#else
	p->setUniformLocationWith3f(g_u.light, 0.f, 0.f, 0.f);
#endif
	p->setUniformLocationWith1f(g_u.invOutline, s.invOutline ? 1.f : 0.f);
	p->setUniformLocationWith1f(g_u.invIcon, s.invIcon ? 1.f : 0.f);
	p->setUniformLocationWith1f(g_u.invFill, s.invFill ? 1.f : 0.f);
	p->setUniformLocationWith1f(g_u.invText, s.invText ? 1.f : 0.f);
	// 색 조정: 부위별 값 = 전체 + 부위 (색상은 더하고 채도/밝기는 곱한다).
	// 부위가 색상 고정이면 부위 색상, 전체만 고정이면 전체 색상 + 부위 회전
	bool rangeOn = std::any_of(s.adjRanges.begin(), s.adjRanges.end(), [](Adjust const& a) { return !a.identity(); });
	bool adjOn = rangeOn || !s.adjAll.identity() || !s.adjOutline.identity() || !s.adjFill.identity() ||
	             !s.adjIcon.identity();
	p->setUniformLocationWith1f(g_u.adjOn, adjOn ? 1.f : 0.f);
	p->setUniformLocationWith1f(g_u.rangeOn, rangeOn ? 1.f : 0.f);
	p->setUniformLocationWith1f(g_u.colorizeGray, s.colorizeGray);
	// 색조 애니메이션: 시작부터 지난 시간 × 속도 (바퀴 단위, 소수부만)
	p->setUniformLocationWith1f(g_u.hueAnim, static_cast<float>(std::fmod(seconds() * s.hueSpeed / 360.0, 1.0)));
	p->setUniformLocationWith1f(g_u.activeAdjOn, s.activeAdj.identity() ? 0.f : 1.f);
	p->setUniformLocationWith4f(g_u.activeAdj, s.activeAdj.hue / 360.f, s.activeAdj.sat, s.activeAdj.val,
	                            s.activeAdj.colorize ? 1.f : 0.f);
	GLfloat ranges[18];
	for (int i = 0; i < 6; ++i) {
		ranges[i * 3] = s.adjRanges[i].hue / 360.f;
		ranges[i * 3 + 1] = s.adjRanges[i].sat;
		ranges[i * 3 + 2] = s.adjRanges[i].val;
	}
	p->setUniformLocationWith3fv(g_u.range, ranges, 6);
	auto setAdj = [&](GLint loc, Adjust const& part) {
		float hue = part.colorize ? part.hue : s.adjAll.hue + part.hue;
		bool fixed = part.colorize || s.adjAll.colorize;
		p->setUniformLocationWith4f(loc, hue / 360.f, s.adjAll.sat * part.sat, s.adjAll.val * part.val, fixed ? 1.f : 0.f);
	};
	setAdj(g_u.adjKeep, Adjust{});
	setAdj(g_u.adjOutline, s.adjOutline);
	setAdj(g_u.adjFill, s.adjFill);
	setAdj(g_u.adjIcon, s.adjIcon);
}

void refreshIfDue() {
	auto frame = frameCount();
	if (g_lastRefresh == ~0u || frame - g_lastRefresh >= REFRESH_FRAMES) {
		g_lastRefresh = frame;
		if (!g_settings.debugStats) return refreshBindings();
		auto start = std::chrono::steady_clock::now();
		refreshBindings();
		double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		g_stats.refreshMax = std::max(g_stats.refreshMax, took);
		if (took > 0.004) log::info("[stats] slow pack refresh: {:.1f} ms", took * 1000);
	}
}

// 공통: 전역 설정(프레임당 한 번) + 프레임별 값 + 마스크 바인딩
void applyCommon(FrameInfo const& frame, Textures const& tex, float posMode, float const* bounds, float text,
                 float textMode, Fx const& fx) {
	auto mask = tex.mask;
	auto sdf = mask ? tex.sdf : nullptr;
	auto now = frameCount();
	if (now != g_lastGlobals) {
		g_lastGlobals = now;
		applyGlobals();
	}
	auto p = g_program;
	p->setUniformLocationWith4f(g_u.rect, frame.rect[0], frame.rect[1], frame.rect[2], frame.rect[3]);
	p->setUniformLocationWith4f(g_u.trim, frame.trim[0], frame.trim[1], frame.trim[2], frame.trim[3]);
	p->setUniformLocationWith1f(g_u.rotated, frame.rotated);
	// 기본 스타일 스프라이트의 원래 바탕색은 활성 팩의 바탕색으로 (Diamond #111111, Vortex #171717)
	float fill = frame.vanilla > 0 && g_active ? g_active->fill : frame.style[2];
	p->setUniformLocationWith3f(g_u.style, frame.style[0], frame.style[1], fill);
	p->setUniformLocationWith1f(g_u.posMode, posMode);
	if (bounds) p->setUniformLocationWith4f(g_u.bounds, bounds[0], bounds[1], bounds[2], bounds[3]);
	p->setUniformLocationWith1f(g_u.text, text);
	p->setUniformLocationWith1f(g_u.textMode, textMode);
	p->setUniformLocationWith1f(g_u.glow, fx.glow);
	p->setUniformLocationWith1f(g_u.shine, fx.shine);
	p->setUniformLocationWith1f(g_u.phase, fx.phase);
	p->setUniformLocationWith1f(g_u.vanilla, frame.vanilla);
	p->setUniformLocationWith1f(g_u.whole, frame.kind > 1.5f ? 1.f : 0.f);
	p->setUniformLocationWith1f(g_u.fxHue, fx.hover * g_settings.hoverHue / 360.f);
	p->setUniformLocationWith1f(g_u.squish, std::min(fx.squish * 4.f, 1.5f));
	p->setUniformLocationWith1f(g_u.active, fx.active);
	p->setUniformLocationWith1f(g_u.invert, fx.invert);
	p->setUniformLocationWith1f(g_u.gray, fx.gray);
	p->setUniformLocationWith1f(g_u.pass, fx.pass);
	p->setUniformLocationWith1f(g_u.noMask, mask ? 0.f : 1.f);
	p->setUniformLocationWith1f(g_u.hasSdf, sdf ? 1.f : 0.f);
	if (mask) {
		p->setUniformLocationWith2f(g_u.texel, 1.f / mask->getPixelsWide(), 1.f / mask->getPixelsHigh());
	}

	bool glassOn = mask && fx.glass > 0 && glass::texture();
	p->setUniformLocationWith1f(g_u.glassOn, glassOn ? 1.f : 0.f);
	if (glassOn) {
		auto v = glass::viewport();
		p->setUniformLocationWith4f(g_u.vp, v[0], v[1], v[2], v[3]);
	}

	if (mask) {
		ccGLBindTexture2DN(1, mask->getName());
		if (sdf) ccGLBindTexture2DN(2, sdf->getName());
		if (glassOn) {
			ccGLBindTexture2DN(3, glass::texture());
			ccGLBindTexture2DN(4, glass::sharpTexture());
		}
		// 활성 유닛을 0으로 되돌려 둔다: 이후 다른 코드가 glBindTexture를 직접 부를 때 유닛 1에 묶이지 않도록
		glActiveTexture(GL_TEXTURE0);
	}
}

// 라벨용: 텍스쳐 전체를 쓰는 프레임 (샘플링 제한 없음)
FrameInfo const FULL_FRAME{{0.f, 0.f, 1.f, 1.f}, {0.f, 0.f, 1.f, 1.f}, 0.f, {0.f, 0.f, 0.f}};

} // namespace

void loadPacks() {
	auto dir = theme::stdPath(Mod::get()->getResourcesDir());
	auto list = utils::file::readJson(theme::geodePath(dir / "packs.json"));
	if (!list) {
		log::error("packs.json: {}", list.unwrapErr());
		return;
	}
	for (auto const& idValue : list.unwrap()) {
		auto id = idValue.asString().unwrapOr("");
		auto json = utils::file::readJson(theme::geodePath(dir / (id + ".json")));
		if (!json) {
			log::error("{}.json: {}", id, json.unwrapErr());
			continue;
		}
		g_packs.push_back(parsePack(json.unwrap(), id, dir));
	}

	for (auto name : {"vortex", "neon", "sunset", "mono", "glass", "frosted", "invert"}) {
		if (auto json = utils::file::readJson(theme::geodePath(dir / fmt::format("theme-{}.json", name)))) {
			g_builtinThemes[name] = std::move(json).unwrap();
		}
		else {
			log::error("theme-{}.json: {}", name, json.unwrapErr());
		}
	}
	file::installExamples();

	// Geode 버튼 시트 (tools/geode_masks.py). 팩과 무관하게 Geode 자체 시트에 붙는다.
	if (auto geode = utils::file::readJson(theme::geodePath(dir / "geode.json"))) {
		for (auto const& s : geode.unwrap()["sheets"]) g_addons.push_back(parseSheet(s, true, dir));
		log::info("loaded Geode button data ({} sheets)", g_addons.size());
	}
	else {
		log::warn("geode.json: {}", geode.unwrapErr());
	}
}

namespace {

// 셰이더 컴파일/링크 + uniform 위치. 컨텍스트가 바뀌면 같은 객체에 다시 호출한다
// (스프라이트들이 이 객체 포인터를 들고 있으므로 새 객체로 바꾸지 않는다).
bool initProgram(CCGLProgram* p) {
	if (!p->initWithVertexShaderByteArray(VERTEX_SHADER, FRAGMENT_SHADER)) {
		log::error("shader compile failed:\n{}\n{}", p->vertexShaderLog(), p->fragmentShaderLog());
		return false;
	}
	p->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
	p->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
	p->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
	if (!p->link()) {
		log::error("shader link failed: {}", p->programLog());
		return false;
	}
	p->updateUniforms();

	auto loc = [p](char const* name) { return p->getUniformLocationForName(name); };
	g_u = Uniforms{
		loc("u_rect"), loc("u_trim"), loc("u_rotated"), loc("u_style"), loc("u_mask"),
		loc("u_posMode"), loc("u_bounds"), loc("u_text"), loc("u_textMode"), loc("u_textColor"),
		loc("u_glow"), loc("u_shine"), loc("u_phase"),
		loc("u_noMask"), loc("u_invert"), loc("u_vanilla"), loc("u_plain"), loc("u_active"), loc("u_activeMode"), loc("u_activeFill"),
		loc("u_a1"), loc("u_a2"), loc("u_aFill"),
		loc("u_sdf"), loc("u_hasSdf"), loc("u_texel"), loc("u_fill2"), loc("u_fillAngle"), loc("u_fillTheme"),
		loc("u_opacity"), loc("u_fillOpacity"), loc("u_bevel"), loc("u_bevelSize"), loc("u_innerShadow"),
		loc("u_innerShadowSize"), loc("u_iconBevel"), loc("u_iconBevelSize"), loc("u_fillOpIcons"),
		loc("u_bg"), loc("u_glassOn"), loc("u_vp"), loc("u_glass"), loc("u_glassRefract"), loc("u_glassRim"),
		loc("u_glassEdge"), loc("u_glassDispersion"), loc("u_bgSharp"), loc("u_frost"), loc("u_gray"),
		loc("u_pass"), loc("u_invOutline"), loc("u_invIcon"), loc("u_invFill"), loc("u_invText"), loc("u_packVanilla"),
		loc("u_adjOn"), loc("u_adjKeep"), loc("u_adjOutline"), loc("u_adjFill"), loc("u_adjIcon"),
		loc("u_rangeOn"), loc("u_range"), loc("u_colorizeGray"), loc("u_whole"),
		loc("u_hueAnim"), loc("u_fxHue"), loc("u_activeAdjOn"), loc("u_activeAdj"), loc("u_packWhole"),
		loc("u_light"), loc("u_spec"), loc("u_squish"), loc("u_glassSat"), loc("u_glassNoise"), loc("u_flowBody"),
		loc("u_time"), loc("u_gradMode"), loc("u_hueStart"), loc("u_hueRange"), loc("u_sat"), loc("u_val"),
		loc("u_angle"), loc("u_flow"), loc("u_spin"), loc("u_use3"),
		loc("u_c1"), loc("u_c2"), loc("u_c3"), loc("u_fill"), loc("u_accent"), loc("u_glyphColor"),
		loc("u_glyphMode"), loc("u_whiteOutline"), loc("u_fillMode"), loc("u_outlineSrc"),
	};
	p->use();
	p->setUniformLocationWith1i(g_u.mask, 1);   // 마스크는 텍스쳐 유닛 1
	p->setUniformLocationWith1i(g_u.sdf, 2);    // 거리장은 텍스쳐 유닛 2
	p->setUniformLocationWith1i(g_u.bg, 3);     // 유리 배경(블러)은 텍스쳐 유닛 3
	p->setUniformLocationWith1i(g_u.bgSharp, 4);   // 블러 전 원본은 유닛 4
	g_lastGlobals = ~0u;   // 새 프로그램에는 전역 uniform이 비어 있다
	return true;
}

// 창 핸들은 윈도우판 CCEGLView에만 있다. 다른 기기는 창 비교 없이 프로그램 검사만으로 복구한다.
void* currentWindow() {
#ifdef GEODE_IS_WINDOWS
	auto view = CCEGLView::sharedOpenGLView();
	return view ? static_cast<void*>(view->getWindow()) : nullptr;
#else
	return nullptr;
#endif
}

// 우리 셰이더 프로그램이 지금 컨텍스트에서도 살아 있는지.
// 번호만 보면 안 된다: 새 컨텍스트에서 GD가 같은 번호로 다른 프로그램을 만들었을 수 있으므로
// 우리 셰이더에만 있는 uniform 위치까지 일치하는지 본다.
bool programStillOurs() {
	auto id = g_program ? g_program->getProgram() : 0;
	if (!id || !glIsProgram(id)) return false;
	return glGetUniformLocation(id, "u_spin") == g_u.spin && glGetUniformLocation(id, "u_mask") == g_u.mask;
}

// GL 컨텍스트가 바뀌었을 때 다시 만든다.
// 이전 컨텍스트의 텍스쳐 객체(마스크, 잡아 둔 단독 이미지 텍스쳐)는 절대 해제하지 않고 버린다.
// 해제하면 소멸자가 glDeleteTextures(옛 번호)를 새 컨텍스트에서 불러, 그 번호를 새로 받은 GD 텍스쳐
// (폰트, 로딩 바 등)를 지운다. 전체 화면을 반복 전환할 때 화면이 깨지던 원인 (v0.3.3까지).
// 버리는 객체는 GPU 메모리가 이미 컨텍스트와 함께 사라졌으므로 작은 CPU 객체만 남는다.
void rebuildGL(char const* reason, bool contextLost) {
	log::info("GL reset detected ({}): rebuilding theme shader{}", reason, contextLost ? " and masks" : "");
	if (g_program) {
		g_program->reset();   // 옛 프로그램 번호를 지우지 않고 잊기만 한다
		if (!initProgram(g_program)) g_programFailed = true;
	}
	if (contextLost) {
		for (auto& pack : g_packs) {
			for (auto& sheet : pack.sheets) sheet.mask = sheet.sdf = nullptr;
			for (auto& single : pack.singles) single.mask = single.sdf = nullptr;
		}
		for (auto& sheet : g_addons) sheet.mask = sheet.sdf = nullptr;   // 다음 연결 때 bindAddons가 다시 만든다
		if (g_external) {
			for (auto& sheet : g_external->sheets) sheet.mask = sheet.sdf = nullptr;
			for (auto& single : g_external->singles) single.mask = single.sdf = nullptr;
		}
		glass::onContextLost();
		g_loaded.clear();   // retain해 둔 옛 텍스쳐도 release하지 않고 버린다
		g_bound.clear();
		g_boundSingles.clear();
		if (g_active) loadMasks(*g_active);
		// 비활성 팩의 마스크는 그 팩이 켜질 때 loadMasks가 다시 만든다
	}
	g_lastRefresh = ~0u;
}

// 프레임당 한 번: 창이 바뀌었거나 우리 프로그램이 더 이상 유효하지 않으면 복구
void checkGL(bool force) {
	auto frame = frameCount();
	if (!force && frame == g_lastGLCheck) return;
	g_lastGLCheck = frame;
	auto window = currentWindow();
	if (!g_window) g_window = window;
	bool windowChanged = window && window != g_window;
	if (windowChanged) g_window = window;
	if (!g_program || g_programFailed) return;

	if (programStillOurs()) {
		if (windowChanged) {
			// 창만 새로 만들고 GL 컨텍스트는 유지(공유)된 경우: 마스크/셰이더는 그대로 유효하다.
			// 여기서 마스크를 다시 만들면 전환할 때마다 수십 MB씩 GPU 메모리가 샌다.
			log::info("window recreated, GL context kept: theme resources still valid");
			g_bound.clear();
			g_boundSingles.clear();
			g_lastRefresh = ~0u;   // GD가 시트 텍스쳐는 새로 만들었을 수 있다
		}
		return;
	}
	rebuildGL(windowChanged ? "window recreated" : "shader program lost", true);
}

} // namespace

CCGLProgram* program() {
	if (g_program || g_programFailed) {
		if (g_program) checkGL(false);
		return g_programFailed ? nullptr : g_program;
	}
	auto p = new CCGLProgram();
	if (!initProgram(p)) {
		p->release();
		g_programFailed = true;
		return nullptr;
	}
	CCShaderCache::sharedShaderCache()->addProgram(p, PROGRAM_KEY);
	// new로 받은 참조는 놓지 않고 우리가 계속 쥔다. GD는 전체 화면 전환/그래픽 설정 변경 때 셰이더 캐시를
	// 비우고 화면을 다시 만드는데, 캐시에만 맡기면 그때 객체가 해제되어 g_program이 해제된 메모리를 가리킨다
	// (v0.2.0 크래시: CCGLProgram::updateUniformLocation에서 0xFFFFFFFFFFFFFFFF 읽기).
	g_program = p;
	g_window = currentWindow();
	log::info("theme shader ready");
	return g_program;
}

void onShadersReloaded() {
	// GD가 텍스쳐도 다시 만들므로 연결표를 비우고 다음 그리기에서 새로 찾는다
	// (해제된 시트 텍스쳐 주소를 새 텍스쳐가 재사용하면 엉뚱한 마스크가 붙을 수 있다)
	g_bound.clear();
	g_boundSingles.clear();
	g_lastRefresh = ~0u;
	if (g_program) checkGL(true);
}

bool enabled() {
	auto frame = frameCount();
	if (g_lastSettings == ~0u || frame - g_lastSettings >= SETTINGS_FRAMES) {
		g_lastSettings = frame;
		readSettings();
	}
	return g_settings.enabled && !g_packs.empty();
}

Settings const& current() {
	g_lastSettings = ~0u;   // 방금 바꾼 설정도 반영되도록 바로 다시 읽는다
	enabled();
	return g_settings;
}

void onTextureLoaded(char const* path, CCTexture2D* texture) {
	if (!path || !texture) return;
	auto name = baseName(path);
	if (!g_singleNames.contains(name)) return;
	{
		std::lock_guard lock(g_pendingMutex);
		g_pending.emplace_back(std::move(name), texture);
	}
	g_lastRefresh = ~0u;   // 다음 그리기에서 바로 다시 연결 (팝업이 처음 열릴 때 1초 늦게 칠해지지 않도록)
}

Stats& stats() {
	return g_stats;
}

bool statsEnabled() {
	return g_settings.debugStats;
}

void tickStats() {
	if (!g_settings.debugStats) return;
	static unsigned lastFrame = ~0u;
	auto frame = frameCount();
	if (frame == lastFrame) return;   // 훅마다 불리지만 프레임당 한 번만
	lastFrame = frame;
	auto now = seconds();
	if (g_statsSince == 0) {
		g_statsSince = now;
		g_stats = {};
		return;
	}
	g_stats.frames++;
	g_stats.worstFrame = std::max<double>(g_stats.worstFrame, CCDirector::sharedDirector()->getDeltaTime());
	// 직전 프레임에 우리 훅이 쓴 시간 (프레임의 첫 훅에서 재므로 한 프레임씩 밀려 있다)
	static double hookAtFrameStart = 0;
	g_stats.hookFrameMax = std::max(g_stats.hookFrameMax, g_stats.hookSeconds - hookAtFrameStart);
	hookAtFrameStart = g_stats.hookSeconds;
	auto elapsed = now - g_statsSince;
	if (elapsed < 5.f) return;
	auto f = std::max<double>(g_stats.frames, 1);
	// 비교하기 쉽게 장면/프리셋도 같이 남긴다
	auto scene = CCDirector::sharedDirector()->getRunningScene();
	auto top = scene && scene->getChildrenCount() ? static_cast<CCNode*>(scene->getChildren()->objectAtIndex(0)) : nullptr;
	log::info("[stats] {:.1f} fps (worst {:.1f} ms) | per frame: sprites {:.0f} (themed {:.0f}), batches {:.0f} "
	          "(themed {:.0f}, labels {:.0f}) | hook cost {:.1f} us/frame | glass: {:.2f} builds, {:.1f} draws, "
	          "{:.1f} list rows | invert 2nd pass {:.1f} | pack: {} | preset: {} | theme {} | scene: {}",
	          g_stats.frames / elapsed, g_stats.worstFrame * 1000, g_stats.spriteDraws / f, g_stats.spriteThemed / f,
	          g_stats.batchDraws / f, g_stats.batchThemed / f, g_stats.labelThemed / f, g_stats.hookSeconds * 1e6 / f,
	          g_stats.glassBuilds / f, g_stats.glassDraws / f, g_stats.listGlassDraws / f, g_stats.invertDraws / f,
	          g_active ? g_active->name : "none", Mod::get()->getSettingValue<std::string>("preset"),
	          g_settings.enabled ? "on" : "off", top ? geode::cocos::getObjectName(top) : "?");
	// 끊김 원인 구분: 우리 훅의 최악 프레임 / 팩 확인 최댓값 vs GD가 텍스쳐를 디스크에서 읽은 시간
	log::info("[stats] spikes: our hooks max {:.2f} ms/frame, pack refresh max {:.2f} ms | GD texture loads: {:.0f} "
	          "({:.1f} ms total, max {:.1f} ms)", g_stats.hookFrameMax * 1000, g_stats.refreshMax * 1000,
	          g_stats.textureLoads, g_stats.textureLoad * 1000, g_stats.textureLoadMax * 1000);
	g_stats = {};
	g_statsSince = now;
}

FrameInfo const* lookupSingle(CCTexture2D* texture, Textures* out) {
	refreshIfDue();
	auto it = g_boundSingles.find(texture);
	if (it == g_boundSingles.end() || !it->second->mask) return nullptr;
	// 주소 재사용 방어: 크기가 다르면 다른 텍스쳐다
	if (!sizeIs(texture, it->second->width, it->second->height)) return nullptr;
	*out = {it->second->mask, it->second->sdf};
	return &it->second->frame;
}

int fontMode(CCLabelBMFont* label, ccColor3B& colorOut) {
	auto const& s = g_settings;
	if (!s.bigFont && !s.goldFont && !s.invText) return 0;
	// GD가 일부러 색을 입힌 라벨(초록 "Online" 등)은 건드리지 않는다
	auto c = label->getColor();
	if (c.r != 255 || c.g != 255 || c.b != 255) return 0;
	std::string_view fnt = label->getFntFile();
	int mode = -1;
	if (fnt.find("bigFont") != std::string_view::npos) {
		colorOut = s.textColor;
		mode = s.bigFont;
	}
	else if (fnt.find("goldFont") != std::string_view::npos) {
		colorOut = s.goldColor;
		mode = s.goldFont;
	}
	if (mode < 0) return 0;
	// 색은 원본 그대로 두고 배경 반전만 쓰는 라벨 = 3
	if (mode == 0 && s.invText) return 3;
	return mode;
}

void applySprite(FrameInfo const& frame, Textures const& tex, Fx const& fx) {
	applyCommon(frame, tex, 0.f, nullptr, 0.f, 0.f, fx);
}

void applyBatch(FrameInfo const& frame, Textures const& tex, float const bounds[4], Fx const& fx) {
	applyCommon(frame, tex, 1.f, bounds, 0.f, 0.f, fx);
}

void applyText(float const bounds[4], int mode, ccColor3B color, Fx const& fx) {
	applyCommon(FULL_FRAME, Textures{}, 1.f, bounds, 1.f, static_cast<float>(mode), fx);
	set3(g_u.textColor, color);
}

void applyPlain(float const rect[4], Fx const& fx) {
	FrameInfo f = FULL_FRAME;
	std::copy(rect, rect + 4, f.rect);
	applyCommon(f, Textures{}, 0.f, nullptr, 0.f, 0.f, fx);
}

namespace {

// 노드별 효과 상태. 노드 포인터를 키로 쓰지만 역참조하지 않는다 (해제된 노드 주소가 재사용되면
// 빛남 값이 잠깐 이어질 뿐). 오래 안 그려진 항목은 주기적으로 지운다.
struct NodeFx {
	float glow = 0;
	float hover = 0;
	float invert = 0;
	float phase = 0;
	unsigned lastFrame = 0;
};
std::unordered_map<CCNode*, NodeFx> g_nodeFx;
unsigned g_lastPrune = 0;

// 위로 몇 단계 안에 있는 버튼. 9분할 버튼은 배치 노드 → CCScale9Sprite → ButtonSprite → 메뉴 아이템 순이라 4단계까지 본다.
// 그리는 중에는 조상 노드가 살아 있으므로 매번 새로 찾는다 (포인터를 저장하지 않는다).
CCMenuItem* findMenuItem(CCNode* node) {
	auto n = node;
	for (int i = 0; i < 5 && n; ++i, n = n->getParent()) {
		if (auto item = typeinfo_cast<CCMenuItem*>(n)) return item;
	}
	return nullptr;
}

bool hovered(CCMenuItem* item) {
	if (!item->isEnabled()) return false;
	auto p = item->convertToNodeSpace(geode::cocos::getMousePos());
	auto size = item->getContentSize();
	return p.x >= 0 && p.y >= 0 && p.x <= size.width && p.y <= size.height;
}

// 켜진 버튼: 토글(CCMenuItemToggler — 체크박스, 설정 토글, Geode 탭 등)의 "켜짐" 이미지 버튼.
// 토글은 켜짐/꺼짐 이미지를 각각 버튼으로 들고 있다가 상태에 맞는 쪽만 보이게 하므로,
// 지금 그려지는 버튼이 켜짐 쪽이면 켜진 상태다.
bool isActive(CCMenuItem* item) {
	auto toggler = typeinfo_cast<CCMenuItemToggler*>(item->getParent());
	return toggler && toggler->m_onButton == item && toggler->m_toggled;
}

}

float g_invertSeen = -10.f;   // 색 반전 누름 버튼이 마지막으로 눌려 있던 시각

bool invertWindow() {
	// 손을 뗀 뒤에도 반전이 풀리는 동안(약 0.4초)은 계속 계산한다
	return g_settings.pressStyle == 4 && seconds() - g_invertSeen < 0.6f;
}

void notePress() {
	g_invertSeen = seconds();
}

Fx fxFor(CCNode* node) {
	auto const& s = g_settings;
	bool wantActive = s.activeMode || s.activeFill || !s.activeAdj.identity();
	bool wantGlow = s.hoverGlow > 0 || s.idle != 0 || s.pressStyle == 2 || s.hoverHue != 0;
	bool wantInvert = invertWindow();
	bool wantSquish = s.pressStyle == 5 && s.glass > 0;
	if (!wantGlow && !wantActive && !wantInvert && !wantSquish) return {};
	auto item = findMenuItem(node);
	if (!item) return {};
	float active = wantActive && isActive(item) ? 1.f : 0.f;
	float squish = 0;
	if (wantSquish) {
		if (auto extra = typeinfo_cast<CCMenuItemSpriteExtra*>(item); extra && extra->getNormalImage()) {
			auto image = extra->getNormalImage();
			float sx = image->getScaleX(), sy = image->getScaleY();
			float avg = (std::abs(sx) + std::abs(sy)) * 0.5f;
			if (avg > 0.001f) squish = std::abs(std::abs(sx) - std::abs(sy)) / avg;
		}
	}
	if (!wantGlow && !wantInvert) {
		Fx fx;
		fx.active = active;
		fx.squish = squish;
		return fx;
	}

	auto frame = frameCount();
	auto& st = g_nodeFx[node];
	bool fresh = st.lastFrame == 0 || frame - st.lastFrame > 30;
	if (fresh) {
		st.glow = 0;
		st.hover = 0;
		st.invert = 0;
		// 버튼마다 다른 박자 (주소 기반 해시)
		st.phase = static_cast<float>((reinterpret_cast<uintptr_t>(item) >> 4) % 997) / 997.f;
	}
	if (st.lastFrame != frame) {
		float target = (s.hoverGlow > 0 && hovered(item)) ? s.hoverGlow : 0.f;
		if (s.pressStyle == 2 && item->isSelected()) target = std::max(target, 0.9f);
		float dt = std::min(CCDirector::sharedDirector()->getDeltaTime(), 0.1f);
		st.glow += (target - st.glow) * std::min(dt * 12.f, 1.f);
		st.hover += ((hovered(item) ? 1.f : 0.f) - st.hover) * std::min(dt * 10.f, 1.f);
		// 색 반전: 누르면 빠르게 뒤집히고, 떼면 조금 천천히 돌아온다
		bool held = s.pressStyle == 4 && item->isSelected();
		if (held) g_invertSeen = seconds();
		st.invert += ((held ? 1.f : 0.f) - st.invert) * std::min(dt * (held ? 30.f : 9.f), 1.f);
		st.lastFrame = frame;
	}

	// 오래 안 그려진 노드 정리
	if (frame - g_lastPrune > 300) {
		g_lastPrune = frame;
		std::erase_if(g_nodeFx, [frame](auto const& kv) { return frame - kv.second.lastFrame > 120; });
	}

	Fx fx;
	fx.glow = st.glow;
	fx.hover = st.hover;
	fx.phase = st.phase;
	fx.active = active;
	fx.invert = s.pressStyle == 4 && st.invert > 0.002f ? st.invert : 0.f;
	fx.squish = squish;
	if (s.idle == 1) fx.glow += 0.22f * (0.5f + 0.5f * std::sin(seconds() * 2.5f + st.phase * 6.2832f));
	if (s.idle == 2) fx.shine = 1.f;
	return fx;
}

bool invertsParts() {
	auto const& s = g_settings;
	return s.invOutline || s.invIcon || s.invFill;
}

bool invertsText() {
	return g_settings.invText;
}

void setPass(float pass) {
	if (g_program) g_program->setUniformLocationWith1f(g_u.pass, pass);
}

bool prepareGlass(CCNode* node, FrameInfo const& frame) {
	auto const& s = g_settings;
	if (s.glass <= 0) return false;
	// 유리는 배경을 직접 깔므로 아이콘 없는 버튼도 사라지지 않는다 → 바탕 투명도와 범위를 따로 둔다.
	// 기본은 버튼(메뉴 아이템 안)만: 팝업 판, 상단 바, 모서리 장식은 그대로
	bool listFrame = s.glassListFrames && frame.kind > 0.5f && frame.kind < 1.5f;
	if (!listFrame) {
		// 분할 없는 버튼/팩에는 아이콘이 따로 없다 → "아이콘 있는 버튼만" 범위에서 빠진다 (팩 안에서 일관되게)
		bool packWhole = g_active && g_active->profile == "whole";
		if (s.glassScope == 1 && (frame.style[1] < 0.5f || frame.kind > 1.5f || packWhole)) return false;
		if (s.glassScope != 2 && !findMenuItem(node)) return false;
	}
	return glass::prepare(node, s.glassBlur, s.glassBlurSize);
}

float listOpacity() {
	return enabled() ? std::clamp(g_settings.listOpacity, 0.f, 1.f) : 1.f;
}

CCGLProgram* prepareListGlass(CCNode* node) {
	auto const& s = g_settings;
	if (!enabled() || s.listGlass <= 0) return nullptr;
	if (!glass::prepare(node, s.glassBlur, s.glassBlurSize)) return nullptr;
	return glass::colorProgram(s.listGlass, s.glassBlur, s.glassSaturation, s.glassNoise);
}

float jellyStrength() {
	return g_settings.jelly;
}

int pressStyle() {
	if (!enabled() || !g_active) return 0;
	return g_settings.pressStyle;
}

bool listColor(int role, ccColor3B& out) {
	// 리스트 색은 텍스쳐와 무관하므로 지원 팩이 없어도 테마가 켜져 있으면 적용한다
	if (!enabled() || g_settings.listMode == 0) return false;
	out = role == 1 ? g_settings.list1 : g_settings.list2;   // "어둡게"는 기본값 그대로
	return true;
}

FrameInfo const* lookup(CCTexture2D* texture, ccV3F_C4B_T2F_Quad const& quad, Textures* out) {
	refreshIfDue();
	auto it = g_bound.find(texture);
	if (it == g_bound.end() || !it->second->mask) {
		return lookupSingle(texture, out);   // 슬라이더 손잡이 등 단독 이미지 스프라이트
	}
	auto* sheet = it->second;
	if (!sizeIs(texture, sheet->width, sheet->height)) return nullptr;   // 주소 재사용 방어
	if (sheet->addon && !g_settings.geodeButtons) return nullptr;

	// 쿼드의 텍스쳐 좌표 최솟값 = 프레임의 시트 안 좌상단 (회전 여부와 무관)
	float u = std::min({quad.tl.texCoords.u, quad.tr.texCoords.u, quad.bl.texCoords.u, quad.br.texCoords.u});
	float v = std::min({quad.tl.texCoords.v, quad.tr.texCoords.v, quad.bl.texCoords.v, quad.br.texCoords.v});
	auto x = static_cast<uint32_t>(std::floor(u * sheet->width + 0.01f));
	auto y = static_cast<uint32_t>(std::floor(v * sheet->height + 0.01f));
	auto f = sheet->byOrigin.find((x << 16) | y);
	if (f == sheet->byOrigin.end()) return nullptr;
	*out = {sheet->mask, sheet->sdf};
	return &f->second;
}

}
