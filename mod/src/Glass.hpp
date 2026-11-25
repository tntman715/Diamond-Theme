#pragma once

#include <Geode/Geode.hpp>

// 유리 효과용 배경: 지금까지 그려진 화면을 복사해 1/4 크기로 줄이고 가우시안 블러한 텍스쳐.
// 유리 버튼을 처음 그릴 때(레이어마다 한 번) 만든다. 그 버튼보다 먼저 그려진 것 = 버튼 뒤의 배경.
namespace theme::glass {

// node가 속한 레이어(장면 바로 아래 노드)의 배경을 준비한다. 이번 프레임에 같은 레이어로 이미 만들었으면 재사용.
// 프로그램/텍스쳐/프레임버퍼 상태를 바꾸므로 테마 셰이더를 쓰기(use) 전에 불러야 한다.
// blur: 0..1 (블러 반경). 준비되면 true
bool prepare(cocos2d::CCNode* node, float blur, float size);

// 준비된 배경 텍스쳐(블러)와 블러 전 원본 복사본, 복사한 화면 영역 (뷰포트: x, y, w, h)
GLuint texture();
GLuint sharpTexture();
float const* viewport();

// 단색 레이어(리스트 칸)용 유리 셰이더: prepare가 성공한 뒤 부른다. 사용 상태로 만들고 배경 텍스쳐를 묶어 돌려준다.
// amount: 배경이 비치는 정도, frost: 블러 비율
cocos2d::CCGLProgram* colorProgram(float amount, float frost, float saturation, float noise);

// GL 컨텍스트가 바뀌었을 때: 옛 컨텍스트 객체는 지우지 않고 잊는다 (지우면 새 컨텍스트의 같은 번호를 지운다)
void onContextLost();

}
