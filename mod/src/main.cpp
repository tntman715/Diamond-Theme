#include <Geode/Geode.hpp>
#include <Geode/modify/CCLayerColor.hpp>
#include <Geode/modify/CCMenuItemSpriteExtra.hpp>
#include <Geode/modify/CCShaderCache.hpp>
#include <Geode/modify/CCSprite.hpp>
#include <Geode/modify/CCSpriteBatchNode.hpp>
#include <Geode/modify/CCSpriteWithHue.hpp>
#include <Geode/modify/CCTextureCache.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

#include "Advanced.hpp"
#include "Theme.hpp"
#include "ThemeFile.hpp"

#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/Notification.hpp>

using namespace geode::prelude;

$on_mod(Loaded) {
	theme::advanced::load();
	theme::loadPacks();


	// 설정 화면의 테마 파일 버튼: 지금 모습을 새 테마 파일로 저장 / 테마 폴더 열기
	// (Geode 6: 리스너가 bool을 돌려준다. false = 다른 리스너에게도 계속 전달)
	ButtonSettingPressedEvent(Mod::get(), "theme-actions").listen([](std::string_view key) {
		if (key == "save") {
			auto res = theme::file::saveCurrent(theme::current());
			if (!res) {
				Notification::create(fmt::format("Save failed: {}", res.unwrapErr()), NotificationIcon::Error)->show();
				return false;
			}
			auto path = res.unwrap();
			// 바로 쓸 수 있도록 테마 파일 설정을 새 파일로 바꿔 둔다 (Preset = file로 두면 적용)
			Mod::get()->setSettingValue<asp::fs::Path>("theme-file", theme::geodePath(path));
			Notification::create(fmt::format("Saved {}", theme::pathString(path.filename())),
			                     NotificationIcon::Success)->show();
			log::info("saved current look to {}", theme::pathString(path));
		}
		else if (key == "open-folder") {
			file::openFolder(theme::geodePath(theme::file::themesDir()));
		}
		return false;
	}).leak();
}

namespace {

// CCSpriteWithHue 훅이 테마 대상 흑백 스프라이트를 일반 스프라이트 경로로 그리는 중 (잠긴 버튼 등)
bool g_grayDraw = false;

CCGLProgram* defaultProgram() {
	return CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor);
}

// 배치 노드의 쿼드 정점 범위 (노드 공간). 9분할 전체/라벨 전체에 그라데이션을 한 번에 걸기 위해 쓴다.
// 쿼드는 원래 draw 안에서 갱신되므로 한 프레임 전 값이지만, 위치가 바뀌는 경우는 드물다.
bool quadBounds(CCTextureAtlas* atlas, float out[4]) {
	auto n = atlas->getTotalQuads();
	auto quads = atlas->getQuads();
	if (!n || !quads) return false;
	float minX = std::numeric_limits<float>::max(), minY = minX;
	float maxX = std::numeric_limits<float>::lowest(), maxY = maxX;
	for (unsigned i = 0; i < n; ++i) {
		for (auto const* v : {&quads[i].bl, &quads[i].br, &quads[i].tl, &quads[i].tr}) {
			minX = std::min(minX, v->vertices.x);
			minY = std::min(minY, v->vertices.y);
			maxX = std::max(maxX, v->vertices.x);
			maxY = std::max(maxY, v->vertices.y);
		}
	}
	out[0] = minX;
	out[1] = minY;
	out[2] = maxX - minX;
	out[3] = maxY - minY;
	return out[2] > 0 && out[3] > 0;
}

// debug-stats가 켜졌을 때만 훅 비용을 잰다 (꺼져 있으면 시간 측정 자체를 하지 않는다)
struct HookTimer {
	bool on = theme::statsEnabled();
	std::chrono::steady_clock::time_point start = on ? std::chrono::steady_clock::now()
	                                                  : std::chrono::steady_clock::time_point{};
	void stop() {
		if (!on) return;
		theme::stats().hookSeconds +=
			std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		on = false;
	}
};

}

