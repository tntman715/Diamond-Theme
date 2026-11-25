#include "Advanced.hpp"
#include "Theme.hpp"

#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/Button.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/file.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace theme::advanced {

namespace {

std::vector<Field> const g_fields = {
#include "AdvancedFields.inc"
};

std::vector<char const*> const g_tabs = {"Color", "Ranges", "Surface", "Glass", "Invert", "Active", "Lists"};

matjson::Value g_values = matjson::Value::object();
bool g_dirty = false;

std::filesystem::path storeFile() {
	return theme::stdPath(Mod::get()->getSaveDir()) / "advanced.json";
}

void save() {
	auto res = utils::file::writeString(theme::geodePath(storeFile()), g_values.dump(2));
	if (!res) log::warn("could not save advanced settings: {}", res.unwrapErr());
	g_dirty = false;
}

std::string hex(ccColor3B c) {
	return fmt::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b);
}

matjson::Value const& stored(Field const& f) {
	matjson::Value const& vals = g_values;
	return vals[f.key];
}

float numberOf(Field const& f) {
	auto const& v = stored(f);
	return v.isNumber() ? static_cast<float>(v.asDouble().unwrapOr(f.defNumber)) : f.defNumber;
}

bool toggleOf(Field const& f) {
	auto const& v = stored(f);
	return v.isBool() ? v.asBool().unwrapOr(false) : f.defNumber > 0.5f;
}

int choiceOf(Field const& f) {
	auto s = stored(f).asString();
	if (s) {
		for (std::size_t i = 0; i < f.options.size(); ++i) {
			if (s.unwrap() == f.options[i]) return static_cast<int>(i);
		}
	}
	return f.defChoice;
}

// Geode 설정 파일에서 옮겨 온 값은 {"r","g","b"} 객체, 이 창에서 고친 값은 "#rrggbb"
ccColor3B colorOf(Field const& f) {
	auto const& v = stored(f);
	if (v.isObject()) {
		auto ch = [&](char const* c, GLubyte d) {
			return static_cast<GLubyte>(std::clamp<int64_t>(v[c].asInt().unwrapOr(d), 0, 255));
		};
		return {ch("r", f.defColor.r), ch("g", f.defColor.g), ch("b", f.defColor.b)};
	}
	if (auto s = v.asString(); s && !s.unwrap().empty()) {
		if (auto c = cc3bFromHexString(s.unwrap())) return c.unwrap();
	}
	return f.defColor;
}

matjson::Value defaultOf(Field const& f) {
	switch (f.kind) {
		case Kind::Number: return f.defNumber;
		case Kind::Toggle: return f.defNumber > 0.5f;
		case Kind::Choice: return f.options[f.defChoice];
		case Kind::Color: return hex(f.defColor);
	}
	return nullptr;
}

void set(Field const& f, matjson::Value value) {
	g_values.set(f.key, std::move(value));
	g_dirty = true;
}

std::string formatNumber(Field const& f, float v) {
	return f.step < 0.999f ? fmt::format("{:.2f}", v) : fmt::format("{:.0f}", v);
}

constexpr float WIDTH = 440.f;
constexpr float HEIGHT = 290.f;
constexpr float LIST_W = 400.f;
constexpr float LIST_H = 182.f;
constexpr float ROW_H = 28.f;

// 고급 설정 창: 위에 탭, 가운데 항목 목록(스크롤), 아래에 "이 탭 초기화".
// 값은 바꾸는 즉시 메모리에 반영되어(테마가 15프레임마다 다시 읽는다) 화면에 바로 보이고, 창을 닫을 때 파일에 저장한다
class AdvancedPopup : public Popup {
protected:
	int m_tab = 0;
	CCMenu* m_tabMenu = nullptr;
	ScrollLayer* m_list = nullptr;
	std::unordered_map<int, CCLabelBMFont*> m_values;   // 항목 번호 → 값 표시 라벨 (지금 탭)

