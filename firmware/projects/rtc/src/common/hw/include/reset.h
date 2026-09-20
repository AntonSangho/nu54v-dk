#ifndef RESET_H_
#define RESET_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


#ifdef _USE_HW_RESET


#define RESET_BIT_POWER       0     // 전원 인가 / 브라운아웃
#define RESET_BIT_PIN         1     // nRESET 핀 (SW6)
#define RESET_BIT_WDG         2     // 워치독
#define RESET_BIT_SOFT        3     // 소프트 리셋
#define RESET_BIT_ETC         4     // 그 밖 (lockup 등)
#define RESET_BIT_WAKE_GPIO   5     // System OFF 에서 GPIO(버튼)로 깨어남
#define RESET_BIT_WAKE_TIMER  6     // System OFF 에서 GRTC 로 깨어남
#define RESET_BIT_DEBUG       7     // 디버거
#define RESET_BIT_MAX         8


bool resetInit(void);
void resetToReset(void);

uint32_t resetGetBits(void);
uint32_t resetGetCause(void);

#endif


#ifdef __cplusplus
}
#endif

#endif
