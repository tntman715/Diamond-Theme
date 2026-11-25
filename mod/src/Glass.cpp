#include "Glass.hpp"
#include "Theme.hpp"

using namespace geode::prelude;

namespace theme::glass {

namespace {

constexpr int MAX_PER_FRAME = 4;   // 한 프레임에 만들 수 있는 배경 수 (레이어가 많아도 비용 상한)
constexpr int SCALE = 4;           // 블러용 축소 배율 (블러 크기 2 이상이면 두 배로 더 줄여 반경을 넓힌다)

constexpr char const* VERTEX = R"(
attribute vec4 a_position;
attribute vec2 a_texCoord;
varying vec2 v_uv;
void main() {
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_uv = a_texCoord;
}
)";

// 축소: 원본 4x4 픽셀 블록을 쌍선형 4번으로 평균
constexpr char const* DOWN = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_uv;
uniform sampler2D CC_Texture0;
uniform vec2 u_texel;
void main() {
    vec3 c = texture2D(CC_Texture0, v_uv + vec2(-1.0, -1.0) * u_texel).rgb
           + texture2D(CC_Texture0, v_uv + vec2(1.0, -1.0) * u_texel).rgb
           + texture2D(CC_Texture0, v_uv + vec2(-1.0, 1.0) * u_texel).rgb
           + texture2D(CC_Texture0, v_uv + vec2(1.0, 1.0) * u_texel).rgb;
    gl_FragColor = vec4(c * 0.25, 1.0);
}
)";

// 9탭 가우시안 (쌍선형으로 5번 샘플)
constexpr char const* BLUR = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_uv;
uniform sampler2D CC_Texture0;
uniform vec2 u_step;
void main() {
    vec3 c = texture2D(CC_Texture0, v_uv).rgb * 0.2270270270;
    c += texture2D(CC_Texture0, v_uv + u_step * 1.3846153846).rgb * 0.3162162162;
    c += texture2D(CC_Texture0, v_uv - u_step * 1.3846153846).rgb * 0.3162162162;
    c += texture2D(CC_Texture0, v_uv + u_step * 3.2307692308).rgb * 0.0702702703;
    c += texture2D(CC_Texture0, v_uv - u_step * 3.2307692308).rgb * 0.0702702703;
    gl_FragColor = vec4(c, 1.0);
}
)";

// 단색 레이어(리스트 칸)용: 레이어 색을 유리처럼 (배경 위에 색을 얇게 입힌다). CCLayerColor가 그대로 그린다
constexpr char const* COLOR_VERTEX = R"(
attribute vec4 a_position;
attribute vec4 a_color;
varying vec4 v_color;
void main() {
    gl_Position = CC_MVPMatrix * a_position;
    v_color = a_color;
}
)";

constexpr char const* COLOR_FRAGMENT = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_color;
uniform sampler2D u_bg;
uniform sampler2D u_bgSharp;
uniform vec4 u_vp;
uniform float u_amount;
uniform float u_frost;
uniform float u_sat;
uniform float u_noise;
void main() {
    vec2 p = clamp((gl_FragCoord.xy - u_vp.xy) / u_vp.zw, 0.0, 1.0);
    vec3 bg = mix(texture2D(u_bgSharp, p).rgb, texture2D(u_bg, p).rgb, u_frost);
    bg = mix(vec3(dot(bg, vec3(0.299, 0.587, 0.114))), bg, u_sat);
    float n = fract(52.9829189 * fract(dot(floor(gl_FragCoord.xy), vec2(0.06711056, 0.00583715)))) - 0.5;
    gl_FragColor = vec4(clamp(mix(v_color.rgb, bg, u_amount) + n * u_noise * 0.12, 0.0, 1.0), v_color.a);
}
)";

struct Target {
	GLuint tex = 0;
	GLuint fbo = 0;
	int w = 0, h = 0;
};

GLuint g_copy = 0;
int g_copyW = 0, g_copyH = 0;
Target g_a, g_b;
CCGLProgram* g_down = nullptr;
CCGLProgram* g_blur = nullptr;
CCGLProgram* g_color = nullptr;
bool g_colorFailed = false;
bool g_failed = false;

unsigned g_frame = ~0u;
CCNode* g_layer = nullptr;
int g_count = 0;
bool g_valid = false;
float g_vp[4] = {0, 0, 1, 1};