	bool initPopup() {
		if (!Popup::init(WIDTH, HEIGHT)) return false;
		this->setTitle("Advanced Settings");

		m_tabMenu = CCMenu::create();
		m_tabMenu->ignoreAnchorPointForPosition(false);
		m_tabMenu->setAnchorPoint({0.5f, 0.5f});
		m_tabMenu->setContentSize({WIDTH - 20.f, 26.f});
		m_tabMenu->setPosition({WIDTH / 2, HEIGHT - 50.f});
		m_mainLayer->addChild(m_tabMenu);

		m_list = ScrollLayer::create(CCSize{LIST_W, LIST_H});
		m_list->setTouchEnabled(true);
		// Geode 6부터 ScrollLayer는 가운데 기준(앵커 0.5, 0.5)이라 위치도 가운데로 준다 (예전엔 왼쪽 아래 기준)
		m_list->ignoreAnchorPointForPosition(false);
		m_list->setAnchorPoint({0.5f, 0.5f});
		m_list->setPosition({WIDTH / 2, 34.f + LIST_H / 2});
		m_mainLayer->addChild(m_list);

		auto bottom = CCMenu::create();
		bottom->ignoreAnchorPointForPosition(false);
		bottom->setAnchorPoint({0.5f, 0.5f});
		bottom->setContentSize({200.f, 30.f});
		bottom->setPosition({WIDTH / 2, 18.f});
		auto resetSpr = ButtonSprite::create("Reset Tab", "goldFont.fnt", "GJ_button_04.png", 0.6f);
		resetSpr->setScale(0.6f);
		auto reset = CCMenuItemSpriteExtra::create(resetSpr, this, menu_selector(AdvancedPopup::onReset));
		reset->setPosition({100.f, 15.f});
		bottom->addChild(reset);
		m_mainLayer->addChild(bottom);

		this->buildTabs();
		this->buildList();
		return true;
	}

