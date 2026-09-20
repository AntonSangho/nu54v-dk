#ifndef BLE_NUS_H_
#define BLE_NUS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_BLE_NUS


bool     bleNusIsReady(void);         // 연결 + notify 켜짐 (cli 를 넘겨도 되는 상태)
uint32_t bleNusGetRxCnt(void);
uint32_t bleNusGetTxCnt(void);


#endif

#ifdef __cplusplus
}
#endif

#endif
