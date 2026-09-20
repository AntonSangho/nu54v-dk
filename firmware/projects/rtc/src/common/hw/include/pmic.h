#ifndef PMIC_H_
#define PMIC_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_PMIC


typedef enum
{
  PMIC_CHG_IDLE,          // 충전 허용 상태지만 충전하지 않음
  PMIC_CHG_CC,            // 정전류 충전
  PMIC_CHG_CV,            // 정전압 충전
  PMIC_CHG_DONE,          // 충전 완료 또는 충전 금지
} PmicChgState_t;

typedef struct
{
  PmicChgState_t chg_state;
  bool     vin_good;          // 입력 전원 있음 (STAT0)
  bool     pg_pin;            // /PG 핀 상태
  bool     ilim_active;       // 입력 전류 제한 동작 중
  bool     chg_disable;       // ICHG_CTRL 의 충전 금지 비트

  uint16_t vbat_target_mv;    // 충전 목표 전압
  uint16_t ichg_ma;           // 충전 전류 설정
  uint16_t ilim_ma;           // 입력 전류 제한

  uint8_t  stat0;
  uint8_t  stat1;
  uint8_t  flag0;             // 읽으면 지워지는 래치 이상 플래그
} pmic_info_t;


bool pmicInit(void);
bool pmicIsInit(void);
bool pmicGetInfo(pmic_info_t *p_info);
bool pmicReadReg(uint8_t addr, uint8_t *p_data);

bool     pmicSetChargeCurrent(uint16_t ma);   // 충전 전류 (HW_PMIC_ICHG_MAX_MA 로 제한)
uint16_t pmicGetChargeCurrent(void);

bool pmicIsPowerGood(void);         // /PG 핀
bool pmicSetChargeEnable(bool enable);   // /CE 핀 (기본은 띄워 둠 = 충전 허용)

void pmicSetEventISR(void (*func)(void));   // /INT 이벤트


#endif

#ifdef __cplusplus
}
#endif

#endif