// 단독 이미지(GJ_button_01.png 등)가 어떤 텍스쳐로 로드됐는지 기록한다.
// Texture Loader가 경로를 바꿔도 여기서 실제 텍스쳐를 잡으므로 경로를 추측할 필요가 없다.
class $modify(ThemedTextureCache, CCTextureCache) {
	CCTexture2D* addImage(char const* path, bool skipSuffix) {
		if (!theme::statsEnabled()) {
			auto texture = CCTextureCache::addImage(path, skipSuffix);
			theme::onTextureLoaded(path, texture);
			return texture;
		}
		// debug-stats: GD가 텍스쳐를 읽는 데 쓴 시간 (이미 캐시에 있으면 거의 0)
		auto start = std::chrono::steady_clock::now();
		auto texture = CCTextureCache::addImage(path, skipSuffix);
		double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		if (took > 0.001) {
			auto& s = theme::stats();
			s.textureLoads++;
			s.textureLoad += took;
			s.textureLoadMax = std::max(s.textureLoadMax, took);
		}
		theme::onTextureLoaded(path, texture);
		return texture;
	}
};

// GD가 그래픽 설정 변경/전체 화면 전환 때 기본 셰이더를 다시 만든다. 우리 셰이더도 같이 확인한다.
// iOS판은 이 함수가 인라인이라 걸 수 없다. 그때는 매 프레임 프로그램 검사(checkGL)만으로 복구한다.
#ifndef GEODE_IS_IOS
class $modify(ThemedShaderCache, CCShaderCache) {
	void reloadDefaultShaders() {
		CCShaderCache::reloadDefaultShaders();
		theme::onShadersReloaded();
	}
};
#endif

// 리스트 칸 배경: GD의 모든 리스트 칸(레벨, 댓글, 업적, 아티스트 등)은 m_backgroundLayer에
// (161,88,44) / (194,114,62) 갈색을 번갈아 칠한다. 그 색으로 칠해진 레이어를 기억해 두었다가
// 그릴 때 테마 색으로 바꾼다 (그릴 때 바꾸므로 설정을 바꾸면 열려 있는 리스트에도 바로 반영된다).
class $modify(ThemedLayerColor, CCLayerColor) {
	struct Fields {
		int role = 0;              // 0 리스트 갈색 아님, 1 어두운 칸, 2 밝은 칸
		ccColor3B original{};      // GD가 칠한 원래 색 (원본으로 되돌릴 때)
		bool applying = false;     // 우리가 setColor를 부르는 중
	};

	static int roleOf(ccColor3B const& c) {
		if (c.r == 161 && c.g == 88 && c.b == 44) return 1;
		if (c.r == 194 && c.g == 114 && c.b == 62) return 2;
		if (c.r == 191 && c.g == 114 && c.b == 62) return 2;   // 리스트 자체 배경 (칸이 적을 때 보이는 부분)
		return 0;
	}

	void remember(ccColor3B const& c) {
		if (m_fields->applying) return;
		m_fields->role = roleOf(c);
		m_fields->original = c;
	}

	bool initWithColor(ccColor4B const& color, float width, float height) {
		if (!CCLayerColor::initWithColor(color, width, height)) return false;
		remember({color.r, color.g, color.b});
		return true;
	}

	void setColor(ccColor3B const& color) {
		remember(color);
		CCLayerColor::setColor(color);
	}

	void draw() {
		int role = m_fields->role;
		if (!role) return CCLayerColor::draw();

		ccColor3B want = m_fields->original;
		theme::listColor(role, want);
		auto cur = getColor();
		if (cur.r != want.r || cur.g != want.g || cur.b != want.b) {
			m_fields->applying = true;
			CCLayerColor::setColor(want);
			m_fields->applying = false;
		}

		// 리스트 칸 투명도: 그릴 때만 잠깐 낮췄다가 되돌린다 (GD가 정한 값은 그대로 남도록)
		float k = theme::listOpacity();
		GLubyte opacity = getOpacity();
		if (k < 0.999f) CCLayerColor::setOpacity(static_cast<GLubyte>(opacity * k));
		// 리스트 칸 유리: 칸 색을 배경 위에 얇게 입힌다 (셰이더만 잠깐 바꿔 원래 draw로 그린다)
		if (auto glassProg = theme::prepareListGlass(this)) {
			if (theme::statsEnabled()) theme::stats().listGlassDraws++;
			auto old = getShaderProgram();
			if (old) old->retain();
			setShaderProgram(glassProg);
			CCLayerColor::draw();
			setShaderProgram(old);
			if (old) old->release();
		}
		else {
			CCLayerColor::draw();
		}
		if (k < 0.999f) CCLayerColor::setOpacity(opacity);
	}
};

