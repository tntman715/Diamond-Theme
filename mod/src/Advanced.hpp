#pragma once

#include <Geode/Geode.hpp>

#include <string_view>
#include <vector>

// 고급 설정: Geode 설정 창에 두기엔 많고 세세한 항목들 (부위별/색상 범위별 색 조정, 표면/유리 세부값, 배경 반전,
// 켜진 버튼, 리스트). 전용 창(openPopup)에서 고치고 저장 폴더의 advanced.json에 둔다.
// 키는 예전 Geode 설정 키와 같아서 테마 파일(Geode 설정 파일 형식)과 그대로 호환된다.
namespace theme::advanced {

enum class Kind { Number, Toggle, Choice, Color };

struct Field {
	char const* key;
	int tab;
	char const* name;
	char const* description;
	Kind kind;
	float min = 0, max = 1, step = 0.01f;
	float defNumber = 0;                 // Number, Toggle(0/1)
	std::vector<char const*> options;    // Choice
	int defChoice = 0;
	cocos2d::ccColor3B defColor{};       // Color
};

std::vector<Field> const& fields();
std::vector<char const*> const& tabs();
Field const* find(std::string_view key);

// 저장된 값 (키 → 값, Geode 설정 파일과 같은 형식). 없는 키는 기본값을 쓴다
matjson::Value const& values();

// 처음 한 번: advanced.json을 읽고, 없으면 Geode 설정 파일(settings.json)에 남아 있던 값을 옮겨 온다
void load();

// 고급 설정 창 열기
void openPopup();

// 모든 고급 설정이 기본값인지 / 전부 기본값으로 되돌리고 저장
// (Geode 설정 창의 "Reset All"과 항목 옆 초기화 화살표가 이걸 부른다: 설정 창의 "advanced" 항목이 사용자 정의 설정이라)
bool allDefault();
void resetAll();

}
