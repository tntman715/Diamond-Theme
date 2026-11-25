'use strict';
// Diamond Theme Preview
// 원본 시트 + 마스크 시트(R=테두리, G=채우기, B=글리프, 알파 premultiplied)를 셰이더로 다시 칠한다.
// 좌표: 장면은 GD 포인트(569x320, y-up), 시트 보기는 시트 픽셀(y-down). UHD 4px = 1pt.

const PT = 4;
const DESIGN = { w: 569, h: 320 };
const STORAGE_KEY = 'diamond-theme-preview';

// 테마 설정의 기본 틀. 프리셋은 이 위에 덮어쓴다.
const DIAMOND = {
  gradMode: 0, hueStart: 120, hueRange: 240, sat: 0.4, val: 1,
  c1: '#99ffc7', c2: '#c399ff', c3: '#ff99b3', use3: true,
  angle: 0, flow: 0, spin: 0, space: 0, outlineSrc: 0,
  fillMode: 0, fill: '#141414', fill2: '#302840', fillAngle: 90, fillTheme: 0.25, opacity: 1, fillOpacity: 1,
  accent: '#ffffff', glyphMode: 0, glyphColor: '#ffffff', whiteOutline: 0,
  textMode: 0, goldMode: 0, textColor: '#ffffff', charFx: 'none',
  pressFx: 'gd', hoverGlow: 0.35, idleFx: 'none', bg: '#22306e',
  // 색 조정 (위에서 정한 색 기준 상대값): 색상 회전(도), 채도/밝기 배율. adj* = 전체, 부위별 값은 그 위에 더해진다
  // *Mode: 0 회전, 1 색상 고정 (색상화)
  adjHue: 0, adjSat: 1, adjVal: 1, adjMode: 0,
  adjOutlineHue: 0, adjOutlineSat: 1, adjOutlineVal: 1, adjOutlineMode: 0,
  adjFillHue: 0, adjFillSat: 1, adjFillVal: 1, adjFillMode: 0,
  adjIconHue: 0, adjIconSat: 1, adjIconVal: 1, adjIconMode: 0,
  adjColorizeGray: 0,
  adjHueSpeed: 0,       // 색조 애니메이션 (도/초)   // 색상 고정할 때 무채색에도 입힐 최소 채도
  // 색상 범위별 조정 (원래 색상 기준, 포토샵 색조/채도의 채널)
  rngRedsHue: 0, rngRedsSat: 1, rngRedsVal: 1, rngYellowsHue: 0, rngYellowsSat: 1, rngYellowsVal: 1,
  rngGreensHue: 0, rngGreensSat: 1, rngGreensVal: 1, rngCyansHue: 0, rngCyansSat: 1, rngCyansVal: 1,
  rngBluesHue: 0, rngBluesSat: 1, rngBluesVal: 1, rngMagentasHue: 0, rngMagentasSat: 1, rngMagentasVal: 1,
};

// 색 조정 부위: 테마 파일 키 ↔ 미리보기 키 접두사
const ADJUST_PARTS = [['', 'adj'], ['outline', 'adjOutline'], ['fill', 'adjFill'], ['icon', 'adjIcon']];
// 색상 범위: 테마 파일 키 ↔ 미리보기 키 접두사 (셰이더 u_range 순서)
const ADJUST_RANGES = ['reds', 'yellows', 'greens', 'cyans', 'blues', 'magentas']
  .map(r => [r, 'rng' + r[0].toUpperCase() + r.slice(1)]);

// 팩 프로필별 "원본" 테마. generic 팩(Diamond Vortex 등)은 그라데이션이 공식으로 표현되지 않으므로
// 테두리/글리프를 원본 색 그대로 두는 것이 원본 재현이다.
const PROFILE_DEFAULTS = {
  diamond: {},
  generic: { outlineSrc: 1, glyphMode: 4, bg: '#1b1b24' },
  // 바닐라 구조: 테두리/아이콘/바탕 모두 원본 색이 원본 재현
  vanilla: { outlineSrc: 1, glyphMode: 4, bg: '#2a4a8a' },
  // 광택 팩: 바닐라와 같은 판정 규칙 (색/밝기 경계로 나눈 것만 다르다)
  glossy: { outlineSrc: 1, glyphMode: 4, bg: '#2a4a8a' },
  // 분할 없음: 버튼 그림 전체가 원래 색의 바탕 하나
  whole: { outlineSrc: 1, glyphMode: 4, bg: '#2a4a8a' },
};

let pack = null;     // 현재 팩 (packs.json 항목)
const packDefault = () => ({ ...DIAMOND, ...(PROFILE_DEFAULTS[pack.profile] || {}) });

// 함수 값은 현재 팩의 원본 테마를 받아 프리셋을 만든다
const PRESETS = {
  '원본': null,
  // 원본 색 그대로 움직이기.
  // generic(Vortex): 그라데이션이 요소마다 대각선/세로라 색상 회전만으로 소용돌이처럼 보인다.
  // diamond: 그라데이션이 정확히 가로라 색상 회전은 옆으로 미끄러져 보인다. 원본과 같은 공식(기본 테마)의
  //          그라데이션 방향을 돌려서 회전 움직임을 만든다.
  '원본 흐름': base => (pack.profile === 'diamond'
    ? { ...base, spin: 0.12 }
    : { ...base, outlineSrc: 1, glyphMode: 4, flow: 0.1 }),
  // Diamond Vortex 원본 분위기: 왼쪽 위 빨강→주황→노랑→초록 대각선, 채도 0.52 / 밝기 0.93 (Vortex 버튼에서 측정)
  '볼텍스': base => ({ ...base, outlineSrc: 0, gradMode: 0, hueStart: 340, hueRange: 220, sat: 0.52, val: 0.93,
                      angle: 45, flow: 0.05, spin: 0.08, glyphMode: 2 }),
  '네온 플로우': { hueStart: 0, hueRange: 360, sat: 0.85, flow: 0.12, space: 1, idleFx: 'pulse', textMode: 1, bg: '#0b0b14' },
  '선셋': { gradMode: 1, c1: '#ffc36b', c2: '#ff6b9d', c3: '#9b6bff', fillMode: 1, fill: '#1c1020', textMode: 1, goldMode: 1, bg: '#2a1530' },
  '민트 글래스': { hueStart: 150, hueRange: 70, sat: 0.35, fillMode: 1, fill: '#0e1d1b', glyphMode: 2, bg: '#123a37' },
  '모노': { gradMode: 2, c1: '#ffffff', fillMode: 1, fill: '#101010', glyphMode: 1, textMode: 2, goldMode: 2, bg: '#2b2b2b' },
  '라이트': { sat: 0.6, val: 0.92, fillMode: 1, fill: '#f3f3f7', accent: '#2a2a33', glyphMode: 3, glyphColor: '#3a3a48',
              textMode: 2, goldMode: 1, textColor: '#2a2a33', bg: '#9fb4e6' },
};

let theme = { ...DIAMOND };

function storedTheme(id) {
  try { return JSON.parse(localStorage.getItem(`${STORAGE_KEY}:${id}`) || 'null'); } catch (e) { return null; }
}

// ------------------------------------------------------------------ WebGL

const canvas = document.getElementById('gl');
let gl = canvas.getContext('webgl2', { premultipliedAlpha: true, antialias: true });
const isGL2 = !!gl;
if (!gl) gl = canvas.getContext('webgl', { premultipliedAlpha: true, antialias: true });

const VS = `
attribute vec2 a_pos; attribute vec2 a_uv; attribute vec2 a_local; attribute vec4 a_style; attribute float a_fill;
attribute vec4 a_rect;
uniform mat3 u_view;
varying vec2 v_uv; varying vec2 v_local; varying vec4 v_style; varying float v_fill; varying vec4 v_rect;
void main() {
  vec3 p = u_view * vec3(a_pos, 1.0);
  gl_Position = vec4(p.xy, 0.0, 1.0);
  v_uv = a_uv; v_local = a_local; v_style = a_style; v_fill = a_fill; v_rect = a_rect;
}`;