// 쫀득한 누름: 감쇠 스프링으로 가로/세로 크기를 따로 움직인다.
// 누르면 옆으로 퍼지며 납작해지고, 떼면 원래 크기를 지나쳐 몇 번 출렁이다 멈춘다 (가로가 줄 때 세로가 늘어 젤리처럼 보인다)
class JellyTo : public CCActionInterval {
	float m_toX = 1, m_toY = 1, m_fromX = 1, m_fromY = 1;
	float m_omega = 20, m_zeta = 0.3f;   // 출렁임 빠르기(rad/s), 감쇠 비율 (작을수록 오래 출렁인다)

public:
	static JellyTo* create(float duration, float toX, float toY, float omega, float zeta) {
		auto ret = new JellyTo();
		if (ret->initWithDuration(duration)) {
			ret->m_toX = toX;
			ret->m_toY = toY;
			ret->m_omega = omega;
			ret->m_zeta = zeta;
			ret->autorelease();
			return ret;
		}
		delete ret;
		return nullptr;
	}

	void startWithTarget(CCNode* target) override {
		CCActionInterval::startWithTarget(target);
		m_fromX = target->getScaleX();
		m_fromY = target->getScaleY();
	}

	void update(float t) override {
		auto target = getTarget();
		if (!target) return;
		// 정지 상태에서 놓은 스프링의 남은 변위 비율 (1 → 0, 0 아래로도 넘어간다)
		float f = 0;
		if (t < 1.f) {
			float tau = t * getDuration();
			float wd = m_omega * std::sqrt(1.f - m_zeta * m_zeta);
			f = std::exp(-m_zeta * m_omega * tau) * (std::cos(wd * tau) + m_zeta * m_omega / wd * std::sin(wd * tau));
		}
		target->setScaleX(m_toX + (m_fromX - m_toX) * f);
		target->setScaleY(m_toY + (m_fromY - m_toY) * f);
	}
};

// 버튼 누름 애니메이션: GD 기본(크게 + 바운스) 대신 설정한 스타일로.
// GD 원래 로직(선택 이미지, 색 어둡게, 소리)은 그대로 두고 크기 애니메이션만 바꾼다.
//
// 돌아갈 크기는 m_baseScale이 아니라 "누르기 직전 실제 크기"를 쓴다. GD나 다른 모드가 버튼을 만든 뒤
// 이미지 크기를 바꾸면 m_baseScale이 옛 값으로 남아, 그 값으로 돌아가면 버튼이 커진 채로 남는다 (v0.4.0 버그).
class $modify(ThemedMenuItem, CCMenuItemSpriteExtra) {
	static constexpr int PRESS_TAG = 0x7DA0;

	struct Fields {
		float restScale = -1.f;   // 누르기 직전 이미지 크기 (아직 모르면 -1)
		float restX = 0.f, restY = 0.f;   // 쫀득한 누름용: 가로/세로를 따로 (비율이 다르거나 뒤집힌(음수) 버튼도 그대로 돌아가게, 0 = 모름)
		int style = 0;            // 이번 누름에 쓴 스타일 (누르는 도중 설정이 바뀌어도 일관되게)
		int lastRelease = 0;      // 방금 unselected에서 시작한 되돌아가기 스타일 (activate에서 확인)
	};

	bool takesOver() {
		return theme::pressStyle() != 0 && m_animationEnabled && m_animationType == MenuAnimationType::Scale;
	}

