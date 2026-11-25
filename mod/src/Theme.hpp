#pragma once

#include <Geode/Geode.hpp>
#include <asp/fs/std.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace theme {
// Geode 6은 경로를 asp::fs::Path로 주고받는다. 우리 코드는 std::filesystem::path를 쓰고, Geode와 주고받는 곳에서만 바꾼다
inline std::filesystem::path stdPath(asp::fs::PathView p) {
	return std::filesystem::path(p.native());
}
inline asp::fs::Path geodePath(std::filesystem::path const& p) {
	return asp::fs::Path::fromStdPath(p);
}
// 로그/이미지 로더용 UTF-8 문자열 (예전 utils::string::pathToString)
inline std::string pathString(std::filesystem::path const& p) {
	return geodePath(p).string();
}
}

// 테마 엔진: 원본 시트 + 마스크 시트(R=테두리, G=바탕, B=글리프, 원본 알파가 곱해진 값)를
// 셰이더로 다시 칠한다. 데이터는 tools/export_mod_data.py가 만든 resources/<팩id>.json.
namespace theme {

// 프레임 하나를 그릴 때 셰이더에 넘기는 값 (불러올 때 미리 계산)
struct FrameInfo {
	float rect[4];      // 샘플링 허용 UV 영역 (반 텍셀 안쪽): u0, v0, u1, v1
	float trim[4];      // 트림된 영역이 노드(원본 크기) 안에서 차지하는 위치, y-down 0..1: x, y, w, h
	float rotated;      // 시트 안에서 시계 방향 90° 회전되어 저장됨
	float style[3];     // 테두리 스타일, 글리프 스타일 (0 없음, 1 그라데이션, 2 흰색), 원래 바탕색(0..1)
	// 팩이 다시 그리지 않은 기본 스타일 스프라이트 (Geode 버튼): 0 아님, 1 기본, 2 비활성 느낌(선택 안 된 탭/회색).
	// "원본 색" 대신 활성 팩의 스타일(윤곽선 유무, 바탕색, 아이콘 색)을 따라 칠한다.
	float vanilla = 0;
	float kind = 0;     // 0 일반, 1 리스트 테두리 (GJ_table_*, 유리 적용 범위), 2 분할 없음 (그림 전체가 원래 색의 바탕)
};

struct Sheet {
	std::string name;
	int width = 0;
	int height = 0;
	std::filesystem::path dir;   // 마스크/거리장 파일이 있는 폴더 (모드 리소스 또는 팩의 theme-data)
	std::string maskFile;
	std::string probe;  // 이 시트에 들어 있는 프레임 이름 (실제 로드된 텍스쳐를 찾는 데 사용)
	std::unordered_map<uint32_t, FrameInfo> byOrigin;   // (x << 16 | y) → 프레임
	// 배치 확인: 데이터에 적힌 프레임 이름별 시트 안 영역 (pack.json "names"가 있을 때만).
	// 팩이 업데이트되어 배치가 바뀌었는데 데이터가 옛것이면 다른 버튼의 마스크가 엉뚱한 버튼에 칠해지므로,
	// 실제 시트와 하나라도 다르면 이 시트 데이터는 쓰지 않는다
	struct FrameCheck {
		std::string name;
		int x = 0, y = 0, w = 0, h = 0;
		bool rotated = false;
	};
	std::vector<FrameCheck> checks;
	cocos2d::CCTexture2D* checkedTex = nullptr;   // 마지막으로 확인한 실제 시트 텍스쳐 (바뀌면 다시 확인)
	bool layoutOk = true;
	cocos2d::CCTexture2D* mask = nullptr;
	cocos2d::CCTexture2D* sdf = nullptr;   // 몸통 가장자리 거리장 (절반 해상도, tools/make_sdf.py). 없으면 모양 효과 없음
	bool addon = false;  // 팩과 무관한 추가 시트 (Geode 버튼, resources/geode.json)
};

// 단독 이미지 (GJ_button_*, GJ_square*, 슬라이더 등): 텍스쳐 전체가 프레임 하나
struct Single {
	std::string file;   // "GJ_button_01" (접미사/확장자 없이)
	int width = 0;
	int height = 0;
	std::filesystem::path dir;
	std::string maskFile;
	FrameInfo frame{};
	cocos2d::CCTexture2D* mask = nullptr;
	cocos2d::CCTexture2D* sdf = nullptr;
};

// 프레임을 그릴 때 같이 묶는 텍스쳐
struct Textures {
	cocos2d::CCTexture2D* mask = nullptr;
	cocos2d::CCTexture2D* sdf = nullptr;
};

struct Pack {
	std::string id;
	std::string name;
	std::string profile;   // "diamond" | "generic"
	// 팩 스타일 요약 (프레임 표에서 계산): Geode 버튼을 이 팩처럼 칠할 때 쓴다
	bool plain = false;      // 대부분의 버튼에 윤곽선이 없다 (Diamond Vortex)
	float fill = 17 / 255.f; // 가장 흔한 바탕색 (회색 0..1)
	// "vanilla" = 게임 기본 리소스(Resources 폴더)에서 온 시트에만 적용 (같은 배치를 쓰는 다른 팩과 구분)
	std::string match;
	// 텍스쳐 팩 안 theme-data에서 읽은 팩이면 그 pack.json 경로와 수정 시각 (바뀌면 다시 읽는다)
	std::filesystem::path dataFile;
	std::filesystem::file_time_type dataTime{};
	std::vector<Sheet> sheets;
	std::vector<Single> singles;
};

// 색 조정: 위에서 정한 색을 기준으로 색상을 돌리고 채도/밝기를 곱한다 (덮어쓰기가 아니라 상대 조정).
// colorize면 색상을 hue로 고정한다 (포토샵 색상화. 채도/밝기는 원래 색 기준이라 흰색/검은색은 그대로)
struct Adjust {
	float hue = 0;   // 도: 회전량 또는 고정할 색상
	float sat = 1;   // 배율
	float val = 1;   // 배율
	bool colorize = false;
	bool identity() const { return !colorize && hue == 0 && sat == 1 && val == 1; }
};

// 색상 범위별 조정 (포토샵 색조/채도의 빨강~마젠타 채널). 순서: 빨강, 노랑, 초록, 시안, 파랑, 마젠타
inline constexpr char const* ADJUST_RANGES[6] = {"reds", "yellows", "greens", "cyans", "blues", "magentas"};

// 셰이더에 넘기는 테마 설정 (미리보기의 theme 객체와 같은 의미)
struct Settings {
	bool enabled = true;
	float gradMode = 0;          // 0 색상 회전(HSV), 1 사용자 색 2~3개, 2 단색
	float hueStart = 120, hueRange = 240, sat = 0.4f, val = 1;
	cocos2d::ccColor3B c1{153, 255, 199}, c2{195, 153, 255}, c3{255, 153, 179};
	bool use3 = true;
	float angle = 0, flow = 0, spin = 0;
	bool flowBody = true;        // 원본 그림이 바탕인 팩(바닐라 구조)에서 색 흐름이 바탕에도 걸린다 (false = 아이콘만)
	float outlineSrc = 0;        // 0 테마, 1 원본 색
	float fillMode = 0;          // 0 원본(프레임별), 1 직접 지정, 2 두 색 그라데이션, 3 테두리 그라데이션을 어둡게
	cocos2d::ccColor3B fill{20, 20, 20};
	cocos2d::ccColor3B fill2{48, 40, 64};   // 그라데이션 끝 색
	float fillAngle = 90;        // 바탕 그라데이션 방향 (90 = 위 → 아래)
	float fillTheme = 0.25f;     // 3번 모드에서 테두리 색의 밝기 비율
	cocos2d::ccColor3B accent{255, 255, 255};
	float glyphMode = 0;         // 0 자동, 1 흰색 부분 색, 2 그라데이션, 3 직접 지정, 4 원본 색
	cocos2d::ccColor3B glyphColor{255, 255, 255};
	float whiteOutline = 0;      // 0 흰색 부분 색, 1 그라데이션
	int bigFont = 0;             // 0 원본, 1 그라데이션, 2 단색
	int goldFont = 0;
	cocos2d::ccColor3B textColor{255, 255, 255};   // bigFont 단색
	cocos2d::ccColor3B goldColor{255, 255, 255};   // goldFont 단색
	bool debugStats = false;
	// 애니메이션 (프리셋과 무관하게 항상 이 설정을 따른다)
	int pressStyle = 0;          // 0 GD 기본, 1 눌림(축소), 2 팝 + 빛남, 3 없음, 4 색 반전, 5 쫀득(스프링)
	float jelly = 1;             // 쫀득한 누름의 세기 (납작해지는 정도와 출렁임)
	float hoverGlow = 0.35f;     // 마우스를 올렸을 때 빛남 세기 (0 = 끔)
	int idle = 0;                // 0 없음, 1 빛 펄스, 2 반짝임(빛 띠)
	// 리스트 칸 배경 (GD 기본 갈색 두 가지를 번갈아 쓰는 CCLayerColor)
	int listMode = 0;            // 0 원본(갈색), 1 어둡게, 2 직접 지정
	cocos2d::ccColor3B list1{42, 42, 48}, list2{52, 52, 60};
	// 표면 효과 (프리셋과 무관, 테마 파일이 덮어쓸 수 있다). 크기는 UHD 텍스쳐 픽셀
	float opacity = 1;           // 버튼 전체 불투명도
	float fillOpacity = 1;       // 바탕만 불투명도 (테두리/아이콘은 그대로)
	float bevel = 0;             // 엠보싱(베벨) 세기
	float bevelSize = 12;
	float innerShadow = 0;       // 안쪽 그림자 세기
	float innerShadowSize = 14;
	bool fillOpacityIconsOnly = true;   // 바탕 투명도를 아이콘 있는 버튼에만 (아이콘 없는 버튼이 통째로 사라지지 않게)
	float iconBevel = 0;         // 아이콘 엠보싱 세기 (아이콘 자체 가장자리 기준)
	float iconBevelSize = 6;
	// 유리 (바탕 자리에 버튼 뒤 배경을 블러해서 깐다). 적용 범위는 glassScope
	float glass = 0;             // 배경이 비치는 정도 (0 = 끔)
	float glassBlur = 0.5f;      // 블러 반경
	float glassBlurSize = 1;     // 블러 크기 배율 (1 = 기본, 4 = 형체만 남을 만큼 크게)
	float glassSaturation = 1;   // 비치는 배경의 채도 (1 = 그대로, 1보다 크면 애플 블러처럼 색이 산다)
	float glassNoise = 0;        // 젖빛 유리 노이즈 입자 (윈도우 아크릴)
	float glassRefract = 0.3f;   // 가장자리 굴절 (리퀴드 글라스)
	float glassRim = 0.4f;       // 가장자리 빛
	float glassEdge = 24;        // 굴절/빛이 생기는 가장자리 폭 (텍스쳐 픽셀)
	float glassDispersion = 0;   // 색수차 (가장자리에서 빨강/파랑이 갈라짐, 피그마 Dispersion)
	float glassSpecular = 0.5f;  // 빛 쪽 가장자리의 반사광 + 마우스 근처 유리면의 은은한 빛
	bool glassFollowMouse = true;   // 유리의 빛(테두리 빛, 반사광)이 마우스 쪽에서 온다. 끄면 왼쪽 위 고정
	int glassScope = 0;          // 0 모든 버튼 (메뉴 아이템 안), 1 아이콘 있는 버튼만, 2 팝업 판/장식까지 전부
	bool glassListFrames = true; // 리스트 테두리(GJ_table_*)에도 유리
	// 배경 반전: 고른 부분은 자기 색 대신 뒤에 그려진 화면을 반전해서 보여준다
	bool invOutline = false, invIcon = false, invFill = false, invText = false;
	// 색 조정 (프리셋과 무관, 테마 파일이 덮어쓸 수 있다). adjAll은 부위 구분 없이 그림 전체
	// (테마가 나누지 못한 원본 유지 부분 포함), 부위별 값은 그 위에 더해진다
	Adjust adjAll, adjOutline, adjFill, adjIcon;
	// 색상 범위별 조정 (테마 파일/미리보기 전용, 설정 창에는 없다). 부위 조정보다 먼저, 원래 색상 기준으로 고른다
	std::array<Adjust, 6> adjRanges;
	float hueSpeed = 0;          // 색조 애니메이션: 버튼 전체 색상이 초당 이만큼(도) 돈다 (0 = 끔)
	float hoverHue = 0;          // 마우스를 올리면 색상을 이만큼(도) 부드럽게 돌린다
	Adjust activeAdj;            // 켜진 버튼(체크박스, 탭 등)의 색조 변화 (그림 전체, 분할 없는 팩에도 걸린다)
	float colorizeGray = 0;      // 색상 고정할 때 무채색(흰색/검은색 제외)에도 입힐 최소 채도 (0 = 원래 채도 그대로)
	// 리스트 칸 (CCLayerColor): 유리 정도와 불투명도
	float listGlass = 0;
	float listOpacity = 1;
	// Geode 버튼(메인 메뉴 Geode 버튼, 모드 목록의 원형/탭 버튼)도 같은 스타일로
	bool geodeButtons = true;
	// 켜진 버튼(체크박스, 토글, 선택된 탭)의 색: 0 다른 버튼과 같게, 1 단색, 2 그라데이션
	int activeMode = 0;
	cocos2d::ccColor3B active1{125, 255, 176}, active2{125, 200, 255};
	bool activeFill = false;     // 켜진 버튼의 바탕색도 바꾼다
	cocos2d::ccColor3B activeFillColor{32, 70, 58};
};

// 노드 하나를 그릴 때의 효과 값
struct Fx {
	float glow = 0;
	float shine = 0;
	float phase = 0;
	float active = 0;   // 켜진 버튼이면 1 (활성 색 설정이 하나라도 켜져 있을 때만)
	float invert = 0;   // 누름 색 반전 정도 (0..1)
	float glass = 0;    // 유리 배경이 준비됐으면 1 (prepareGlass)
	float gray = 0;     // 잠긴 버튼처럼 GD가 흑백으로 그리는 스프라이트(CCSpriteGrayscale)면 1
	float hover = 0;    // 마우스 올림 정도 (0..1, 부드럽게 따라간다). 호버 색조에 쓴다
	float squish = 0;   // 쫀득한 누름으로 버튼이 찌그러진 정도 (가로/세로 크기 차이 비율). 유리 굴절이 같이 출렁인다
	float pass = 0;     // 배경 반전 그리기 단계: 0 한 번에, 1 반전 부분을 비운 첫 번째, 2 반전 부분만 (setPass)
};

// 디버그 통계 (설정 debug-stats가 켜졌을 때 5초마다 로그)
struct Stats {
	double spriteDraws = 0, spriteThemed = 0;
	double batchDraws = 0, batchThemed = 0, labelThemed = 0;
	double hookSeconds = 0;   // 우리 훅이 원래 draw 전에 쓴 시간 합계 (CPU. 유리 배경 만들기 명령 제출 포함)
	double glassBuilds = 0;   // 유리 배경(화면 복사 + 블러)을 만든 횟수
	double glassDraws = 0;    // 유리로 그린 테마 그리기 수
	double listGlassDraws = 0;
	double invertDraws = 0;   // 배경 반전용 두 번째 그리기 수
	double worstFrame = 0;    // 가장 긴 프레임 (초)
	double hookFrameMax = 0;  // 한 프레임 안에서 우리 훅이 쓴 시간의 최댓값 (초)
	double refreshMax = 0;    // 주기적 팩 확인(refreshBindings) 한 번의 최댓값 (초)
	double textureLoad = 0;   // GD가 텍스쳐를 디스크에서 읽은 시간 합계 (초, addImage. 우리 비용 아님)
	double textureLoadMax = 0;
	double textureLoads = 0;
	unsigned frames = 0;
};

void loadPacks();

// 텍스쳐 캐시 훅에서 호출: 단독 이미지 텍스쳐가 (다시) 로드되면 기록
void onTextureLoaded(char const* path, cocos2d::CCTexture2D* texture);

// 개별 스프라이트: 시트 프레임 또는 단독 이미지면 정보를 돌려준다 (마스크/거리장 텍스쳐는 out)
FrameInfo const* lookup(cocos2d::CCTexture2D* texture, cocos2d::ccV3F_C4B_T2F_Quad const& quad, Textures* out);
// 배치 노드(9분할 등): 단독 이미지만 대상
FrameInfo const* lookupSingle(cocos2d::CCTexture2D* texture, Textures* out);
// 라벨: 0 원본 유지, 1 그라데이션, 2 단색 (단색이면 colorOut에 색)
int fontMode(cocos2d::CCLabelBMFont* label, cocos2d::ccColor3B& colorOut);

// 테마 셰이더 (없거나 컴파일 실패면 nullptr). 프레임당 한 번 GL 컨텍스트 재생성 여부를 확인하고 복구한다.
cocos2d::CCGLProgram* program();
// CCShaderCache::reloadDefaultShaders 훅에서 호출: GD가 GL 리소스를 다시 만들 때
void onShadersReloaded();
bool enabled();
// 지금 적용 중인 설정 (프리셋/테마 파일까지 반영된 최종 값)
Settings const& current();

Stats& stats();
bool statsEnabled();

// 셰이더 링크 오류 로그. CCGLProgram::programLog는 맥(M1)판에 없어 GL에서 직접 읽는다.
std::string linkLog(cocos2d::CCGLProgram* p);
void tickStats();

// 셰이더를 쓰는 상태에서 호출 (전역 설정은 프레임당 한 번만 보낸다)
// bounds: 노드 공간 정점 범위 (minX, minY, w, h). 9분할/라벨은 텍스쳐 좌표 대신 위치로 그라데이션을 계산한다.
void applySprite(FrameInfo const& frame, Textures const& tex, Fx const& fx);
void applyBatch(FrameInfo const& frame, Textures const& tex, float const bounds[4], Fx const& fx);
void applyText(float const bounds[4], int mode, cocos2d::ccColor3B color, Fx const& fx);
// 테마 대상이 아닌 텍스쳐를 효과(색 반전)만 걸어 그린다. rect: 샘플링할 UV 범위 (u0, v0, u1, v1)
void applyPlain(float const rect[4], Fx const& fx);

// 색 반전 누름이 진행 중일 수 있는 동안만 true (아닐 때 모든 스프라이트마다 버튼을 찾지 않도록)
bool invertWindow();
// 버튼을 누르기 시작할 때 호출 (색 반전 창을 연다)
void notePress();

// 이 노드가 버튼(CCMenuItem)의 일부면 호버/누름/대기 효과와 켜짐 상태를 계산한다 (버튼이 아니면 효과 없음).
// 노드별로 빛남 값을 부드럽게 따라가게 하는 상태를 들고 있다.
Fx fxFor(cocos2d::CCNode* node);

// 배경 반전을 쓰는 부분이 있는지 (있으면 테마 대상을 두 번 그린다: 1단계 → 반전 블렌드로 2단계)
bool invertsParts();
bool invertsText();
// 이미 적용한 그리기의 단계만 바꾼다 (두 번째 그리기 직전)
void setPass(float pass);
// 두 번째 그리기의 블렌드: 결과 = 흰색 × (1 - 화면색) + 화면색 × (1 - 알파) → 덮는 비율만큼 뒷배경이 반전된다
inline constexpr cocos2d::ccBlendFunc INVERT_BLEND{GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA};

// 유리 효과 대상이면 버튼 뒤 배경(블러)을 준비하고 true. GL 상태를 바꾸므로 테마 셰이더 use() 전에 부른다
bool prepareGlass(cocos2d::CCNode* node, FrameInfo const& frame);

// 테마가 켜져 있고 지원 팩이 활성일 때의 누름 스타일 (아니면 0 = GD 기본)
int pressStyle();
// 쫀득한 누름 세기
float jellyStrength();

// 리스트 칸 배경색. role 1 = 짝수 칸(어두운 갈색), 2 = 홀수 칸(밝은 갈색).
// 바꿔야 하면 out에 색을 넣고 true, 원본 유지면 false.
bool listColor(int role, cocos2d::ccColor3B& out);
// 리스트 칸 불투명도 배율 (1 = 그대로)
float listOpacity();
// 리스트 칸 유리: 켜져 있으면 배경을 준비하고 유리 셰이더(설정/텍스쳐 적용됨)를 돌려준다, 아니면 nullptr
cocos2d::CCGLProgram* prepareListGlass(cocos2d::CCNode* node);

}