// v_style: x = 모드(0 마스크 텍스쳐, 1 텍스트, 2 원본 유지, 3 단색), y = 테두리 스타일, z = 글리프 스타일 (1 그라데이션, 2 흰색), w = 빛남
const FS = `
precision highp float;
uniform sampler2D u_tex; uniform sampler2D u_mask;
uniform float u_time; uniform vec2 u_res; uniform float u_split;
uniform float u_gradMode, u_hueStart, u_hueRange, u_sat, u_val, u_angle, u_flow, u_space, u_use3, u_spin;
uniform vec3 u_c1, u_c2, u_c3, u_fill, u_accent, u_glyphColor, u_textColor;
uniform float u_glyphMode, u_whiteOutline, u_textMode, u_fillMode, u_outlineSrc;
uniform vec3 u_fill2; uniform float u_fillAngle, u_fillTheme, u_opacity, u_fillOpacity;
uniform float u_adjOn, u_rangeOn, u_colorizeGray; uniform vec4 u_adjKeep, u_adjOutline, u_adjFill, u_adjIcon; uniform vec3 u_range[6];
varying vec2 v_uv; varying vec2 v_local; varying vec4 v_style; varying float v_fill; varying vec4 v_rect;

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

// 원본 색의 채도/밝기와 공간 배치는 그대로 두고 색상만 시간에 따라 돌린다 (원본 그라데이션이 흐르는 효과)
vec3 hueShift(vec3 c, float turns) {
  vec3 h = rgb2hsv(c);
  h.x = fract(h.x + turns);
  return hsv2rgb(h);
}

// 색 조정: k = (색상(바퀴), 채도 배율, 밝기 배율, 1 색상 고정 / 0 회전) — 인게임 셰이더와 같은 공식
// 색상 범위 조정을 먼저 (원래 색상으로 6개 범위를 부드럽게 고른다, 회색에 가까우면 약하게)
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
  if (k.w > 0.5) {
    // 색상 고정: HSL 명도 유지 (흰색/검은색은 그대로), 무채색은 u_colorizeGray만큼 채도를 올린다 — 인게임과 같은 공식
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
  // 회전: 그라데이션 방향 자체가 요소 중심을 기준으로 돈다 (색상 흐름은 옆으로 미끄러지는 느낌, 회전은 소용돌이 느낌)
  float a = radians(u_angle + u_time * u_spin * 360.0);
  vec2 d = vec2(cos(a), sin(a));
  float u = dot(loc - 0.5, d) / (abs(d.x) + abs(d.y)) + 0.5;
  if (u_gradMode < 0.5) {
    float h = (u_hueStart + u_hueRange * u) / 360.0 + u_time * u_flow;
    return hsv2rgb(vec3(fract(h), u_sat, u_val));
  }
  if (u_gradMode < 1.5) {
    float p = 1.0 - abs(1.0 - fract((u + u_time * u_flow) * 0.5) * 2.0);
    if (u_use3 > 0.5) return p < 0.5 ? mix(u_c1, u_c2, p * 2.0) : mix(u_c2, u_c3, p * 2.0 - 1.0);
    return mix(u_c1, u_c2, p);
  }
  return u_c1;
}

void main() {
  float mode = v_style.x;
  if (mode > 2.5) { gl_FragColor = vec4(v_style.yzw * v_local.x, v_local.x); return; }
  // 프레임 영역 밖(옆 스프라이트)을 샘플링하지 않도록 반 텍셀 안쪽으로 고정. 시트에 여백 없이 붙어 있어서
  // 이걸 안 하면 쿼드 가장자리에 옆 스프라이트가 선처럼 묻어난다.
  vec2 uv = clamp(v_uv, v_rect.xy, v_rect.zw);
  vec4 base = texture2D(u_tex, uv);
  float a = base.a;
  bool orig = (u_split >= 0.0 && gl_FragCoord.x < u_split) || (mode > 1.5);
  if (orig || a < 0.002) { gl_FragColor = base; return; }

  vec3 straight = base.rgb / a;
  vec2 loc = u_space > 0.5 ? vec2(gl_FragCoord.x / u_res.x, 1.0 - gl_FragCoord.y / u_res.y) : v_local;
  vec3 grad = gradAt(loc);
  vec3 col;
  float alphaK = 1.0;   // 투명도 (바탕만 / 버튼 전체)
  if (mode > 0.5) {
    col = u_textMode < 0.5 ? straight : (u_textMode < 1.5 ? grad : u_textColor);
  } else {
    vec3 m = clamp(texture2D(u_mask, uv).rgb / a, 0.0, 1.0);
    float keep = clamp(1.0 - m.r - m.g - m.b, 0.0, 1.0);
    // 원본 색: 픽셀에서 원래 바탕색 비율만큼 빼고 남은 색 (바탕만 바꾸고 아이콘 색은 그대로 둘 때)
    // v_fill < 0: 바탕이 단색이 아니다 (바닐라 구조) → 원본 색 = 픽셀 그대로
    bool textured = v_fill < 0.0;
    vec3 nonFill = textured ? straight : clamp((straight - m.g * vec3(v_fill)) / max(1.0 - m.g, 0.05), 0.0, 1.0);
    nonFill = hueShift(nonFill, u_time * u_flow);
    vec3 outlineCol = u_outlineSrc > 0.5 ? nonFill
                    : (v_style.y > 1.5 && u_whiteOutline < 0.5) ? u_accent : grad;
    vec3 glyphCol = u_glyphMode < 0.5 ? (v_style.z > 1.5 ? u_accent : grad)
                  : u_glyphMode < 1.5 ? u_accent
                  : u_glyphMode < 2.5 ? grad
                  : u_glyphMode < 3.5 ? u_glyphColor : nonFill;
    // 바탕: 0 원본(프레임별), 1 직접 지정, 2 두 색 그라데이션, 3 테두리 그라데이션을 어둡게 (인게임과 같은 공식)
    vec3 fillCol = textured ? straight : vec3(v_fill);
    if (u_fillMode > 2.5) fillCol = grad * u_fillTheme;
    else if (u_fillMode > 1.5) {
      float fa = radians(u_fillAngle);
      vec2 fd = vec2(cos(fa), sin(fa));
      float fu = clamp(dot(v_local - 0.5, fd) / (abs(fd.x) + abs(fd.y)) + 0.5, 0.0, 1.0);
      fillCol = mix(u_fill, u_fill2, fu);
    }
    else if (u_fillMode > 0.5) fillCol = u_fill;
    // 분할 없는 프레임(whole, v_fill = -2): 바탕 = 그림 전체이므로 바탕색 설정 대신 항상 원래 그림 (인게임과 같다)
    if (v_fill < -1.5) fillCol = straight;
    vec3 keepCol = straight;
    if (u_adjOn > 0.5) {
      outlineCol = adjustCol(outlineCol, u_adjOutline);
      glyphCol = adjustCol(glyphCol, u_adjIcon);
      fillCol = adjustCol(fillCol, u_adjFill);
      keepCol = adjustCol(straight, u_adjKeep);
    }
    col = m.r * outlineCol + m.g * fillCol + m.b * glyphCol + keep * keepCol;
    alphaK = (keep + m.r + m.b + m.g * u_fillOpacity) * u_opacity;
    // w: 양수 = 빛남, 음수 = 누름 색 반전 정도
    float glow = max(v_style.w, 0.0);
    col += glow * (m.r + 0.35 * m.b) * outlineCol * 0.9 + glow * 0.06;
  }
  col = clamp(col, 0.0, 1.0);
  col = mix(col, 1.0 - col, max(-v_style.w, 0.0));
  float oa = a * alphaK;
  gl_FragColor = vec4(col * oa, oa);
}`;