	float restScale() {
		return m_fields->restScale > 0 ? m_fields->restScale : m_baseScale;
	}
	float restX() {
		return m_fields->restX != 0.f ? m_fields->restX : restScale();
	}
	float restY() {
		return m_fields->restY != 0.f ? m_fields->restY : restScale();
	}

	void runPress(CCNode* image, CCActionInterval* action) {
		image->stopActionByTag(PRESS_TAG);
		if (!action) {
			image->setScale(restScale());
			return;
		}
		action->setTag(PRESS_TAG);
		image->runAction(action);
	}

	CCActionInterval* releaseAction(int style) {
		if (style == 5) return JellyTo::create(0.9f, restX(), restY(), 20.f, 0.22f);
		auto to = CCScaleTo::create(style == 2 ? 0.5f : 0.35f, restScale());
		if (style == 1) return CCEaseBackOut::create(to);
		if (style == 2) return CCEaseElasticOut::create(to, 0.35f);
		return nullptr;
	}

	void selected() {
		if (!takesOver()) return CCMenuItemSpriteExtra::selected();
		auto image = getNormalImage();
		// 우리 애니메이션이 진행 중이 아닐 때만 지금 크기를 기준으로 삼는다 (연타 중에는 처음 기준 유지)
		if (image && !image->getActionByTag(PRESS_TAG)) {
			m_fields->restScale = image->getScale();
			m_fields->restX = image->getScaleX();
			m_fields->restY = image->getScaleY();
		}
		int style = m_fields->style = theme::pressStyle();
		m_animationEnabled = false;   // GD 크기 애니메이션만 끄고 원래 처리 실행
		CCMenuItemSpriteExtra::selected();
		m_animationEnabled = true;
		if (!image) return;
		float rest = restScale();
		switch (style) {
			case 1: runPress(image, CCEaseOut::create(CCScaleTo::create(0.08f, rest * 0.9f), 2.f)); break;
			case 2: runPress(image, CCEaseOut::create(CCScaleTo::create(0.12f, rest * 1.12f), 2.f)); break;
			case 4: theme::notePress(); runPress(image, nullptr); break;   // 색 반전 (크기는 그대로, 색은 셰이더에서)
			case 5: {
				float k = theme::jellyStrength();
				runPress(image, JellyTo::create(0.45f, restX() * (1.f + 0.09f * k), restY() * (1.f - 0.05f * k), 26.f, 0.45f));
				break;
			}
			default: runPress(image, nullptr); break;   // 없음
		}
	}

	void unselected() {
		int style = m_fields->style;
		if (!style) return CCMenuItemSpriteExtra::unselected();   // 우리가 맡지 않은 누름
		m_fields->style = 0;
		bool wasEnabled = m_animationEnabled;
		m_animationEnabled = false;
		CCMenuItemSpriteExtra::unselected();
		m_animationEnabled = wasEnabled;
		if (auto image = getNormalImage()) runPress(image, releaseAction(style));
		m_fields->lastRelease = style;
	}

	// 클릭하면 unselected 직후 activate가 불린다. activate가 이미지 액션을 정리해 되돌아가는 애니메이션이
	// 끊기면 커진 채로 멈추므로, 끊겼으면 현재 크기에서 다시 이어서 돌아간다.
	void activate() {
		int style = m_fields->lastRelease;
		m_fields->lastRelease = 0;
		CCMenuItemSpriteExtra::activate();
		auto image = getNormalImage();
		if (!style || !image || image->getActionByTag(PRESS_TAG)) return;
		bool off = style == 5 ? std::abs(image->getScaleX() - restX()) > 0.001f || std::abs(image->getScaleY() - restY()) > 0.001f
		                      : std::abs(image->getScale() - restScale()) > 0.001f;
		if (off) {
			runPress(image, releaseAction(style));
		}
	}
};

