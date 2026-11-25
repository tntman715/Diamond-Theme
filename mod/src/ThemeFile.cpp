#include "ThemeFile.hpp"

#include <Geode/utils/cocos.hpp>
#include <Geode/utils/file.hpp>

#include <algorithm>
#include <ctime>
#include <initializer_list>

using namespace geode::prelude;

namespace theme::file {

namespace {

using Value = matjson::Value;

struct Reader {
	std::vector<std::string>& errors;
	std::string where;

	void bad(std::string_view key, Value const& v) {
		errors.push_back(fmt::format("{}{}: unsupported value {}", where, key, v.dump(0)));
	}

	bool color(Value const& v, ccColor3B& out, std::string_view key) {
		auto s = v.asString();
		if (s && !s.unwrap().empty()) {
			if (auto c = cc3bFromHexString(s.unwrap())) {
				out = c.unwrap();
				return true;
			}
		}
		bad(key, v);
		return false;
	}

	void number(Value const& obj, std::string_view key, float& out) {
		auto const& v = obj[key];
		if (v.isNull()) return;
		if (v.isNumber()) out = static_cast<float>(v.asDouble().unwrapOr(out));
		else bad(key, v);
	}

	// 문자열 선택지 → 번호. 선택지에 없으면 -1 (색 문자열 같은 다른 해석을 시도할 수 있게)
	int option(Value const& v, std::initializer_list<char const*> options) {
		auto s = v.asString();
		if (!s) return -1;
		int i = 0;
		for (auto o : options) {
			if (s.unwrap() == o) return i;
			++i;
		}
		return -1;
	}
};

void applyOutline(Reader& r, Value const& o, Settings& s) {
	if (!o.isObject()) {
		if (!o.isNull()) r.bad("outline", o);
		return;
	}
	if (auto const& v = o["source"]; !v.isNull()) {
		int i = r.option(v, {"theme", "original"});
		if (i >= 0) s.outlineSrc = static_cast<float>(i);
		else r.bad("outline.source", v);
	}
	if (auto const& hsv = o["hsv"]; hsv.isObject()) {
		s.gradMode = 0;
		r.number(hsv, "start", s.hueStart);
		r.number(hsv, "range", s.hueRange);
		r.number(hsv, "saturation", s.sat);
		r.number(hsv, "value", s.val);
	}
	else if (auto const& stops = o["stops"]; stops.isArray()) {
		auto n = stops.size();
		if (n >= 1) {
			s.gradMode = 1;
			r.color(stops[std::size_t(0)], s.c1, "outline.stops[0]");
			s.c2 = s.c1;
			if (n >= 2) r.color(stops[std::size_t(1)], s.c2, "outline.stops[1]");
			s.use3 = n >= 3;
			if (s.use3) r.color(stops[std::size_t(2)], s.c3, "outline.stops[2]");
		}
	}
	else if (auto const& solid = o["solid"]; !solid.isNull()) {
		if (r.color(solid, s.c1, "outline.solid")) s.gradMode = 2;
	}
	r.number(o, "angle", s.angle);
	r.number(o, "flow", s.flow);
	if (auto const& v = o["flowScope"]; !v.isNull()) {
		int i = r.option(v, {"whole-button", "icons"});
		if (i >= 0) s.flowBody = i == 0;
		else r.bad("outline.flowScope", v);
	}
	r.number(o, "spin", s.spin);
}

// "original" | "gradient" | "#hex" → 0, 1, 2(색)
void applyFont(Reader& r, Value const& v, int& mode, ccColor3B& color, std::string_view key) {
	if (v.isNull()) return;
	int i = r.option(v, {"original", "gradient"});
	if (i >= 0) mode = i;
	else if (r.color(v, color, key)) mode = 2;
}

void applySections(Reader& r, Value const& j, Settings& s) {
	applyOutline(r, j["outline"], s);

	if (auto const& v = j["fill"]; v.isObject()) {
		// { "gradient": ["#위", "#아래"], "angle": 90 } 또는 { "theme": 0.25 } (테두리 그라데이션을 어둡게)
		if (auto const& g = v["gradient"]; g.isArray() && g.size() >= 2) {
			if (r.color(g[std::size_t(0)], s.fill, "fill.gradient[0]") &&
			    r.color(g[std::size_t(1)], s.fill2, "fill.gradient[1]")) {
				s.fillMode = 2;
			}
			r.number(v, "angle", s.fillAngle);
		}
		else if (v.contains("theme")) {
			s.fillMode = 3;
			r.number(v, "theme", s.fillTheme);
		}
		else r.bad("fill", v);
	}
	else if (!v.isNull()) {
		if (r.option(v, {"original"}) == 0) s.fillMode = 0;
		else if (r.color(v, s.fill, "fill")) s.fillMode = 1;
	}

	if (auto const& f = j["inverted"]; f.isObject()) {
		auto flag = [&](char const* key, bool& out) {
			auto const& v = f[key];
			if (v.isNull()) return;
			if (v.isBool()) out = v.asBool().unwrapOr(out);
			else r.bad(fmt::format("inverted.{}", key), v);
		};
		flag("outline", s.invOutline);
		flag("icon", s.invIcon);
		flag("fill", s.invFill);
		flag("text", s.invText);
	}

	if (auto const& f = j["glass"]; f.isObject()) {
		r.number(f, "amount", s.glass);
		r.number(f, "blur", s.glassBlur);
		r.number(f, "blurSize", s.glassBlurSize);
		r.number(f, "saturation", s.glassSaturation);
		r.number(f, "noise", s.glassNoise);
		r.number(f, "refraction", s.glassRefract);
		r.number(f, "rim", s.glassRim);
		r.number(f, "edge", s.glassEdge);
		r.number(f, "dispersion", s.glassDispersion);
		r.number(f, "specular", s.glassSpecular);
		if (auto const& v = f["followMouse"]; !v.isNull()) {
			if (v.isBool()) s.glassFollowMouse = v.asBool().unwrapOr(true);
			else r.bad("glass.followMouse", v);
		}
		if (auto const& v = f["scope"]; !v.isNull()) {
			int i = r.option(v, {"buttons", "icons", "all"});
			if (i >= 0) s.glassScope = i;
			else r.bad("glass.scope", v);
		}
		if (auto const& v = f["listFrames"]; !v.isNull()) {
			if (v.isBool()) s.glassListFrames = v.asBool().unwrapOr(true);
			else r.bad("glass.listFrames", v);
		}
	}

	if (auto const& f = j["surface"]; f.isObject()) {
		r.number(f, "opacity", s.opacity);
		r.number(f, "fillOpacity", s.fillOpacity);
		r.number(f, "bevel", s.bevel);
		r.number(f, "bevelSize", s.bevelSize);
		r.number(f, "innerShadow", s.innerShadow);
		r.number(f, "innerShadowSize", s.innerShadowSize);
		r.number(f, "iconBevel", s.iconBevel);
		r.number(f, "iconBevelSize", s.iconBevelSize);
		if (auto const& v = f["fillOpacityScope"]; !v.isNull()) {
			int i = r.option(v, {"all", "icons"});
			if (i >= 0) s.fillOpacityIconsOnly = i == 1;
			else r.bad("surface.fillOpacityScope", v);
		}
	}
	if (auto const& f = j["adjust"]; f.isObject()) {
		// { "hue": 0, "saturation": 1, "brightness": 1, "mode": "shift" | "colorize",
		//   "outline": {...}, "fill": {...}, "icon": {...}, "ranges": { "reds": {...}, ... } }
		auto read = [&](Value const& o, Adjust& a) {
			r.number(o, "hue", a.hue);
			r.number(o, "saturation", a.sat);
			r.number(o, "brightness", a.val);
			if (auto const& v = o["mode"]; !v.isNull()) {
				int i = r.option(v, {"shift", "colorize"});
				if (i >= 0) a.colorize = i == 1;
				else r.bad("adjust.mode", v);
			}
		};
		read(f, s.adjAll);
		r.number(f, "colorizeGrays", s.colorizeGray);
		r.number(f, "speed", s.hueSpeed);
		std::pair<char const*, Adjust*> const parts[] = {
			{"outline", &s.adjOutline}, {"fill", &s.adjFill}, {"icon", &s.adjIcon},
		};
		for (auto [key, a] : parts) {
			auto const& o = f[key];
			if (o.isObject()) read(o, *a);
			else if (!o.isNull()) r.bad(fmt::format("adjust.{}", key), o);
		}
		if (auto const& g = f["ranges"]; g.isObject()) {
			for (int i = 0; i < 6; ++i) {
				auto const& o = g[ADJUST_RANGES[i]];
				if (o.isObject()) read(o, s.adjRanges[i]);
			}
		}
	}
	else if (!f.isNull()) r.bad("adjust", f);
	if (auto const& v = j["accent"]; !v.isNull()) r.color(v, s.accent, "accent");
	if (auto const& v = j["glyph"]; !v.isNull()) {
		// 3번(직접 지정)은 색 문자열로 쓴다
		int i = r.option(v, {"auto", "accent", "gradient", "", "original"});
		if (i >= 0) s.glyphMode = static_cast<float>(i);
		else if (r.color(v, s.glyphColor, "glyph")) s.glyphMode = 3;
	}
	if (auto const& v = j["whiteOutline"]; !v.isNull()) {
		int i = r.option(v, {"accent", "gradient"});
		if (i >= 0) s.whiteOutline = static_cast<float>(i);
		else r.bad("whiteOutline", v);
	}
	if (auto const& f = j["fonts"]; f.isObject()) {
		applyFont(r, f["bigFont"], s.bigFont, s.textColor, "fonts.bigFont");
		applyFont(r, f["goldFont"], s.goldFont, s.goldColor, "fonts.goldFont");
	}

	if (auto const& a = j["animation"]; a.isObject()) {
		if (auto const& v = a["press"]; !v.isNull()) {
			int i = r.option(v, {"gd", "squash", "pop", "none", "invert", "jelly"});
			if (i >= 0) s.pressStyle = i;
			else r.bad("animation.press", v);
		}
		r.number(a, "hoverGlow", s.hoverGlow);
		r.number(a, "hoverHue", s.hoverHue);
		r.number(a, "jelly", s.jelly);
		if (auto const& v = a["idle"]; !v.isNull()) {
			int i = r.option(v, {"none", "pulse", "shimmer"});
			if (i >= 0) s.idle = i;
			else r.bad("animation.idle", v);
		}
	}

	if (auto const& l = j["lists"]; l.isObject()) {
		r.number(l, "opacity", s.listOpacity);
		r.number(l, "glass", s.listGlass);
		auto const& v = l["background"];
		if (v.isArray() && v.size() >= 2) {
			if (r.color(v[std::size_t(0)], s.list1, "lists.background[0]") &&
			    r.color(v[std::size_t(1)], s.list2, "lists.background[1]")) {
				s.listMode = 2;
			}
		}
		else if (!v.isNull()) {
			int i = r.option(v, {"original", "dark"});
			if (i == 1) {
				s.listMode = 1;
				s.list1 = Settings{}.list1;
				s.list2 = Settings{}.list2;
			}
			else if (i == 0) s.listMode = 0;
			else r.bad("lists.background", v);
		}
	}

	if (auto const& a = j["active"]; a.isObject()) {
		auto const& o = a["outline"];
		if (o.isArray() && o.size() >= 2) {
			if (r.color(o[std::size_t(0)], s.active1, "active.outline[0]") &&
			    r.color(o[std::size_t(1)], s.active2, "active.outline[1]")) {
				s.activeMode = 2;
			}
		}
		else if (!o.isNull()) {
			if (r.option(o, {"same"}) == 0) s.activeMode = 0;
			else if (r.color(o, s.active1, "active.outline")) s.activeMode = 1;
		}
		if (auto const& f = a["fill"]; !f.isNull()) {
			if (r.option(f, {"same"}) == 0) s.activeFill = false;
			else if (r.color(f, s.activeFillColor, "active.fill")) s.activeFill = true;
		}
		// { "hue": 40, "saturation": 1, "brightness": 1, "mode": "shift" | "colorize" }
		if (auto const& o = a["adjust"]; o.isObject()) {
			r.number(o, "hue", s.activeAdj.hue);
			r.number(o, "saturation", s.activeAdj.sat);
			r.number(o, "brightness", s.activeAdj.val);
			if (auto const& v = o["mode"]; !v.isNull()) {
				int i = r.option(v, {"shift", "colorize"});
				if (i >= 0) s.activeAdj.colorize = i == 1;
				else r.bad("active.adjust.mode", v);
			}
		}
	}

	if (auto const& v = j["geodeButtons"]; !v.isNull()) {
		if (v.isBool()) s.geodeButtons = v.asBool().unwrapOr(true);
		else r.bad("geodeButtons", v);
	}
}

}

std::filesystem::path themesDir() {
	return theme::stdPath(Mod::get()->getConfigDir()) / "themes";
}

void installExamples() {
	auto dir = themesDir();
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	if (ec) {
		log::warn("could not create theme folder {}: {}", theme::pathString(dir), ec.message());
		return;
	}
	auto res = theme::stdPath(Mod::get()->getResourcesDir());
	// 리소스 이름 → 테마 폴더 안 이름
	std::pair<char const*, char const*> const files[] = {
		{"my-theme.json", "my-theme.json"},
		{"theme-vortex.json", "vortex.json"},
		{"theme-neon.json", "neon.json"},
		{"theme-sunset.json", "sunset.json"},
		{"theme-mono.json", "mono.json"},
		{"theme-glass.json", "glass.json"},
		{"theme-frosted.json", "frosted.json"},
		{"theme-invert.json", "invert.json"},
	};
	for (auto [from, to] : files) {
		auto target = dir / to;
		if (std::filesystem::exists(target, ec)) continue;
		std::filesystem::copy_file(res / from, target, ec);
		if (ec) log::warn("could not write example theme {}: {}", to, ec.message());
	}
}

bool isSettingsExport(Value const& json) {
	return json.isObject() && !json.contains("schema") && json.contains("preset");
}

namespace {

std::string hex(ccColor3B c) {
	return fmt::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b);
}

Value adjustJson(Adjust const& a) {
	return matjson::makeObject({
		{"hue", a.hue}, {"saturation", a.sat}, {"brightness", a.val}, {"mode", a.colorize ? "colorize" : "shift"},
	});
}

Value fontJson(int mode, ccColor3B color) {
	if (mode == 2) return hex(color);
	return mode == 1 ? "gradient" : "original";
}

}