function compile(type, src) {
  const s = gl.createShader(type);
  gl.shaderSource(s, src);
  gl.compileShader(s);
  if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
  return s;
}
const prog = gl.createProgram();
gl.attachShader(prog, compile(gl.VERTEX_SHADER, VS));
gl.attachShader(prog, compile(gl.FRAGMENT_SHADER, FS));
gl.linkProgram(prog);
if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(prog));
gl.useProgram(prog);

const U = {};
for (let i = 0, n = gl.getProgramParameter(prog, gl.ACTIVE_UNIFORMS); i < n; i++) {
  const name = gl.getActiveUniform(prog, i).name;
  U[name] = gl.getUniformLocation(prog, name);
}
gl.uniform1i(U.u_tex, 0);
gl.uniform1i(U.u_mask, 1);

const FLOATS = 15; // pos2 uv2 local2 style4 fill1 rect4
const vbo = gl.createBuffer();
gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
[['a_pos', 2, 0], ['a_uv', 2, 2], ['a_local', 2, 4], ['a_style', 4, 6], ['a_fill', 1, 10], ['a_rect', 4, 11]].forEach(([name, size, off]) => {
  const loc = gl.getAttribLocation(prog, name);
  gl.enableVertexAttribArray(loc);
  gl.vertexAttribPointer(loc, size, gl.FLOAT, false, FLOATS * 4, off * 4);
});
gl.enable(gl.BLEND);
gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);

let whiteTex = null;
function makeTexture(img, premultiply) {
  const t = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, t);
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, premultiply);
  gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.NONE);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  // 밉맵은 쓰지 않는다: 여백 없는 아틀라스라 축소 단계에서 옆 스프라이트가 섞인다 (게임도 밉맵 없이 그린다)
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
  return t;
}

// ------------------------------------------------------------------ 배치 렌더러

const batch = { data: new Float32Array(FLOATS * 6 * 8192), n: 0, key: null };

function flush() {
  if (!batch.n) return;
  gl.bufferData(gl.ARRAY_BUFFER, batch.data.subarray(0, batch.n * FLOATS), gl.DYNAMIC_DRAW);
  gl.drawArrays(gl.TRIANGLES, 0, batch.n);
  batch.n = 0;
}

// 텍스쳐/폰트가 바뀔 때마다 끊어서 그린다
function use(key) {
  if (batch.key === key) return;
  flush();
  batch.key = key;
  const tex = assets.textures[key], font = assets.fonts[key];
  gl.activeTexture(gl.TEXTURE0);
  gl.bindTexture(gl.TEXTURE_2D, tex ? tex.tex : font ? font.tex : whiteTex);
  gl.activeTexture(gl.TEXTURE1);
  gl.bindTexture(gl.TEXTURE_2D, tex ? tex.mask : whiteTex);
  gl.uniform1f(U.u_textMode, font ? +(font.name.startsWith('goldFont') ? theme.goldMode : theme.textMode) : 0);
}

const FULL_RECT = [0, 0, 1, 1];

// corners: tl, tr, br, bl 순서. 각각 [x, y, u, v, lx, ly]. fill = 원본 바탕색(회색 단계), rect = 샘플링 허용 UV 영역
function quad(c, style, fill = 0, rect = FULL_RECT) {
  if (batch.n + 6 > batch.data.length / FLOATS) flush();
  for (const i of [0, 1, 2, 0, 2, 3]) {
    const o = batch.n++ * FLOATS, p = c[i];
    batch.data.set(p, o);
    batch.data.set(style, o + 6);
    batch.data[o + 10] = fill;
    batch.data.set(rect, o + 11);
  }
}

// 원래 바탕색 (회색). "textured" = 바탕이 단색이 아님(바닐라 두 톤 바탕 등) → 셰이더에 -1로 넘겨 픽셀 원본 그대로
function frameFill(f, T) {
  if (f.whole) return -2;
  if (f.fill === 'textured') return -1;
  return f.fill ? hex(f.fill)[0] : T.fill[0];
}

// 텍셀 영역 [x, y, w, h] → 반 텍셀 안쪽으로 줄인 UV 사각형
function texelRect(x, y, w, h, W, H) {
  return [(x + 0.5) / W, (y + 0.5) / H, (x + w - 0.5) / W, (y + h - 0.5) / H];
}

function frameRect(f, T) {
  return texelRect(...f.rect, ...T.size);
}

function solidRect(x0, y0, x1, y1, rgb, alpha) {
  use('__solid');
  const st = [3, rgb[0], rgb[1], rgb[2]];
  quad([[x0, y1, 0, 0, alpha, 0], [x1, y1, 0, 0, alpha, 0], [x1, y0, 0, 0, alpha, 0], [x0, y0, 0, 0, alpha, 0]], st);
}

// ------------------------------------------------------------------ 에셋

const assets = { textures: {}, fonts: {}, frames: {} };
const plainOverrides = new Set();
const styleCode = s => (s === 'gradient' ? 1 : s === 'white' ? 2 : 0);

function hex(h) {
  const n = parseInt(h.slice(1), 16);
  return [(n >> 16 & 255) / 255, (n >> 8 & 255) / 255, (n & 255) / 255];
}

async function loadImage(url) {
  const img = new Image();
  img.src = url;
  await img.decode();
  return img;
}

// 팩을 바꿀 때 이전 팩 텍스쳐는 GPU에서 지운다 (시트 하나가 수십 MB라 두 팩을 같이 올려두지 않는다)
function freeAssets() {
  for (const T of Object.values(assets.textures)) { gl.deleteTexture(T.tex); gl.deleteTexture(T.mask); }
  for (const F of Object.values(assets.fonts)) gl.deleteTexture(F.tex);
  assets.textures = {}; assets.fonts = {}; assets.frames = {};
}

async function loadAssets(entry) {
  const status = document.getElementById('status');
  const base = `assets/${entry.id}/`;
  const atlas = await (await fetch(base + 'atlas.json')).json();
  const textures = {}, fonts = {}, frames = {};
  const jobs = [];
  let done = 0;
  const total = atlas.textures.length * 2 + atlas.fonts.length;
  const tick = () => { status.textContent = `${entry.name} 불러오는 중… ${++done}/${total}`; };

  for (const t of atlas.textures) {
    jobs.push(Promise.all([loadImage(base + t.image), loadImage(base + t.mask)]).then(([img, mask]) => {
      tick(); tick();
      textures[t.name] = { ...t, tex: makeTexture(img, true), mask: makeTexture(mask, false), fill: hex(t.fill) };
    }));
  }
  for (const f of atlas.fonts) {
    jobs.push(loadImage(base + f.image).then(img => {
      tick();
      fonts[f.name] = { ...f, tex: makeTexture(img, true), size: [img.width, img.height] };
    }));
  }
  await Promise.all(jobs);
  // 같은 프레임 이름이 여러 시트에 있으면 atlas 순서상 먼저 나온 것을 쓴다
  for (const t of atlas.textures) {
    for (const [fname, fr] of Object.entries(t.frames)) {
      if (!frames[fname]) frames[fname] = { tex: t.name, frame: fr };
    }
  }
  freeAssets();
  Object.assign(assets, { textures, fonts, frames });
  return atlas;
}

// ------------------------------------------------------------------ 그리기 도우미

// 프레임 내부 좌표(트림된 스프라이트 기준 0..1, y-down) → 시트 UV. 회전 프레임은 시계 방향 90°로 저장되어 있다.
function frameUV(f, T, lx, ly) {
  const [x, y, w, h] = f.rect;
  const px = f.rotated ? x + (1 - ly) * w : x + lx * w;
  const py = f.rotated ? y + lx * h : y + ly * h;
  return [px / T.size[0], py / T.size[1]];
}

function frameStyle(name, f, glow) {
  const mode = plainOverrides.has(name) ? 2 : 0;
  return [mode, styleCode(f.outline), styleCode(f.glyph), glow];
}