// 모든 개별 스프라이트 그리기에서: 테마 대상 프레임(시트 프레임, 슬라이더 같은 단독 이미지)이면 테마 셰이더로.
// 그릴 때마다 판별하므로 프레임이 바뀌는 스프라이트(토글 버튼 등)도 따라간다.
class $modify(ThemedSprite, CCSprite) {
	// 원래 기본 셰이더(또는 우리가 잠깐 바꿔 둔 테마 셰이더)를 쓰는 스프라이트만 효과를 건다.
	// 다른 모드나 GD가 특수 셰이더를 붙인 스프라이트는 건드리지 않는다.
	bool usesDefaultShader(CCGLProgram* prog) {
		auto cur = getShaderProgram();
		return cur == prog || cur == defaultProgram();
	}

	// 쿼드의 텍스쳐 좌표 범위 (회전 여부와 무관하게 최솟값/최댓값)
	static void quadUV(ccV3F_C4B_T2F_Quad const& q, float out[4]) {
		out[0] = std::min({q.tl.texCoords.u, q.tr.texCoords.u, q.bl.texCoords.u, q.br.texCoords.u});
		out[1] = std::min({q.tl.texCoords.v, q.tr.texCoords.v, q.bl.texCoords.v, q.br.texCoords.v});
		out[2] = std::max({q.tl.texCoords.u, q.tr.texCoords.u, q.bl.texCoords.u, q.br.texCoords.u});
		out[3] = std::max({q.tl.texCoords.v, q.tr.texCoords.v, q.bl.texCoords.v, q.br.texCoords.v});
	}

	void draw() {
		HookTimer timer;
		auto prog = theme::program();
		theme::FrameInfo const* frame = nullptr;
		theme::Textures tex;
		if (prog && !m_pobBatchNode && m_pobTexture && theme::enabled()) {
			frame = theme::lookup(m_pobTexture, m_sQuad, &tex);
		}

		bool plain = false;
		bool twoPass = false;
		bool glass = false;
		if (frame) {
			// 유리 배경 준비는 GL 상태를 바꾸므로 테마 셰이더를 쓰기 전에
			glass = theme::prepareGlass(this, *frame);
			if (getShaderProgram() != prog) setShaderProgram(prog);
			prog->use();
			auto fx = theme::fxFor(this);
			fx.glass = glass ? 1.f : 0.f;
			fx.gray = g_grayDraw ? 1.f : 0.f;
			twoPass = theme::invertsParts();
			fx.pass = twoPass ? 1.f : 0.f;
			theme::applySprite(*frame, tex, fx);
		}
		else if (prog && m_pobTexture && !m_pobBatchNode && theme::invertWindow() && usesDefaultShader(prog)) {
			// 색 반전 누름: 테마 대상이 아닌 그림(다른 모드 버튼, 텍스트 이미지 등)도 버튼 전체가 뒤집히도록
			auto fx = theme::fxFor(this);
			if (fx.invert > 0) {
				if (getShaderProgram() != prog) setShaderProgram(prog);
				prog->use();
				float rect[4];
				quadUV(m_sQuad, rect);
				theme::applyPlain(rect, fx);
				plain = true;
			}
		}
		if (!frame && !plain && prog && getShaderProgram() == prog) {
			setShaderProgram(defaultProgram());
		}
		if (timer.on) {
			auto& s = theme::stats();
			s.spriteDraws++;
			if (frame) s.spriteThemed++;
			if (glass) s.glassDraws++;
			if (twoPass) s.invertDraws++;
			timer.stop();
			theme::tickStats();
		}
		CCSprite::draw();
		if (twoPass) {
			// 배경 반전 부분: 같은 쿼드를 반전 블렌드로 한 번 더 (1번째 그리기까지 포함한 뒷화면이 뒤집힌다)
			theme::setPass(2.f);
			auto blend = m_sBlendFunc;
			m_sBlendFunc = theme::INVERT_BLEND;
			CCSprite::draw();
			m_sBlendFunc = blend;
		}
	}
};

