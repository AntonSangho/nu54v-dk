#ifndef TEMP_H_
#define TEMP_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_TEMP


bool    tempInit(void);
bool    tempIsInit(void);

// 칩 내부 온도. 0.01 ℃ 단위 (예: 2534 = 25.34 ℃)
bool    tempRead(int16_t *p_temp);


#endif

#ifdef __cplusplus
}
#endif

#endif