function drawSprite(name, cx, cy, scale, glow) {
  const ref = assets.frames[name];
  if (!ref) return;
  const T = assets.textures[ref.tex], f = ref.frame;
  use(ref.tex);
  const sw = f.sourceSize[0] / PT * scale, sh = f.sourceSize[1] / PT * scale;
  const w = f.size[0] / PT * scale, h = f.size[1] / PT * scale;
  const tcx = cx + f.offset[0] / PT * scale, tcy = cy + f.offset[1] / PT * scale;
  const nx0 = cx - sw / 2, ny1 = cy + sh / 2;
  const pts = [[tcx - w / 2, tcy + h / 2, 0, 0], [tcx + w / 2, tcy + h / 2, 1, 0],
               [tcx + w / 2, tcy - h / 2, 1, 1], [tcx - w / 2, tcy - h / 2, 0, 1]];
  quad(pts.map(([x, y, lx, ly]) => [x, y, ...frameUV(f, T, lx, ly), (x - nx0) / sw, (ny1 - y) / sh]),
       frameStyle(name, f, glow), frameFill(f, T), frameRect(f, T));
}

// 9분할: 모서리는 그대로, 가운데만 늘린다. 그라데이션 좌표는 노드 전체 기준이라 이음새가 없다.
function drawNine(name, cx, cy, w, h, inset, glow) {
  const ref = assets.frames[name];
  if (!ref) return;
  const T = assets.textures[ref.tex], f = ref.frame;
  use(ref.tex);
  const [W, H] = f.size, ip = inset / PT;
  const xs = [cx - w / 2, cx - w / 2 + ip, cx + w / 2 - ip, cx + w / 2];
  const ys = [cy + h / 2, cy + h / 2 - ip, cy - h / 2 + ip, cy - h / 2];
  const us = [0, inset / W, 1 - inset / W, 1], vs = [0, inset / H, 1 - inset / H, 1];
  const style = frameStyle(name, f, glow);
  const v = (i, j) => [xs[i], ys[j], ...frameUV(f, T, us[i], vs[j]), (xs[i] - xs[0]) / w, (ys[0] - ys[j]) / h];
  for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) {
    quad([v(i, j), v(i + 1, j), v(i + 1, j + 1), v(i, j + 1)], style, frameFill(f, T), frameRect(f, T));
  }
}

function measureLabel(F, text, s) {
  const lines = text.split('\n');
  const widths = lines.map(l => [...l].reduce((sum, ch) => sum + ((F.chars[ch.codePointAt(0)] || [])[6] || 0), 0) * s);
  return { lines, widths, w: Math.max(...widths), h: lines.length * F.lineHeight * s };
}

function drawLabel(fontName, text, cx, cy, scale, time) {
  const F = assets.fonts[fontName];
  if (!F) return;
  use(fontName);
  const s = scale / PT;
  const m = measureLabel(F, text, s);
  const bx0 = cx - m.w / 2, by1 = cy + m.h / 2;
  const [TW, TH] = F.size;
  let idx = 0;
  m.lines.forEach((line, li) => {
    let pen = cx - m.widths[li] / 2;
    const top = by1 - li * F.lineHeight * s;
    for (const ch of line) {
      const c = F.chars[ch.codePointAt(0)];
      if (!c) continue;
      const [x, y, w, h, xo, yo, xa] = c;
      let dy = 0;
      if (theme.charFx === 'wave') dy = Math.sin(time * 4 - idx * 0.55) * 2.5 * scale;
      if (theme.charFx === 'bounce') dy = Math.abs(Math.sin(time * 3 - idx * 0.35)) * 4 * scale;
      idx++;
      const x0 = pen + xo * s, y1 = top - yo * s + dy, x1 = x0 + w * s, y0 = y1 - h * s;
      const vtx = (X, Y, u, v) => [X, Y, u / TW, v / TH, (X - bx0) / m.w, (by1 - Y) / m.h];
      quad([vtx(x0, y1, x, y), vtx(x1, y1, x + w, y), vtx(x1, y0, x + w, y + h), vtx(x0, y0, x, y + h)], [1, 0, 0, 0],
           0, texelRect(x, y, w, h, TW, TH));
      pen += xa * s;
    }
  });
}

// ------------------------------------------------------------------ 장면 / 버튼 애니메이션

const EASE = {
  linear: t => t,
  quadOut: t => 1 - (1 - t) * (1 - t),
  backOut: t => { const c = 1.70158; return 1 + (c + 1) * Math.pow(t - 1, 3) + c * Math.pow(t - 1, 2); },
  bounceOut: t => {
    const n = 7.5625, d = 2.75;
    if (t < 1 / d) return n * t * t;
    if (t < 2 / d) return n * (t -= 1.5 / d) * t + 0.75;
    if (t < 2.5 / d) return n * (t -= 2.25 / d) * t + 0.9375;
    return n * (t -= 2.625 / d) * t + 0.984375;
  },
  elasticOut: t => (t === 0 || t === 1) ? t : Math.pow(2, -10 * t) * Math.sin((t * 10 - 0.75) * (2 * Math.PI) / 3) + 1,
};

// [누를 때 목표 배율, 시간, 이징], [뗄 때 시간, 이징]
const PRESS = {
  gd:     { down: [1.26, 0.3, 'bounceOut'], up: [0.4, 'bounceOut'] },
  squash: { down: [0.9, 0.08, 'quadOut'], up: [0.35, 'backOut'] },
  pop:    { down: [1.12, 0.12, 'quadOut'], up: [0.5, 'elasticOut'], glow: 0.9 },
  none:   { down: [1, 0.01, 'linear'], up: [0.01, 'linear'] },
  invert: { down: [1, 0.01, 'linear'], up: [0.01, 'linear'], invert: true },
};

function tweenScale(n, to, dur, ease, now) {
  n.tw = { from: n.scale, to, t0: now, dur, ease };
}

function button(node) {
  return { scale: 1, glow: 0, hover: false, pressed: false, phase: Math.random() * 6, btn: true, ...node };
}

function menuNodes(interactive = true) {
  const cx = DESIGN.w / 2;
  const b = n => (interactive ? button(n) : { scale: 1, glow: 0, ...n });
  return [
    { type: 'sprite', frame: 'GJ_logo_001.png', x: cx, y: 272, s: 1, scale: 1, glow: 0 },
    b({ type: 'sprite', frame: 'GJ_garageBtn_001.png', x: cx - 112, y: 165, s: 1 }),
    b({ type: 'sprite', frame: 'GJ_playBtn_001.png', x: cx, y: 165, s: 1 }),
    b({ type: 'sprite', frame: 'GJ_creatorBtn_001.png', x: cx + 112, y: 165, s: 1 }),
    b({ type: 'sprite', frame: 'GJ_achBtn_001.png', x: cx - 70, y: 45, s: 1 }),
    b({ type: 'sprite', frame: 'GJ_optionsBtn_001.png', x: cx, y: 45, s: 1 }),
    b({ type: 'sprite', frame: 'GJ_statsBtn_001.png', x: cx + 70, y: 45, s: 1 }),
    b({ type: 'sprite', frame: 'GJ_dailyRewardBtn_001.png', x: DESIGN.w - 45, y: 165, s: 0.9 }),
    b({ type: 'sprite', frame: 'GJ_moreGamesBtn_001.png', x: DESIGN.w - 45, y: 45, s: 0.8 }),
    b({ type: 'sprite', frame: 'GJ_profileButton_001.png', x: 45, y: 115, s: 1 }),
    b({ type: 'sprite', frame: 'GJ_closeBtn_001.png', x: 22, y: DESIGN.h - 22, s: 0.8 }),
  ];
}

function creatorNodes() {
  const grid = [
    ['GJ_createBtn_001.png', 'GJ_savedBtn_001.png', 'GJ_highscoreBtn_001.png', 'GJ_challengeBtn_001.png', 'GJ_versusBtn_001.png'],
    ['GJ_dailyBtn_001.png', 'GJ_weeklyBtn_001.png', 'GJ_eventBtn_001.png', 'GJ_gauntletsBtn_001.png', 'GJ_featuredBtn_001.png'],
    ['GJ_listsBtn_001.png', 'GJ_pathsBtn_001.png', 'GJ_mapPacksBtn_001.png', 'GJ_searchBtn_001.png', 'GJ_mapBtn_001.png'],
  ];
  const nodes = [button({ type: 'sprite', frame: 'GJ_arrow_01_001.png', x: 26, y: DESIGN.h - 24, s: 0.9 })];
  grid.forEach((row, r) => row.forEach((frame, c) => {
    nodes.push(button({ type: 'sprite', frame, x: DESIGN.w / 2 + (c - 2) * 84, y: 248 - r * 84, s: 0.68 }));
  }));
  return nodes;
}