// 색상/흑백 스프라이트(CCSpriteWithHue, CCSpriteGrayscale): GD가 잠긴 버튼(Versus, The Map 등)을 이걸로 그린다.
// 자기 셰이더로 draw를 통째로 바꿔서 위 CCSprite::draw 훅을 거치지 않으므로, 테마 대상이면 일반 스프라이트
// 경로(CCSprite::draw → 테마 셰이더)로 그리고 흑백은 셰이더 마지막에 입힌다.
class $modify(ThemedHueSprite, CCSpriteWithHue) {
	void draw() {
		auto prog = theme::program();
		theme::Textures tex;
		if (!prog || m_pobBatchNode || !m_pobTexture || !theme::enabled() ||
		    !theme::lookup(m_pobTexture, m_sQuad, &tex)) {
			return CCSpriteWithHue::draw();
		}
		auto old = getShaderProgram();
		if (old) old->retain();
		g_grayDraw = typeinfo_cast<CCSpriteGrayscale*>(this) != nullptr;
		// 이 클래스는 색/투명도를 정점 대신 자기 uniform으로 넘긴다 → 일반 스프라이트처럼 정점 색을 채워 그린 뒤 되돌린다
		CCSprite::updateColor();
		CCSprite::draw();
		g_grayDraw = false;
		this->updateColor();
		setShaderProgram(old);
		if (old) old->release();
	}
};

// 배치 노드: 9분할 배경(CCScale9Sprite → GJ_button_*, GJ_square*)과 라벨(CCLabelBMFont)
class $modify(ThemedBatchNode, CCSpriteBatchNode) {
	void draw() {
		HookTimer timer;
		auto prog = theme::program();
		bool themed = false, label = false, twoPass = false, glassUsed = false;
		if (prog && m_pobTextureAtlas && theme::enabled()) {
			float bounds[4];
			bool inverting = theme::invertWindow();
			if (auto bm = typeinfo_cast<CCLabelBMFont*>(this)) {
				ccColor3B color{255, 255, 255};
				int mode = theme::fontMode(bm, color);
				if (mode && quadBounds(m_pobTextureAtlas, bounds)) {
					if (getShaderProgram() != prog) setShaderProgram(prog);
					prog->use();
					auto fx = inverting ? theme::fxFor(this) : theme::Fx{};
					twoPass = theme::invertsText();
					fx.pass = twoPass ? 1.f : 0.f;
					theme::applyText(bounds, mode, color, fx);
					themed = label = true;
				}
			}
			else {
				theme::Textures tex;
				auto frame = theme::lookupSingle(m_pobTextureAtlas->getTexture(), &tex);
				if (frame && quadBounds(m_pobTextureAtlas, bounds)) {
					bool glass = theme::prepareGlass(this, *frame);
					glassUsed = glass;
					if (getShaderProgram() != prog) setShaderProgram(prog);
					prog->use();
					auto fx = theme::fxFor(this);
					fx.glass = glass ? 1.f : 0.f;
					twoPass = theme::invertsParts();
					fx.pass = twoPass ? 1.f : 0.f;
					theme::applyBatch(*frame, tex, bounds, fx);
					themed = true;
				}
			}
			// 색 반전 누름: 테마 대상이 아닌 배치(일반 라벨, 다른 9분할)도 버튼 안에 있으면 같이 뒤집는다
			if (!themed && inverting) {
				auto cur = getShaderProgram();
				auto fx = (cur == prog || cur == defaultProgram()) ? theme::fxFor(this) : theme::Fx{};
				if (fx.invert > 0) {
					if (cur != prog) setShaderProgram(prog);
					prog->use();
					float const full[4] = {0.f, 0.f, 1.f, 1.f};
					theme::applyPlain(full, fx);
					themed = true;
				}
			}
		}
		if (!themed && prog && getShaderProgram() == prog) {
			setShaderProgram(defaultProgram());
		}
		if (timer.on) {
			auto& s = theme::stats();
			s.batchDraws++;
			if (themed) s.batchThemed++;
			if (label) s.labelThemed++;
			if (glassUsed) s.glassDraws++;
			if (twoPass) s.invertDraws++;
			timer.stop();
			theme::tickStats();
		}
		CCSpriteBatchNode::draw();
		if (twoPass) {
			theme::setPass(2.f);
			auto blend = m_blendFunc;
			m_blendFunc = theme::INVERT_BLEND;
			CCSpriteBatchNode::draw();
			m_blendFunc = blend;
		}
	}
};