Value toJson(Settings const& s, std::string const& name) {
	auto outline = Value::object();
	outline.set("source", s.outlineSrc > 0.5f ? "original" : "theme");
	if (s.gradMode < 0.5f) {
		outline.set("hsv", matjson::makeObject({
			{"start", s.hueStart}, {"range", s.hueRange}, {"saturation", s.sat}, {"value", s.val},
		}));
	}
	else if (s.gradMode < 1.5f) {
		auto stops = Value::array();
		stops.push(hex(s.c1));
		stops.push(hex(s.c2));
		if (s.use3) stops.push(hex(s.c3));
		outline.set("stops", std::move(stops));
	}
	else {
		outline.set("solid", hex(s.c1));
	}
	outline.set("angle", s.angle);
	outline.set("flow", s.flow);
	outline.set("flowScope", s.flowBody ? "whole-button" : "icons");
	outline.set("spin", s.spin);

	char const* glyphs[] = {"auto", "accent", "gradient", "", "original"};
	int glyph = static_cast<int>(s.glyphMode + 0.5f);
	char const* presses[] = {"gd", "squash", "pop", "none", "invert", "jelly"};
	char const* idles[] = {"none", "pulse", "shimmer"};

	Value lists = "original";
	if (s.listMode == 1) lists = "dark";
	else if (s.listMode == 2) {
		lists = Value::array();
		lists.push(hex(s.list1));
		lists.push(hex(s.list2));
	}
	Value activeOutline = "same";
	if (s.activeMode == 1) activeOutline = hex(s.active1);
	else if (s.activeMode == 2) {
		activeOutline = Value::array();
		activeOutline.push(hex(s.active1));
		activeOutline.push(hex(s.active2));
	}

	Value fill = "original";
	if (s.fillMode > 2.5f) fill = matjson::makeObject({{"theme", s.fillTheme}});
	else if (s.fillMode > 1.5f) {
		auto stops = Value::array();
		stops.push(hex(s.fill));
		stops.push(hex(s.fill2));
		fill = matjson::makeObject({{"gradient", std::move(stops)}, {"angle", s.fillAngle}});
	}
	else if (s.fillMode > 0.5f) fill = hex(s.fill);

	return matjson::makeObject({
		{"schema", 1},
		{"name", name},
		{"outline", std::move(outline)},
		{"fill", std::move(fill)},
		{"surface", matjson::makeObject({
			{"opacity", s.opacity}, {"fillOpacity", s.fillOpacity},
			{"fillOpacityScope", s.fillOpacityIconsOnly ? "icons" : "all"},
			{"bevel", s.bevel}, {"bevelSize", s.bevelSize},
			{"iconBevel", s.iconBevel}, {"iconBevelSize", s.iconBevelSize},
			{"innerShadow", s.innerShadow}, {"innerShadowSize", s.innerShadowSize},
		})},
		{"inverted", matjson::makeObject({
			{"outline", s.invOutline}, {"icon", s.invIcon}, {"fill", s.invFill}, {"text", s.invText},
		})},
		{"glass", matjson::makeObject({
			{"amount", s.glass}, {"blur", s.glassBlur}, {"blurSize", s.glassBlurSize},
			{"saturation", s.glassSaturation}, {"noise", s.glassNoise}, {"refraction", s.glassRefract},
			{"rim", s.glassRim}, {"edge", s.glassEdge}, {"dispersion", s.glassDispersion},
			{"specular", s.glassSpecular}, {"followMouse", s.glassFollowMouse},
			{"scope", s.glassScope == 1 ? "icons" : s.glassScope == 2 ? "all" : "buttons"},
			{"listFrames", s.glassListFrames},
		})},
		{"adjust", [&] {
			auto v = adjustJson(s.adjAll);
			v.set("outline", adjustJson(s.adjOutline));
			v.set("fill", adjustJson(s.adjFill));
			v.set("icon", adjustJson(s.adjIcon));
			auto ranges = Value::object();
			for (int i = 0; i < 6; ++i) {
				auto const& a = s.adjRanges[i];
				ranges.set(ADJUST_RANGES[i], matjson::makeObject({
					{"hue", a.hue}, {"saturation", a.sat}, {"brightness", a.val},
				}));
			}
			v.set("ranges", std::move(ranges));
			v.set("colorizeGrays", s.colorizeGray);
			v.set("speed", s.hueSpeed);
			return v;
		}()},
		{"accent", hex(s.accent)},
		{"glyph", glyph == 3 ? Value(hex(s.glyphColor)) : Value(glyphs[std::clamp(glyph, 0, 4)])},
		{"whiteOutline", s.whiteOutline > 0.5f ? "gradient" : "accent"},
		{"fonts", matjson::makeObject({
			{"bigFont", fontJson(s.bigFont, s.textColor)},
			{"goldFont", fontJson(s.goldFont, s.goldColor)},
		})},
		{"animation", matjson::makeObject({
			{"press", presses[std::clamp(s.pressStyle, 0, 5)]},
			{"jelly", s.jelly},
			{"hoverGlow", s.hoverGlow},
			{"hoverHue", s.hoverHue},
			{"idle", idles[std::clamp(s.idle, 0, 2)]},
		})},
		{"lists", matjson::makeObject({
			{"background", std::move(lists)}, {"opacity", s.listOpacity}, {"glass", s.listGlass},
		})},
		{"active", matjson::makeObject({
			{"outline", std::move(activeOutline)},
			{"fill", s.activeFill ? Value(hex(s.activeFillColor)) : Value("same")},
			{"adjust", adjustJson(s.activeAdj)},
		})},
		{"geodeButtons", s.geodeButtons},
	});
}

Result<std::filesystem::path> saveCurrent(Settings const& s) {
	auto dir = themesDir();
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	auto t = std::time(nullptr);
	std::tm local{};
#ifdef GEODE_IS_WINDOWS
	localtime_s(&local, &t);
#else
	localtime_r(&t, &local);
#endif
	char stamp[32];
	std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
	auto name = fmt::format("my-look-{}", stamp);
	auto path = dir / (name + ".json");
	GEODE_UNWRAP(utils::file::writeString(theme::geodePath(path), toJson(s, name).dump(2)));
	return Ok(path);
}

void apply(Value const& json, std::string const& packId, Settings& s, std::vector<std::string>& errors) {
	if (!json.isObject()) {
		errors.push_back("theme file is not a JSON object");
		return;
	}
	Reader r{errors, ""};
	applySections(r, json, s);
	if (auto const& p = json["packs"][packId]; p.isObject()) {
		Reader pr{errors, fmt::format("packs.{}.", packId)};
		applySections(pr, p, s);
	}
}

}
