#ifndef SHTC3_H_
#define SHTC3_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_SHTC3


typedef struct
{
  int16_t temp;       // 0.01 ℃ 단위   (예: 2534 = 25.34 ℃)
  int16_t humi;       // 0.01 %RH 단위 (예: 4512 = 45.12 %)
} shtc3_info_t;


bool shtc3Init(void);
bool shtc3IsInit(void);
bool shtc3GetID(uint16_t *p_id);
bool shtc3SetLowPower(bool enable);
bool shtc3Read(shtc3_info_t *p_info);

#endif

#ifdef __cplusplus
}
#endif

#endif