function popupNodes() {
  const cx = DESIGN.w / 2, cy = DESIGN.h / 2;
  return [
    ...menuNodes(false),
    { type: 'overlay', alpha: 0.55, scale: 1, glow: 0 },
    { type: 'nine', frame: 'GJ_square01.png', x: cx, y: cy, w: 360, h: 230, inset: 64, scale: 1, glow: 0 },
    { type: 'label', font: 'goldFont-uhd', text: 'Diamond Pack', x: cx, y: cy + 82, s: 0.9, scale: 1 },
    { type: 'label', font: 'bigFont-uhd', text: 'Interactive UI Theme\nPreview build 0.1', x: cx, y: cy + 18, s: 0.5, scale: 1 },
    button({ type: 'nine', frame: 'GJ_button_01.png', x: cx - 62, y: cy - 70, w: 100, h: 36, inset: 40,
             label: { font: 'goldFont-uhd', text: 'OK', s: 0.75 } }),
    button({ type: 'nine', frame: 'GJ_button_02.png', x: cx + 62, y: cy - 70, w: 100, h: 36, inset: 40,
             label: { font: 'goldFont-uhd', text: 'Back', s: 0.75 } }),
    button({ type: 'sprite', frame: 'GJ_closeBtn_001.png', x: cx - 174, y: cy + 109, s: 0.85 }),
    button({ type: 'sprite', frame: 'GJ_infoIcon_001.png', x: cx + 164, y: cy + 99, s: 1 }),
  ];
}

const scenes = {};
let sceneName = 'menu';

function nodeBounds(n) {
  if (n.type === 'nine') return [n.w, n.h];
  const ref = assets.frames[n.frame];
  if (!ref) return [0, 0];
  return [ref.frame.sourceSize[0] / PT * n.s, ref.frame.sourceSize[1] / PT * n.s];
}

function updateNode(n, now, dt) {
  if (!n.btn) return;
  if (n.tw) {
    const k = Math.min((now - n.tw.t0) / n.tw.dur, 1);
    n.scale = n.tw.from + (n.tw.to - n.tw.from) * EASE[n.tw.ease](k);
    if (k >= 1) n.tw = null;
  }
  const P = PRESS[theme.pressFx] || PRESS.gd;
  let target = n.hover ? theme.hoverGlow : 0;
  if (n.pressed && P.glow) target = Math.max(target, P.glow);
  n.glow += (target - n.glow) * Math.min(dt * 12, 1);
  // 색 반전 (인게임과 같은 속도: 누르면 빠르게, 떼면 천천히)
  const inv = n.pressed && P.invert ? 1 : 0;
  n.inv = (n.inv || 0) + (inv - (n.inv || 0)) * Math.min(dt * (inv ? 30 : 9), 1);
}

function drawNode(n, now) {
  let scale = n.scale, glow = n.glow || 0;
  if (n.btn && theme.idleFx === 'pulse') glow += 0.22 * (0.5 + 0.5 * Math.sin(now * 2.5 + n.phase));
  if (n.btn && theme.idleFx === 'breath') scale *= 1 + 0.025 * Math.sin(now * 2 + n.phase);
  if (n.inv > 0.002) glow = -n.inv;   // 셰이더에 음수로 넘기면 색 반전
  if (n.type === 'sprite') drawSprite(n.frame, n.x, n.y, n.s * scale, glow);
  else if (n.type === 'nine') {
    drawNine(n.frame, n.x, n.y, n.w * scale, n.h * scale, n.inset, glow);
    if (n.label) drawLabel(n.label.font, n.label.text, n.x, n.y + 1, n.label.s * scale, now);
  } else if (n.type === 'label') drawLabel(n.font, n.text, n.x, n.y, n.s * scale, now);
  else if (n.type === 'overlay') solidRect(-2000, -2000, 4000, 4000, [0, 0, 0], n.alpha);
}

// ------------------------------------------------------------------ 시트 보기

const sheetView = { tex: 'GJ_GameSheet03-uhd', zoom: 0.2, x: 10, y: 10, hover: null };

function fitSheet() {
  const T = assets.textures[sheetView.tex];
  const cw = canvas.width, ch = canvas.height;
  sheetView.zoom = Math.min(cw / T.size[0], ch / T.size[1]) * 0.96;
  sheetView.x = (cw - T.size[0] * sheetView.zoom) / 2;
  sheetView.y = (ch - T.size[1] * sheetView.zoom) / 2;
}

function drawSheet() {
  const T = assets.textures[sheetView.tex];
  const [W, H] = T.size;
  solidRect(0, 0, W, H, [0.08, 0.08, 0.1], 1);
  use(sheetView.tex);
  for (const [name, f] of Object.entries(T.frames)) {
    const [x, y, w, h] = f.rect;
    const loc = (X, Y) => f.rotated ? [(Y - y) / h, 1 - (X - x) / w] : [(X - x) / w, (Y - y) / h];
    const v = (X, Y) => [X, Y, X / W, Y / H, ...loc(X, Y)];
    quad([v(x, y), v(x + w, y), v(x + w, y + h), v(x, y + h)], frameStyle(name, f, sheetView.hover === name ? 0.25 : 0),
         frameFill(f, T), frameRect(f, T));
  }
  if (sheetView.hover) {
    const f = T.frames[sheetView.hover];
    const [x, y, w, h] = f.rect, t = 2 / sheetView.zoom;
    const c = plainOverrides.has(sheetView.hover) ? [1, 0.4, 0.4] : [1, 1, 1];
    solidRect(x, y, x + w, y + t, c, 1); solidRect(x, y + h - t, x + w, y + h, c, 1);
    solidRect(x, y, x + t, y + h, c, 1); solidRect(x + w - t, y, x + w, y + h, c, 1);
  }
}

// ------------------------------------------------------------------ 메인 루프

const view = { scl: 1, ox: 0, oy: 0 };
let split = -1;
const splitLine = document.createElement('div');
splitLine.style.cssText = 'position:absolute;top:0;bottom:0;width:2px;background:#fff;box-shadow:0 0 6px #000;pointer-events:none;display:none';
document.getElementById('canvasWrap').appendChild(splitLine);

function resize() {
  const dpr = window.devicePixelRatio || 1;
  const r = canvas.getBoundingClientRect();
  const w = Math.max(1, Math.round(r.width * dpr)), h = Math.max(1, Math.round(r.height * dpr));
  if (canvas.width !== w || canvas.height !== h) {
    const first = canvas.width === 300;
    canvas.width = w; canvas.height = h;
    if (sceneName === 'sheet' && first) fitSheet();
    if (split > w) split = w / 2;
  }
}