	void buildTabs() {
		m_tabMenu->removeAllChildren();
		float const step = (WIDTH - 20.f) / g_tabs.size();
		for (std::size_t i = 0; i < g_tabs.size(); ++i) {
			auto spr = ButtonSprite::create(g_tabs[i], "bigFont.fnt",
			                                static_cast<int>(i) == m_tab ? "GJ_button_02.png" : "GJ_button_04.png", 0.6f);
			spr->setScale(std::min(0.55f, (step - 4.f) / std::max(spr->getContentWidth(), 1.f)));
			auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(AdvancedPopup::onTab));
			btn->setTag(static_cast<int>(i));
			btn->setPosition({step * (i + 0.5f), 13.f});
			m_tabMenu->addChild(btn);
		}
	}

	CCNode* makeRow(int index) {
		auto const& f = g_fields[index];
		auto row = CCNode::create();
		row->setContentSize({LIST_W, ROW_H});

		auto name = CCLabelBMFont::create(f.name, "bigFont.fnt");
		name->setAnchorPoint({0.f, 0.5f});
		name->setPosition({8.f, ROW_H / 2});
		name->limitLabelWidth(150.f, 0.4f, 0.1f);
		row->addChild(name);

		auto menu = CCMenu::create();
		menu->ignoreAnchorPointForPosition(false);
		menu->setAnchorPoint({0.5f, 0.5f});
		menu->setContentSize(row->getContentSize());
		menu->setPosition({LIST_W / 2, ROW_H / 2});
		row->addChild(menu);

		if (f.description && *f.description) {
			auto info = InfoAlertButton::create(f.name, f.description, 0.45f);
			info->setPosition({168.f, ROW_H / 2});
			menu->addChild(info);
		}

		auto valueLabel = [&](std::string const& text, float x, CCPoint anchor) {
			auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
			label->setScale(0.35f);
			label->setAnchorPoint(anchor);
			label->setPosition({x, ROW_H / 2});
			row->addChild(label);
			m_values[index] = label;
		};

		switch (f.kind) {
			case Kind::Number: {
				auto slider = Slider::create(this, menu_selector(AdvancedPopup::onSlider), 0.55f);
				slider->setPosition({268.f, ROW_H / 2});
				slider->m_touchLogic->m_thumb->setTag(index);
				slider->setValue((numberOf(f) - f.min) / std::max(f.max - f.min, 0.0001f));
				slider->updateBar();
				row->addChild(slider);
				valueLabel(formatNumber(f, numberOf(f)), LIST_W - 8.f, {1.f, 0.5f});
				break;
			}
			case Kind::Toggle: {
				auto toggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(AdvancedPopup::onToggle), 0.6f);
				toggle->toggle(toggleOf(f));
				toggle->setTag(index);
				toggle->setPosition({LIST_W - 26.f, ROW_H / 2});
				menu->addChild(toggle);
				break;
			}
			case Kind::Choice: {
				auto arrow = [&](bool next) {
					auto spr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
					spr->setFlipX(!next);
					spr->setScale(0.35f);
					auto btn = CCMenuItemSpriteExtra::create(spr, this, next
						? menu_selector(AdvancedPopup::onNext) : menu_selector(AdvancedPopup::onPrev));
					btn->setTag(index);
					btn->setPosition({next ? LIST_W - 16.f : 222.f, ROW_H / 2});
					menu->addChild(btn);
				};
				arrow(false);
				arrow(true);
				valueLabel(f.options[choiceOf(f)], (222.f + LIST_W - 16.f) / 2, {0.5f, 0.5f});
				break;
			}
			case Kind::Color: {
				auto spr = CCSprite::createWithSpriteFrameName("GJ_colorBtn_001.png");
				spr->setScale(0.55f);
				spr->setColor(colorOf(f));
				auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(AdvancedPopup::onColor));
				btn->setTag(index);
				btn->setPosition({LIST_W - 26.f, ROW_H / 2});
				menu->addChild(btn);
				break;
			}
		}
		return row;
	}

	void buildList() {
		m_values.clear();
		auto content = m_list->m_contentLayer;
		content->removeAllChildren();
		// 줄은 직접 위에서부터 놓는다 (Geode 버전마다 기본 목록 레이아웃의 크기 맞춤/정렬 방식이 달라
		// Geode 6에서는 줄이 커지고 목록 밖으로 밀려났다)
		std::vector<CCNode*> rows;
		for (std::size_t i = 0; i < g_fields.size(); ++i) {
			if (g_fields[i].tab == m_tab) rows.push_back(this->makeRow(static_cast<int>(i)));
		}
		float height = std::max(LIST_H, ROW_H * static_cast<float>(rows.size()));
		content->setContentSize({LIST_W, height});
		for (std::size_t k = 0; k < rows.size(); ++k) {
			rows[k]->ignoreAnchorPointForPosition(false);
			rows[k]->setAnchorPoint({0.f, 0.f});
			rows[k]->setScale(1.f);
			rows[k]->setPosition({0.f, height - ROW_H * static_cast<float>(k + 1)});
			content->addChild(rows[k]);
		}
		m_list->scrollToTop();
		handleTouchPriority(this, true);
	}

	void onTab(CCObject* sender) {
		m_tab = sender->getTag();
		this->buildTabs();
		this->buildList();
	}

	void onReset(CCObject*) {
		for (auto const& f : g_fields) {
			if (f.tab == m_tab) set(f, defaultOf(f));
		}
		this->buildList();
	}

	void onSlider(CCObject* sender) {
		auto thumb = static_cast<SliderThumb*>(sender);
		int index = thumb->getTag();
		if (index < 0 || index >= static_cast<int>(g_fields.size())) return;
		auto const& f = g_fields[index];
		float v = f.min + thumb->getValue() * (f.max - f.min);
		if (f.step > 0) v = f.min + std::round((v - f.min) / f.step) * f.step;
		v = std::clamp(v, f.min, f.max);
		set(f, v);
		if (auto it = m_values.find(index); it != m_values.end()) it->second->setString(formatNumber(f, v).c_str());
	}

	void onToggle(CCObject* sender) {
		auto toggle = static_cast<CCMenuItemToggler*>(sender);
		// GD 토글은 콜백 뒤에 상태가 바뀐다 → 새 값은 지금 상태의 반대
		set(g_fields[toggle->getTag()], !toggle->isToggled());
	}

	void stepChoice(CCObject* sender, int dir) {
		int index = sender->getTag();
		auto const& f = g_fields[index];
		int n = static_cast<int>(f.options.size());
		int next = (choiceOf(f) + dir + n) % n;
		set(f, f.options[next]);
		if (auto it = m_values.find(index); it != m_values.end()) it->second->setString(f.options[next]);
	}
	void onPrev(CCObject* sender) { this->stepChoice(sender, -1); }
	void onNext(CCObject* sender) { this->stepChoice(sender, 1); }

	void onColor(CCObject* sender) {
		int index = sender->getTag();
		auto const& f = g_fields[index];
		Ref<CCSprite> swatch = static_cast<CCSprite*>(static_cast<CCMenuItemSpriteExtra*>(sender)->getNormalImage());
		auto popup = ColorPickPopup::create(colorOf(f));
		popup->setCallback([index, swatch](ccColor4B const& c) {
			ccColor3B color{c.r, c.g, c.b};
			set(g_fields[index], hex(color));
			swatch->setColor(color);
		});
		popup->show();
	}

	void onClose(CCObject* sender) override {
		if (g_dirty) save();
		Popup::onClose(sender);
	}

public:
	static AdvancedPopup* create() {
		auto ret = new AdvancedPopup();
		if (ret->initPopup()) {
			ret->autorelease();
			return ret;
		}
		delete ret;
		return nullptr;
	}
};

}

// Geode 설정 창의 "Advanced Settings" 항목: 값은 advanced.json에 있으므로 여기서는 저장하지 않고,
// "기본값인가"와 "초기화"만 고급 설정 저장소에 맡긴다. 그래서 설정 창의 Reset All을 누르면 고급 설정도 같이 초기화되고,
// 고급 설정을 바꿨으면 항목 옆에 초기화 화살표가 보인다.
class AdvancedSetting : public SettingV3 {
public:
	static Result<std::shared_ptr<SettingV3>> parse(std::string key, std::string modID, matjson::Value const& json) {
		auto ret = std::make_shared<AdvancedSetting>();
		GEODE_UNWRAP(ret->parseBaseProperties(std::move(key), std::move(modID), json));
		return Ok(std::static_pointer_cast<SettingV3>(ret));
	}