CCGLProgram* makeProgram(char const* frag, char const* vert = VERTEX) {
	auto p = new CCGLProgram();
	if (!p->initWithVertexShaderByteArray(vert, frag)) {
		log::error("glass shader compile failed:\n{}\n{}", p->vertexShaderLog(), p->fragmentShaderLog());
		p->release();
		return nullptr;
	}
	p->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
	p->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
	p->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
	if (!p->link()) {
		log::error("glass shader link failed: {}", theme::linkLog(p));
		p->release();
		return nullptr;
	}
	p->updateUniforms();   // CC_Texture0 = 유닛 0
	return p;
}

void allocTexture(GLuint& tex, int w, int h) {
	if (!tex) glGenTextures(1, &tex);
	ccGLBindTexture2D(tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
}

bool allocTarget(Target& t, int w, int h) {
	if (t.tex && t.w == w && t.h == h) return true;
	allocTexture(t.tex, w, h);
	t.w = w;
	t.h = h;
	if (!t.fbo) glGenFramebuffers(1, &t.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
	return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

// 화면 전체 사각형 (CCSprite::draw와 같은 방식: 클라이언트 메모리 정점)
void drawQuad(Target const& dst) {
	glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
	glViewport(0, 0, dst.w, dst.h);
	static ccV3F_C4B_T2F const quad[4] = {
		{{-1, -1, 0}, {255, 255, 255, 255}, {0, 0}},
		{{1, -1, 0}, {255, 255, 255, 255}, {1, 0}},
		{{-1, 1, 0}, {255, 255, 255, 255}, {0, 1}},
		{{1, 1, 0}, {255, 255, 255, 255}, {1, 1}},
	};
	ccGLEnableVertexAttribs(kCCVertexAttribFlag_PosColorTex);
	auto base = reinterpret_cast<char const*>(quad);
	glVertexAttribPointer(kCCVertexAttrib_Position, 3, GL_FLOAT, GL_FALSE, sizeof(ccV3F_C4B_T2F),
	                      base + offsetof(ccV3F_C4B_T2F, vertices));
	glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ccV3F_C4B_T2F),
	                      base + offsetof(ccV3F_C4B_T2F, colors));
	glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, sizeof(ccV3F_C4B_T2F),
	                      base + offsetof(ccV3F_C4B_T2F, texCoords));
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// 장면 바로 아래 노드 (메뉴 레이어, 팝업 등). 팝업이 열리면 그 위 버튼은 새 배경(팝업 판 포함)을 쓴다
CCNode* layerOf(CCNode* node) {
	auto n = node;
	while (n && n->getParent() && n->getParent()->getParent()) n = n->getParent();
	return n;
}

bool build(float blur, float size) {
	if (g_failed) return false;
	if (!g_down) g_down = makeProgram(DOWN);
	if (!g_blur) g_blur = makeProgram(BLUR);
	if (!g_down || !g_blur) {
		g_failed = true;
		return false;
	}

	// 지금 상태 저장
	GLint prevFbo = 0, vp[4] = {0, 0, 0, 0};
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
	glGetIntegerv(GL_VIEWPORT, vp);
	GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
	GLboolean stencil = glIsEnabled(GL_STENCIL_TEST);
	if (vp[2] <= 0 || vp[3] <= 0) return false;

	// 1) 지금 그려진 화면(현재 프레임버퍼의 뷰포트 영역)을 텍스쳐로
	if (g_copyW != vp[2] || g_copyH != vp[3]) {
		allocTexture(g_copy, vp[2], vp[3]);
		g_copyW = vp[2];
		g_copyH = vp[3];
	}
	ccGLBindTexture2D(g_copy);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);

	// 블러 크기: 1 = 예전 그대로, 2 이상은 더 작게 줄인 뒤 블러를 여러 번 (클래식 반투명 유리처럼 형체만 남게)
	int scale = size >= 2.f ? SCALE * 2 : SCALE;
	int passes = std::clamp(static_cast<int>(std::lround(size * 2.f)), 2, 8);
	int w = std::max(1, vp[2] / scale), h = std::max(1, vp[3] / scale);
	bool ok = allocTarget(g_a, w, h) && allocTarget(g_b, w, h);
	if (ok) {
		if (scissor) glDisable(GL_SCISSOR_TEST);
		if (stencil) glDisable(GL_STENCIL_TEST);
		ccGLBlendFunc(GL_ONE, GL_ZERO);

		// 2) 축소 (4x4 평균을 축소 배율에 맞춰 넓게 집는다)
		g_down->use();
		float reach = scale / 4.f;
		g_down->setUniformLocationWith2f(g_down->getUniformLocationForName("u_texel"), reach / g_copyW, reach / g_copyH);
		ccGLBindTexture2D(g_copy);
		drawQuad(g_a);

		// 3) 가로/세로 블러 여러 번
		g_blur->use();
		auto stepLoc = g_blur->getUniformLocationForName("u_step");
		float spread = 0.6f + blur * 2.4f;
		for (int i = 0; i < passes; ++i) {
			g_blur->setUniformLocationWith2f(stepLoc, spread / w, 0.f);
			ccGLBindTexture2D(g_a.tex);
			drawQuad(g_b);
			g_blur->setUniformLocationWith2f(stepLoc, 0.f, spread / h);
			ccGLBindTexture2D(g_b.tex);
			drawQuad(g_a);
		}
	}

	// 원래 상태로
	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
	glViewport(vp[0], vp[1], vp[2], vp[3]);
	if (scissor) glEnable(GL_SCISSOR_TEST);
	if (stencil) glEnable(GL_STENCIL_TEST);
	if (!ok) {
		log::warn("glass: could not create blur buffers ({}x{})", w, h);
		g_failed = true;
		return false;
	}
	for (int i = 0; i < 4; ++i) g_vp[i] = static_cast<float>(vp[i]);
	if (theme::statsEnabled()) theme::stats().glassBuilds++;
	static int logged = 0;
	if (logged != vp[2] * 100000 + vp[3]) {
		logged = vp[2] * 100000 + vp[3];
		log::info("glass: background buffers ready ({}x{} -> {}x{} blur)", vp[2], vp[3], w, h);
	}
	return true;
}

}