function setUniforms(now) {
  const c = theme;
  gl.uniform1f(U.u_time, now);
  gl.uniform2f(U.u_res, canvas.width, canvas.height);
  gl.uniform1f(U.u_split, document.getElementById('compare').checked ? split : -1);
  gl.uniform1f(U.u_gradMode, +c.gradMode);
  gl.uniform1f(U.u_hueStart, +c.hueStart);
  gl.uniform1f(U.u_hueRange, +c.hueRange);
  gl.uniform1f(U.u_sat, +c.sat);
  gl.uniform1f(U.u_val, +c.val);
  gl.uniform1f(U.u_angle, +c.angle);
  gl.uniform1f(U.u_flow, +c.flow);
  gl.uniform1f(U.u_spin, +c.spin);
  gl.uniform1f(U.u_space, +c.space);
  gl.uniform1f(U.u_use3, c.use3 ? 1 : 0);
  gl.uniform3fv(U.u_c1, hex(c.c1));
  gl.uniform3fv(U.u_c2, hex(c.c2));
  gl.uniform3fv(U.u_c3, hex(c.c3));
  gl.uniform3fv(U.u_fill, hex(c.fill));
  gl.uniform1f(U.u_fillMode, +c.fillMode);
  gl.uniform3fv(U.u_fill2, hex(c.fill2));
  gl.uniform1f(U.u_fillAngle, +c.fillAngle);
  gl.uniform1f(U.u_fillTheme, +c.fillTheme);
  gl.uniform1f(U.u_opacity, +c.opacity);
  gl.uniform1f(U.u_fillOpacity, +c.fillOpacity);
  gl.uniform3fv(U.u_accent, hex(c.accent));
  gl.uniform3fv(U.u_glyphColor, hex(c.glyphColor));
  gl.uniform3fv(U.u_textColor, hex(c.textColor));
  gl.uniform1f(U.u_glyphMode, +c.glyphMode);
  gl.uniform1f(U.u_whiteOutline, +c.whiteOutline);
  gl.uniform1f(U.u_outlineSrc, +c.outlineSrc);
  // 색 조정: 부위별 값 = 전체 + 부위 (색상은 더하고 채도/밝기는 곱한다)
  const changed = k => +c[k + 'Hue'] !== 0 || +c[k + 'Sat'] !== 1 || +c[k + 'Val'] !== 1 || +c[k + 'Mode'] === 1;
  const rangeOn = ADJUST_RANGES.some(([, k]) => changed(k));
  // 색조 애니메이션: 모든 부위의 색상 값에 시간 × 속도를 더한다 (인게임과 같다)
  const anim = ((now * +c.adjHueSpeed) % 360) / 360;
  gl.uniform1f(U.u_adjOn, rangeOn || anim !== 0 || ADJUST_PARTS.some(([, k]) => changed(k)) ? 1 : 0);
  gl.uniform1f(U.u_rangeOn, rangeOn ? 1 : 0);
  gl.uniform1f(U.u_colorizeGray, +c.adjColorizeGray);
  gl.uniform3fv(U['u_range[0]'], ADJUST_RANGES.flatMap(([, k]) => [+c[k + 'Hue'] / 360, +c[k + 'Sat'], +c[k + 'Val']]));
  // 부위가 색상 고정이면 부위 색상, 전체만 고정이면 전체 색상 + 부위 회전
  const adj = (loc, k) => {
    const own = k && +c[k + 'Mode'] === 1;
    const hue = own ? +c[k + 'Hue'] : +c.adjHue + (k ? +c[k + 'Hue'] : 0);
    gl.uniform4f(loc, hue / 360 + anim, +c.adjSat * (k ? +c[k + 'Sat'] : 1), +c.adjVal * (k ? +c[k + 'Val'] : 1),
                 own || +c.adjMode === 1 ? 1 : 0);
  };
  adj(U.u_adjKeep, null);
  adj(U.u_adjOutline, 'adjOutline');
  adj(U.u_adjFill, 'adjFill');
  adj(U.u_adjIcon, 'adjIcon');
}

let last = performance.now();
function frame(ms) {
  const now = ms / 1000, dt = Math.min((ms - last) / 1000, 0.1);
  last = ms;
  resize();
  const cw = canvas.width, ch = canvas.height;
  gl.viewport(0, 0, cw, ch);
  const bg = sceneName === 'sheet' ? [0.05, 0.05, 0.07] : hex(theme.bg);
  gl.clearColor(bg[0], bg[1], bg[2], 1);
  gl.clear(gl.COLOR_BUFFER_BIT);
  setUniforms(now);
  batch.key = null;

  if (!pack || (sceneName === 'sheet' ? !assets.textures[sheetView.tex] : !scenes[sceneName])) {
    requestAnimationFrame(frame);
    return;
  }
  if (sceneName === 'sheet') {
    const z = sheetView.zoom;
    gl.uniformMatrix3fv(U.u_view, false, [2 * z / cw, 0, 0, 0, -2 * z / ch, 0, 2 * sheetView.x / cw - 1, 1 - 2 * sheetView.y / ch, 1]);
    drawSheet();
  } else {
    view.scl = Math.min(cw / DESIGN.w, ch / DESIGN.h);
    view.ox = (cw - DESIGN.w * view.scl) / 2;
    view.oy = (ch - DESIGN.h * view.scl) / 2;
    gl.uniformMatrix3fv(U.u_view, false, [2 * view.scl / cw, 0, 0, 0, 2 * view.scl / ch, 0, 2 * view.ox / cw - 1, 2 * view.oy / ch - 1, 1]);
    for (const n of scenes[sceneName]) { updateNode(n, now, dt); drawNode(n, now); }
  }
  flush();
  requestAnimationFrame(frame);
}

// ------------------------------------------------------------------ 입력

const tip = document.getElementById('tip');
let drag = null;

function devicePos(e) {
  const r = canvas.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
  return [(e.clientX - r.left) * dpr, (e.clientY - r.top) * dpr, e.clientX - r.left, e.clientY - r.top];
}

function hitNode(e) {
  const [dx, dy] = devicePos(e);
  const wx = (dx - view.ox) / view.scl, wy = (canvas.height - dy - view.oy) / view.scl;
  const list = scenes[sceneName];
  for (let i = list.length - 1; i >= 0; i--) {
    const n = list[i];
    if (!n.btn) continue;
    const [w, h] = nodeBounds(n);
    if (Math.abs(wx - n.x) <= w / 2 && Math.abs(wy - n.y) <= h / 2) return n;
  }
  return null;
}

function hitFrame(e) {
  const [dx, dy] = devicePos(e);
  const sx = (dx - sheetView.x) / sheetView.zoom, sy = (dy - sheetView.y) / sheetView.zoom;
  for (const [name, f] of Object.entries(assets.textures[sheetView.tex].frames)) {
    const [x, y, w, h] = f.rect;
    if (sx >= x && sx < x + w && sy >= y && sy < y + h) return name;
  }
  return null;
}

function nearSplit(e) {
  const [dx] = devicePos(e);
  return document.getElementById('compare').checked && Math.abs(dx - split) < 10 * (window.devicePixelRatio || 1);
}

canvas.addEventListener('pointerdown', e => {
  canvas.setPointerCapture(e.pointerId);
  const [dx, dy] = devicePos(e);
  if (nearSplit(e)) { drag = { kind: 'split' }; return; }
  if (sceneName === 'sheet') {
    if (e.shiftKey) {
      const name = hitFrame(e);
      if (name) { plainOverrides.has(name) ? plainOverrides.delete(name) : plainOverrides.add(name); updateOverrides(); }
      return;
    }
    drag = { kind: 'pan', x: dx, y: dy, vx: sheetView.x, vy: sheetView.y };
    return;
  }
  const n = hitNode(e);
  if (!n) return;
  const P = PRESS[theme.pressFx] || PRESS.gd;
  n.pressed = true;
  tweenScale(n, P.down[0], P.down[1], P.down[2], performance.now() / 1000);
  drag = { kind: 'press', node: n };
});

canvas.addEventListener('pointermove', e => {
  const [dx, dy, cssX, cssY] = devicePos(e);
  if (drag && drag.kind === 'split') { split = Math.max(0, Math.min(canvas.width, dx)); placeSplit(); return; }
  if (drag && drag.kind === 'pan') { sheetView.x = drag.vx + dx - drag.x; sheetView.y = drag.vy + dy - drag.y; return; }
  canvas.style.cursor = nearSplit(e) ? 'ew-resize' : '';
  if (sceneName === 'sheet') {
    const name = hitFrame(e);
    sheetView.hover = name;
    if (name) {
      const f = assets.textures[sheetView.tex].frames[name];
      tip.textContent = `${name}\n테마: ${f.themed ? (plainOverrides.has(name) ? '원본 유지 (수정됨)' : '적용') : '미적용'}`
        + (f.themed ? `\n테두리: ${f.outline}  글리프: ${f.glyph}\n커버리지: ${Math.round(f.coverage * 100)}%` : '')
        + `${f.rotated ? '\n(시트 안에서 회전됨)' : ''}\nShift+클릭: 원본 유지 토글`;
      tip.style.display = 'block';
      tip.style.left = cssX + 14 + 'px';
      tip.style.top = cssY + 14 + 'px';
    } else tip.style.display = 'none';
    return;
  }
  const hit = hitNode(e);
  for (const n of scenes[sceneName]) n.hover = n === hit;
  canvas.style.cursor = hit ? 'pointer' : canvas.style.cursor;
});