	bool load(matjson::Value const&) override { return true; }
	bool save(matjson::Value&) const override { return true; }
	bool isDefaultValue() const override { return allDefault(); }
	void reset() override {
		resetAll();
		this->markChanged();
	}
	SettingNodeV3* createNode(float width) override;
};

class AdvancedSettingNode : public SettingNodeV3 {
protected:
	bool init(std::shared_ptr<AdvancedSetting> setting, float width) {
		if (!SettingNodeV3::init(setting, width)) return false;
		auto spr = ButtonSprite::create("Open", "goldFont.fnt", "GJ_button_01.png", 0.8f);
		spr->setScale(0.55f);
		// Geode 6: 설정 항목의 버튼 칸은 CCMenu가 아니라서 CCMenuItemSpriteExtra는 눌리지 않는다 → geode::Button
		spr->setCascadeColorEnabled(true);
		spr->setCascadeOpacityEnabled(true);
		auto btn = Button::createWithNode(spr, [](auto) { openPopup(); });
		btn->setCascadeColorEnabled(true);
		btn->setCascadeOpacityEnabled(true);
		this->getButtonMenu()->setContentWidth(60.f);
		this->getButtonMenu()->addChildAtPosition(btn, Anchor::Center);
		this->getButtonMenu()->updateLayout();
		this->updateState(nullptr);
		return true;
	}

	void onCommit() override {}
	void onResetToDefault() override {}

public:
	static AdvancedSettingNode* create(std::shared_ptr<AdvancedSetting> setting, float width) {
		auto ret = new AdvancedSettingNode();
		if (ret->init(std::move(setting), width)) {
			ret->autorelease();
			return ret;
		}
		delete ret;
		return nullptr;
	}

	bool hasUncommittedChanges() const override { return false; }
	bool hasNonDefaultValue() const override { return !allDefault(); }
};

SettingNodeV3* AdvancedSetting::createNode(float width) {
	return AdvancedSettingNode::create(std::static_pointer_cast<AdvancedSetting>(shared_from_this()), width);
}

$execute {
	(void)Mod::get()->registerCustomSettingType("advanced", &AdvancedSetting::parse);
}

std::vector<Field> const& fields() {
	return g_fields;
}

bool allDefault() {
	for (auto const& f : g_fields) {
		auto const& v = stored(f);
		if (v.isNull()) continue;
		bool same = false;
		switch (f.kind) {
			case Kind::Number: same = std::abs(numberOf(f) - f.defNumber) < 1e-4f; break;
			case Kind::Toggle: same = toggleOf(f) == (f.defNumber > 0.5f); break;
			case Kind::Choice: same = choiceOf(f) == f.defChoice; break;
			case Kind::Color: {
				auto c = colorOf(f);
				same = c.r == f.defColor.r && c.g == f.defColor.g && c.b == f.defColor.b;
				break;
			}
		}
		if (!same) return false;
	}
	return true;
}

void resetAll() {
	g_values = matjson::Value::object();
	save();
	log::info("advanced settings reset to defaults");
}

std::vector<char const*> const& tabs() {
	return g_tabs;
}

Field const* find(std::string_view key) {
	for (auto const& f : g_fields) {
		if (key == f.key) return &f;
	}
	return nullptr;
}

matjson::Value const& values() {
	return g_values;
}

void load() {
	auto path = storeFile();
	std::error_code ec;
	if (std::filesystem::exists(path, ec)) {
		auto res = utils::file::readJson(theme::geodePath(path));
		if (res && res.unwrap().isObject()) {
			g_values = std::move(res).unwrap();
			return;
		}
		log::warn("advanced settings file is broken, starting from defaults: {}", theme::pathString(path));
		g_values = matjson::Value::object();
		return;
	}
	// 처음: 예전 버전에서 Geode 설정 창에 있던 값들을 옮겨 온다 (Geode는 모르는 키를 다음 저장 때 지운다)
	g_values = matjson::Value::object();
	int moved = 0;
	if (auto old = utils::file::readJson(theme::geodePath(theme::stdPath(Mod::get()->getSaveDir()) / "settings.json")); old && old.unwrap().isObject()) {
		auto const& json = old.unwrap();
		for (auto const& f : g_fields) {
			if (json.contains(f.key)) {
				g_values.set(f.key, json[f.key]);
				++moved;
			}
		}
	}
	save();
	log::info("advanced settings created ({} values moved from the old settings)", moved);
}

void openPopup() {
	if (auto popup = AdvancedPopup::create()) popup->show();
}

}
