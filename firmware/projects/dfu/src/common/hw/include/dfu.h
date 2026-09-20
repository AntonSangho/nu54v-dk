#ifndef DFU_H_
#define DFU_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_DFU


typedef struct
{
  bool     is_valid;
  uint8_t  slot;
  bool     is_active;       // 지금 실행 중인 이미지
  bool     is_confirmed;    // 확정됨 (되돌아가지 않는다)
  bool     is_pending;      // 다음 부팅에 시도 (test)
  uint8_t  version[3];      // major.minor.revision
  uint32_t build;           // +build
  uint8_t  hash[32];
} dfu_img_t;


bool dfuInit(void);
bool dfuIsInit(void);

bool dfuGetImage(uint8_t slot, dfu_img_t *p_img);
bool dfuIsConfirmed(void);        // 실행 중인 이미지가 확정되었나
bool dfuConfirm(void);            // 실행 중인 이미지를 확정 (되돌리기 취소)
bool dfuRevert(void);             // 다음 부팅에 이전 이미지로 되돌리게 표시
bool dfuTest(void);               // slot1 이미지를 다음 부팅에 시도하게 표시
bool dfuErase(void);              // slot1 지우기

#ifdef _USE_HW_DFU_SERIAL
bool dfuSerialInit(void);
bool dfuSerialRxByte(uint8_t ch, uint8_t rx_data);   // cli 가 읽은 바이트를 먼저 보여 준다
void dfuSerialEnable(bool enable);
bool dfuSerialIsEnable(void);
void dfuSerialGetCnt(uint32_t *p_rx, uint32_t *p_tx, uint32_t *p_err);
void dfuSerialGetFragCnt(uint32_t *p_frag, uint32_t *p_drop);
uint16_t dfuSerialGetDropFrag(uint8_t **pp_buf);
#endif


#endif

#ifdef __cplusplus
}
#endif

#endif