function release() {
  if (drag && drag.kind === 'press') {
    const n = drag.node, P = PRESS[theme.pressFx] || PRESS.gd;
    n.pressed = false;
    tweenScale(n, 1, P.up[0], P.up[1], performance.now() / 1000);
  }
  drag = null;
}
canvas.addEventListener('pointerup', release);
canvas.addEventListener('pointercancel', release);
canvas.addEventListener('pointerleave', () => { tip.style.display = 'none'; sheetView.hover = null; });

canvas.addEventListener('wheel', e => {
  if (sceneName !== 'sheet') return;
  e.preventDefault();
  const [dx, dy] = devicePos(e);
  const k = Math.exp(-e.deltaY * 0.0015);
  const z = Math.max(0.05, Math.min(8, sheetView.zoom * k));
  sheetView.x = dx - (dx - sheetView.x) * z / sheetView.zoom;
  sheetView.y = dy - (dy - sheetView.y) * z / sheetView.zoom;
  sheetView.zoom = z;
}, { passive: false });

function placeSplit() {
  const on = document.getElementById('compare').checked;
  splitLine.style.display = on ? 'block' : 'none';
  splitLine.style.left = split / (window.devicePixelRatio || 1) - 1 + 'px';
}

document.getElementById('compare').addEventListener('change', () => {
  if (split < 0) split = canvas.width / 2;
  placeSplit();
});

// ------------------------------------------------------------------ 패널 / 테마 JSON

const panelKeys = Object.keys(DIAMOND);

function syncPanel() {
  for (const k of panelKeys) {
    const el = document.getElementById(k);
    if (!el) continue;
    if (el.type === 'checkbox') el.checked = !!theme[k];
    else el.value = theme[k];
    const out = el.parentElement.querySelector('output');
    if (out) out.textContent = (+theme[k]).toFixed(el.step && el.step < 1 ? 2 : 0);
  }
  document.querySelectorAll('[data-mode]').forEach(el => {
    el.classList.toggle('hidden', !el.dataset.mode.split(' ').includes(String(theme.gradMode)));
  });
  document.querySelectorAll('[data-fill]').forEach(el => el.classList.toggle('hidden', !el.dataset.fill.split(' ').includes(String(theme.fillMode))));
  document.querySelectorAll('[data-glyph]').forEach(el => el.classList.toggle('hidden', String(theme.glyphMode) !== el.dataset.glyph));
  if (document.activeElement !== themeBox) themeBox.value = JSON.stringify(exportTheme(), null, 2);
  if (pack) {
    try { localStorage.setItem(`${STORAGE_KEY}:${pack.id}`, JSON.stringify(theme)); } catch (e) { /* 무시 */ }
  }
}

const themeBox = document.getElementById('themeJson');
const GLYPH = ['auto', 'accent', 'gradient', null, 'original'];   // 3 = 직접 지정 색 (#hex)
const TEXT = ['original', 'gradient'];

// 미리보기 설정을 테마 파일 형식으로 변환 (플랫폼 스키마 초안)
function exportTheme() {
  const t = theme;
  const font = m => (+m === 2 ? t.textColor : TEXT[+m]);
  const outline = { source: +t.outlineSrc ? 'original' : 'theme', angle: +t.angle, flow: +t.flow, spin: +t.spin,
                    space: +t.space ? 'screen' : 'node' };
  if (+t.gradMode === 0) outline.hsv = { start: +t.hueStart, range: +t.hueRange, saturation: +t.sat, value: +t.val };
  else if (+t.gradMode === 1) outline.stops = t.use3 ? [t.c1, t.c2, t.c3] : [t.c1, t.c2];
  else outline.solid = t.c1;
  return {
    schema: 1,
    ...(t.extra && t.extra.name != null ? { name: t.extra.name } : {}),
    base: pack ? pack.name : '',
    outline,
    fill: +t.fillMode === 3 ? { theme: +t.fillTheme }
      : +t.fillMode === 2 ? { gradient: [t.fill, t.fill2], angle: +t.fillAngle }
      : +t.fillMode ? t.fill : 'original',
    accent: t.accent,
    glyph: +t.glyphMode === 3 ? t.glyphColor : GLYPH[+t.glyphMode],
    whiteOutline: +t.whiteOutline ? 'gradient' : 'accent',
    fonts: { bigFont: font(t.textMode), goldFont: font(t.goldMode), effect: t.charFx },
    animation: { press: t.pressFx, hoverGlow: +t.hoverGlow, idle: t.idleFx },
    // 미리보기에 아직 없는 인게임 항목(리스트, 켜진 버튼, Geode 버튼, 팩별 덮어쓰기 등)은 불러온 그대로 돌려준다
    ...(t.extra || {}),
    // 엠보싱/안쪽 그림자는 미리보기에 아직 없으므로 불러온 값을 그대로 두고 투명도만 패널 값으로
    surface: { ...((t.extra && t.extra.surface) || {}), opacity: +t.opacity, fillOpacity: +t.fillOpacity },
    adjust: exportAdjust(t),
    preview: { background: t.bg },
  };
}

function exportAdjust(t) {
  const part = k => ({ hue: +t[k + 'Hue'], saturation: +t[k + 'Sat'], brightness: +t[k + 'Val'],
                       ...(t[k + 'Mode'] != null ? { mode: +t[k + 'Mode'] ? 'colorize' : 'shift' } : {}) });
  const out = part('adj');
  for (const [key, k] of ADJUST_PARTS) if (key) out[key] = part(k);
  out.ranges = Object.fromEntries(ADJUST_RANGES.map(([key, k]) => [key, part(k)]));
  out.colorizeGrays = +t.adjColorizeGray;
  out.speed = +t.adjHueSpeed;
  return out;
}

function importAdjust(t, a) {
  if (!a || typeof a !== 'object') return;
  const read = (o, k) => {
    if (!o || typeof o !== 'object') return;
    if (o.hue != null) t[k + 'Hue'] = o.hue;
    if (o.saturation != null) t[k + 'Sat'] = o.saturation;
    if (o.brightness != null) t[k + 'Val'] = o.brightness;
    if (o.mode != null && t[k + 'Mode'] != null) t[k + 'Mode'] = o.mode === 'colorize' ? 1 : 0;
  };
  for (const [key, k] of ADJUST_PARTS) read(key ? a[key] : a, k);
  for (const [key, k] of ADJUST_RANGES) read((a.ranges || {})[key], k);
  if (a.colorizeGrays != null) t.adjColorizeGray = a.colorizeGrays;
  if (a.speed != null) t.adjHueSpeed = a.speed;
}

// importTheme가 그대로 보관하는 최상위 항목 (미리보기 패널에 없는 것)
const EXTRA_KEYS = ['name', 'author', 'lists', 'active', 'geodeButtons', 'packs', 'surface'];