bool prepare(CCNode* node, float blur, float size) {
	auto frame = CCDirector::sharedDirector()->getTotalFrames();
	auto layer = layerOf(node);
	if (frame != g_frame) {
		g_frame = frame;
		g_layer = nullptr;
		g_count = 0;
		g_valid = false;
	}
	if (g_valid && layer == g_layer) return true;
	if (g_count >= MAX_PER_FRAME) return g_valid;   // 상한: 마지막 배경을 그대로 쓴다
	++g_count;
	g_layer = layer;
	g_valid = build(blur, size);
	return g_valid;
}

GLuint texture() {
	return g_a.tex;
}

GLuint sharpTexture() {
	return g_copy;
}

CCGLProgram* colorProgram(float amount, float frost, float saturation, float noise) {
	if (!g_valid || g_colorFailed) return nullptr;
	if (!g_color) {
		g_color = makeProgram(COLOR_FRAGMENT, COLOR_VERTEX);
		if (!g_color) {
			g_colorFailed = true;
			return nullptr;
		}
		g_color->use();
		g_color->setUniformLocationWith1i(g_color->getUniformLocationForName("u_bg"), 3);
		g_color->setUniformLocationWith1i(g_color->getUniformLocationForName("u_bgSharp"), 4);
	}
	auto p = g_color;
	p->use();
	p->setUniformLocationWith4f(p->getUniformLocationForName("u_vp"), g_vp[0], g_vp[1], g_vp[2], g_vp[3]);
	p->setUniformLocationWith1f(p->getUniformLocationForName("u_amount"), amount);
	p->setUniformLocationWith1f(p->getUniformLocationForName("u_frost"), frost);
	p->setUniformLocationWith1f(p->getUniformLocationForName("u_sat"), saturation);
	p->setUniformLocationWith1f(p->getUniformLocationForName("u_noise"), noise);
	ccGLBindTexture2DN(3, g_a.tex);
	ccGLBindTexture2DN(4, g_copy);
	glActiveTexture(GL_TEXTURE0);
	return p;
}

float const* viewport() {
	return g_vp;
}

void onContextLost() {
	// 옛 컨텍스트의 번호는 이미 사라졌다: 지우지 않고 잊기만 한다 (프로그램 객체도 해제하지 않고 버린다)
	g_copy = 0;
	g_copyW = g_copyH = 0;
	g_a = {};
	g_b = {};
	g_down = nullptr;
	g_blur = nullptr;
	g_color = nullptr;
	g_colorFailed = false;
	g_failed = false;
	g_frame = ~0u;
	g_valid = false;
}

}