function importTheme(j) {
  const t = { ...DIAMOND };
  const o = j.outline || {};
  if (o.hsv) Object.assign(t, { gradMode: 0, hueStart: o.hsv.start, hueRange: o.hsv.range, sat: o.hsv.saturation, val: o.hsv.value });
  else if (o.stops) Object.assign(t, { gradMode: 1, c1: o.stops[0], c2: o.stops[1] || o.stops[0], c3: o.stops[2] || o.stops[1], use3: o.stops.length > 2 });
  else if (o.solid) Object.assign(t, { gradMode: 2, c1: o.solid });
  if (o.angle != null) t.angle = o.angle;
  if (o.flow != null) t.flow = o.flow;
  if (o.spin != null) t.spin = o.spin;
  t.space = o.space === 'screen' ? 1 : 0;
  t.outlineSrc = o.source === 'original' ? 1 : 0;
  if (j.fill && typeof j.fill === 'object') {
    if (Array.isArray(j.fill.gradient)) {
      Object.assign(t, { fillMode: 2, fill: j.fill.gradient[0], fill2: j.fill.gradient[1] || j.fill.gradient[0] });
      if (j.fill.angle != null) t.fillAngle = j.fill.angle;
    } else if (j.fill.theme != null) Object.assign(t, { fillMode: 3, fillTheme: j.fill.theme });
  }
  else if (j.fill && j.fill !== 'original') Object.assign(t, { fillMode: 1, fill: j.fill });
  const sf = j.surface || {};
  if (sf.opacity != null) t.opacity = sf.opacity;
  if (sf.fillOpacity != null) t.fillOpacity = sf.fillOpacity;
  importAdjust(t, j.adjust);
  if (j.accent) t.accent = j.accent;
  if (j.glyph) {
    const i = GLYPH.indexOf(j.glyph);
    Object.assign(t, i >= 0 ? { glyphMode: i } : { glyphMode: 3, glyphColor: j.glyph });
  }
  t.whiteOutline = j.whiteOutline === 'gradient' ? 1 : 0;
  const f = j.fonts || {};
  for (const [key, mode] of [['bigFont', 'textMode'], ['goldFont', 'goldMode']]) {
    if (!f[key]) continue;
    const i = TEXT.indexOf(f[key]);
    if (i >= 0) t[mode] = i; else { t[mode] = 2; t.textColor = f[key]; }
  }
  if (f.effect) t.charFx = f.effect;
  const a = j.animation || {};
  if (a.press) t.pressFx = a.press;
  if (a.hoverGlow != null) t.hoverGlow = a.hoverGlow;
  if (a.idle) t.idleFx = a.idle;
  if (j.preview && j.preview.background) t.bg = j.preview.background;
  const extra = {};
  for (const k of EXTRA_KEYS) if (j[k] !== undefined) extra[k] = j[k];
  if (Object.keys(extra).length) t.extra = extra;
  return t;
}

function updateOverrides() {
  document.getElementById('overrideBox').classList.toggle('hidden', !plainOverrides.size && sceneName !== 'sheet');
  document.getElementById('overrideJson').value = JSON.stringify({ themed: [], plain: [...plainOverrides].sort() }, null, 2);
}

function bindPanel() {
  for (const k of panelKeys) {
    const el = document.getElementById(k);
    if (!el) continue;
    el.addEventListener('input', () => {
      theme[k] = el.type === 'checkbox' ? el.checked : el.type === 'range' ? +el.value : el.value;
      syncPanel();
    });
  }
  const box = document.getElementById('presets');
  for (const [name, p] of Object.entries(PRESETS)) {
    const b = document.createElement('button');
    b.textContent = name;
    b.onclick = () => {
      const prev = theme;
      theme = !p ? packDefault() : typeof p === 'function' ? p(packDefault()) : { ...DIAMOND, ...p };
      // 색 조정은 인게임처럼 프리셋과 무관하게 유지
      for (const k of panelKeys) if (k.startsWith('adj') || k.startsWith('rng')) theme[k] = prev[k];
      syncPanel();
    };
    box.appendChild(b);
  }
  document.getElementById('copyTheme').onclick = () => {
    themeBox.select();
    navigator.clipboard.writeText(themeBox.value).catch(() => document.execCommand('copy'));
  };
  document.getElementById('applyTheme').onclick = () => {
    try { theme = importTheme(JSON.parse(themeBox.value)); syncPanel(); themeBox.style.borderColor = ''; }
    catch (e) { themeBox.style.borderColor = '#ff6b6b'; }
  };
  document.getElementById('resetTheme').onclick = () => { theme = packDefault(); syncPanel(); };

  document.querySelectorAll('[data-scene]').forEach(b => b.onclick = () => setScene(b.dataset.scene));
  document.getElementById('sheetSel').onchange = e => { sheetView.tex = e.target.value; fitSheet(); };
  document.getElementById('packSel').onchange = e => switchPack(e.target.value);
}

function setScene(name) {
  sceneName = name;
  document.querySelectorAll('[data-scene]').forEach(b => b.classList.toggle('on', b.dataset.scene === name));
  document.getElementById('sheetSel').classList.toggle('hidden', name !== 'sheet');
  tip.style.display = 'none';
  if (name === 'sheet') fitSheet();
  updateOverrides();
}

// ------------------------------------------------------------------ 시작

let packs = [];

// 팩 교체: 텍스쳐를 새로 올리고, 장면과 시트 목록을 다시 만들고, 그 팩에 저장된 테마(없으면 원본)를 불러온다
async function switchPack(id) {
  const entry = packs.find(p => p.id === id) || packs[0];
  const sel = document.getElementById('packSel');
  sel.value = entry.id;
  sel.disabled = true;
  try {
    const atlas = await loadAssets(entry);
    pack = entry;
    try { localStorage.setItem(`${STORAGE_KEY}:pack`, entry.id); } catch (e) { /* 무시 */ }
    theme = { ...packDefault(), ...(storedTheme(entry.id) || {}) };
    plainOverrides.clear();
    scenes.menu = menuNodes();
    scenes.creator = creatorNodes();
    scenes.popup = popupNodes();

    const sheetSel = document.getElementById('sheetSel');
    sheetSel.innerHTML = '';
    for (const t of atlas.textures) {
      const themed = Object.values(t.frames).filter(f => f.themed).length;
      const o = document.createElement('option');
      o.value = t.name;
      o.textContent = `${t.name.replace('-uhd', '')} (${themed}/${Object.keys(t.frames).length})`;
      sheetSel.appendChild(o);
    }
    sheetView.tex = assets.textures['GJ_GameSheet03-uhd'] ? 'GJ_GameSheet03-uhd' : atlas.textures[0].name;
    sheetSel.value = sheetView.tex;

    syncPanel();
    setScene(sceneName);
    document.getElementById('status').textContent =
      `${entry.name} · 버튼을 눌러 애니메이션을 확인하세요 · 원본 비교를 켜면 분할선 왼쪽이 원본`;
  } catch (err) {
    document.getElementById('status').textContent = '불러오기 실패: ' + err.message + ' (로컬 서버로 열어야 합니다)';
    console.error(err);
  } finally {
    sel.disabled = false;
  }
}

async function start() {
  const px = new Uint8Array([255, 255, 255, 255]);
  whiteTex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, whiteTex);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, px);

  packs = await (await fetch('assets/packs.json')).json();
  // 공개 저장소에는 로컬 전용 팩(남의 팩, 바닐라 원본)이 없으니 자료가 있는 팩만 목록에 올린다
  const present = await Promise.all(packs.map(p =>
    fetch(`assets/${p.id}/atlas.json`, { method: 'HEAD' }).then(r => r.ok, () => false)));
  packs = packs.filter((p, i) => present[i]);
  if (!packs.length) throw new Error('팩 자료가 없습니다');
  const sel = document.getElementById('packSel');
  for (const p of packs) {
    const o = document.createElement('option');
    o.value = p.id;
    o.textContent = p.name;
    sel.appendChild(o);
  }
  let saved = null;
  try { saved = localStorage.getItem(`${STORAGE_KEY}:pack`); } catch (e) { /* 무시 */ }
  bindPanel();
  resize();
  requestAnimationFrame(frame);
  await switchPack(saved && packs.some(p => p.id === saved) ? saved : packs[0].id);
}

start().catch(err => {
  document.getElementById('status').textContent = '불러오기 실패: ' + err.message + ' (로컬 서버로 열어야 합니다)';
  console.error(err);
});
