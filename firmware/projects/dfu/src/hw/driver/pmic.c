/*
 * pmic.c — 배터리 충전기 BQ25186 (I2C 0x6A, Qwiic 과 같은 버스)
 *
 * 레지스터는 거의 읽기만 한다. 쓰는 것은 충전 전류(ICHG_CTRL) 하나뿐이다.
 * 배터리 용량에 맞춰야 하는 값이라 hw_def.h 의 HW_PMIC_ICHG_MA 로 설정하고,
 * HW_PMIC_ICHG_MAX_MA 를 넘지 못하게 막는다. 목표 전압·안전 타이머·VSYS 등은 건드리지 않는다.
 * 충전을 끄고 켜는 것은 /CE 핀으로 한다 (되돌리기 쉽다).
 *
 * 핀 (보드 DTS zephyr,user, 모두 솔더 브리지로 연결)
 *   /INT P1.11  open drain + 풀업. 상태가 바뀌면 Low → 인터럽트로 알 수 있다
 *   /PG  P2.08  입력 전원 있음
 *   /CE  P2.10  R10 10K 풀다운 → 띄워 두면 충전 허용. 필요할 때만 출력으로 잡는다
 *
 * 저전력 : 폴링하지 않는다. 상태 변화는 /INT 인터럽트로 알고, 레지스터는 그때 읽는다.
 */

#include "pmic.h"


#ifdef _USE_HW_PMIC
#include "cli.h"
#include "i2c.h"
#include <zephyr/drivers/gpio.h>


// BQ25186 레지스터
#define PMIC_REG_STAT0        0x00    // 충전 상태, 입력 전원
#define PMIC_REG_STAT1        0x01    // 현재 이상 상태
#define PMIC_REG_FLAG0        0x02    // 래치된 이상 (읽으면 지워짐)
#define PMIC_REG_VBAT_CTRL    0x03    // 충전 목표 전압
#define PMIC_REG_ICHG_CTRL    0x04    // 충전 전류, 충전 금지
#define PMIC_REG_TMR_ILIM     0x08    // 입력 전류 제한
#define PMIC_REG_MASK_ID      0x0C    // 장치 ID

#define PMIC_STAT0_VIN_GOOD   (1<<0)
#define PMIC_STAT0_THERMREG   (1<<1)
#define PMIC_STAT0_VINDPM     (1<<2)
#define PMIC_STAT0_VDPPM      (1<<3)
#define PMIC_STAT0_IINLIM     (1<<4)
#define PMIC_STAT0_CHG_MASK   (3<<5)
#define PMIC_STAT0_TS_OPEN    (1<<7)

#define PMIC_ICHG_DISABLE     (1<<7)


#if CLI_USE(HW_PMIC)
static void cliPmic(cli_args_t *args);
#endif
static void pmicGpioCallback(const struct device *port, struct gpio_callback *cb, uint32_t pins);
static uint16_t pmicIchgToMilliAmp(uint8_t code);
static uint8_t  pmicMilliAmpToIchg(uint16_t ma);


static const struct gpio_dt_spec int_pin = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), pmic_int_gpios);
static const struct gpio_dt_spec pg_pin  = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), pmic_pg_gpios);
static const struct gpio_dt_spec ce_pin  = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), pmic_ce_gpios);

static struct gpio_callback int_cb;
static void  (*event_func)(void) = NULL;

static bool     is_init = false;
static uint8_t  dev_id  = 0;
static uint32_t event_cnt = 0;

static const uint8_t i2c_ch   = HW_PMIC_I2C_CH;
static const uint8_t i2c_addr = HW_PMIC_I2C_ADDR;




bool pmicInit(void)
{
  bool ret = false;


  if (i2cIsBegin(i2c_ch) != true)
  {
    i2cBegin(i2c_ch, 400);
  }

  if (pmicReadReg(PMIC_REG_MASK_ID, &dev_id) == true)
  {
    ret = true;
  }

  // /PG : 입력
  if (gpio_is_ready_dt(&pg_pin))
  {
    gpio_pin_configure_dt(&pg_pin, GPIO_INPUT);
  }

  // /INT : 상태가 바뀔 때만 알려 준다 (폴링하지 않는다)
  if (gpio_is_ready_dt(&int_pin))
  {
    gpio_pin_configure_dt(&int_pin, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&int_pin, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&int_cb, pmicGpioCallback, BIT(int_pin.pin));
    gpio_add_callback(int_pin.port, &int_cb);
  }

  // /CE 는 건드리지 않는다. 풀다운으로 충전 허용 상태가 기본이다.

  is_init = ret;

  // 배터리 용량에 맞는 충전 전류를 설정한다 (이 레지스터만 쓴다).
  if (is_init)
  {
    pmicSetChargeCurrent(HW_PMIC_ICHG_MA);
  }

  logPrintf("[%s] pmicInit()\n", is_init ? "OK" : "E_");
  if (is_init)
  {
    logPrintf("     id   : 0x%02X\n", dev_id);
    logPrintf("     ichg : %d mA\n", pmicGetChargeCurrent());
  }

#if CLI_USE(HW_PMIC)
  cliAdd("pmic", cliPmic);
#endif

  return ret;
}

bool pmicIsInit(void)
{
  return is_init;
}

void pmicGpioCallback(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
  event_cnt++;

  if (event_func != NULL)
  {
    event_func();
  }
}

void pmicSetEventISR(void (*func)(void))
{
  event_func = func;
}

bool pmicReadReg(uint8_t addr, uint8_t *p_data)
{
  return i2cReadByte(i2c_ch, i2c_addr, addr, p_data, 10);
}

bool pmicIsPowerGood(void)
{
  if (!gpio_is_ready_dt(&pg_pin)) return false;

  return gpio_pin_get_dt(&pg_pin) == 1;
}

// /CE 핀으로만 충전을 끄고 켠다 (레지스터는 건드리지 않는다).
//
bool pmicSetChargeEnable(bool enable)
{
  if (!gpio_is_ready_dt(&ce_pin)) return false;

  return gpio_pin_configure_dt(&ce_pin, enable ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE) == 0;
}

// ICHG_CTRL 코드 → mA (데이터시트: 31 이하는 1 mA, 넘으면 10 mA 간격)
//
static uint16_t pmicIchgToMilliAmp(uint8_t code)
{
  return (code > 31) ? (uint16_t)(40 + (code - 31) * 10) : (uint16_t)(code + 5);
}

static uint8_t pmicMilliAmpToIchg(uint16_t ma)
{
  if (ma < 5) ma = 5;

  if (ma > 36)
    return (uint8_t)constrain(31 + ((int)ma - 40 + 5) / 10, 31, 127);

  return (uint8_t)(ma - 5);
}

// 충전 전류 설정. 배터리 용량에 맞는 값이어야 한다 (보통 0.5C 이하).
// HW_PMIC_ICHG_MAX_MA 를 넘는 값은 잘라낸다.
//
bool pmicSetChargeCurrent(uint16_t ma)
{
  uint8_t reg;
  uint8_t code;


  if (is_init != true) return false;

  if (ma > HW_PMIC_ICHG_MAX_MA)
  {
    logPrintf("[E_] pmicSetChargeCurrent() : %d mA > 최대 %d mA\n", ma, HW_PMIC_ICHG_MAX_MA);
    ma = HW_PMIC_ICHG_MAX_MA;
  }

  if (pmicReadReg(PMIC_REG_ICHG_CTRL, &reg) != true) return false;

  code = pmicMilliAmpToIchg(ma);

  // 충전 금지 비트(7)는 그대로 둔다.
  reg = (reg & PMIC_ICHG_DISABLE) | (code & 0x7F);

  return i2cWriteByte(i2c_ch, i2c_addr, PMIC_REG_ICHG_CTRL, reg, 10);
}

uint16_t pmicGetChargeCurrent(void)
{
  uint8_t reg;

  if (pmicReadReg(PMIC_REG_ICHG_CTRL, &reg) != true) return 0;

  return pmicIchgToMilliAmp(reg & 0x7F);
}

static uint16_t pmicIlimToMilliAmp(uint8_t code)
{
  static const uint16_t tbl[8] = {50, 100, 200, 300, 400, 500, 700, 1100};

  return tbl[code & 0x07];
}

bool pmicGetInfo(pmic_info_t *p_info)
{
  uint8_t stat0, stat1, flag0, vbat, ichg, ilim;


  if (is_init != true) return false;

  if (pmicReadReg(PMIC_REG_STAT0, &stat0) != true) return false;
  if (pmicReadReg(PMIC_REG_STAT1, &stat1) != true) return false;
  if (pmicReadReg(PMIC_REG_FLAG0, &flag0) != true) return false;
  if (pmicReadReg(PMIC_REG_VBAT_CTRL, &vbat) != true) return false;
  if (pmicReadReg(PMIC_REG_ICHG_CTRL, &ichg) != true) return false;
  if (pmicReadReg(PMIC_REG_TMR_ILIM, &ilim) != true) return false;

  switch (stat0 & PMIC_STAT0_CHG_MASK)
  {
    case (0<<5): p_info->chg_state = PMIC_CHG_IDLE; break;
    case (1<<5): p_info->chg_state = PMIC_CHG_CC;   break;
    case (2<<5): p_info->chg_state = PMIC_CHG_CV;   break;
    default:     p_info->chg_state = PMIC_CHG_DONE; break;
  }

  p_info->vin_good    = (stat0 & PMIC_STAT0_VIN_GOOD) != 0;
  p_info->ilim_active = (stat0 & PMIC_STAT0_IINLIM) != 0;
  p_info->chg_disable = (ichg & PMIC_ICHG_DISABLE) != 0;
  p_info->pg_pin      = pmicIsPowerGood();

  p_info->vbat_target_mv = 3500 + (vbat & 0x7F) * 10;
  p_info->ichg_ma        = pmicIchgToMilliAmp(ichg & 0x7F);
  p_info->ilim_ma        = pmicIlimToMilliAmp(ilim);

  p_info->stat0 = stat0;
  p_info->stat1 = stat1;
  p_info->flag0 = flag0;

  return true;
}


#if CLI_USE(HW_PMIC)
static const char *pmicChgStateStr(PmicChgState_t state)
{
  switch (state)
  {
    case PMIC_CHG_IDLE: return "idle (충전 안 함)";
    case PMIC_CHG_CC:   return "charging - CC";
    case PMIC_CHG_CV:   return "charging - CV";
    default:            return "done 또는 충전 금지";
  }
}

static void cliPmicInfo(void)
{
  pmic_info_t info;


  if (pmicGetInfo(&info) != true)
  {
    cliPrintf("pmicGetInfo() Fail\n");
    return;
  }

  cliPrintf("state   : %s%s\n", pmicChgStateStr(info.chg_state), info.chg_disable ? "  [CHG_DISABLE]" : "");
  cliPrintf("input   : %s%s, /PG pin %s\n",
            info.vin_good ? "VIN good" : "no input",
            info.ilim_active ? ", 입력 전류 제한 중" : "",
            info.pg_pin ? "active" : "-");
  cliPrintf("setting : target %d.%02d V, charge %d mA, input limit %d mA\n",
            info.vbat_target_mv / 1000, (info.vbat_target_mv % 1000) / 10,
            info.ichg_ma, info.ilim_ma);

  cliPrintf("health  : ");
  {
    bool bad = false;

    if (info.stat0 & PMIC_STAT0_THERMREG) { cliPrintf("[thermal regulation] "); bad = true; }
    if (info.stat0 & PMIC_STAT0_TS_OPEN)  { cliPrintf("[TS open] ");            bad = true; }
    if (info.stat1 & (1<<7))              { cliPrintf("[VIN over-voltage] ");   bad = true; }
    if (info.stat1 & (1<<6))              { cliPrintf("[battery under-volt] "); bad = true; }
    if (info.stat1 & (1<<2))              { cliPrintf("[safety timer] ");       bad = true; }
    switch (info.stat1 & 0x18)
    {
      case 0x08: cliPrintf("[battery hot/cold] "); bad = true; break;
      case 0x10: cliPrintf("[battery cool] ");     bad = true; break;
      case 0x18: cliPrintf("[battery warm] ");     bad = true; break;
      default: break;
    }
    cliPrintf("%s\n", bad ? "" : "ok");
  }

  if (info.flag0 != 0)
  {
    cliPrintf("latched : FLAG0 0x%02X (마지막으로 읽은 뒤에 생긴 이상)\n", info.flag0);
  }
  cliPrintf("raw     : STAT0 %02X STAT1 %02X  int %d회\n", info.stat0, info.stat1, event_cnt);
}

void cliPmic(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("id      : 0x%02X (i2c ch%d, addr 0x%02X)\n", dev_id, i2c_ch + 1, i2c_addr);
    cliPmicInfo();
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "show"))
  {
    while(cliKeepLoop())
    {
      cliPmicInfo();
      cliPrintf("\n");
      delay(1000);
    }
    cliRead();
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "reg"))
  {
    const uint8_t reg_list[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};

    for (int i=0; i<sizeof(reg_list); i++)
    {
      uint8_t data;

      if (pmicReadReg(reg_list[i], &data) == true)
        cliPrintf("0x%02X : 0x%02X\n", reg_list[i], data);
      else
        cliPrintf("0x%02X : Fail\n", reg_list[i]);
    }
    ret = true;
  }

  // /CE 핀으로만 제어한다. off 로 두면 충전하지 않는다.
  // 충전 전류 설정 (기본값은 hw_def.h 의 HW_PMIC_ICHG_MA)
  if (args->argc == 2 && args->isStr(0, "ichg"))
  {
    uint16_t ma = (uint16_t)args->getData(1);
    bool     set_ret = pmicSetChargeCurrent(ma);

    cliPrintf("요청 %d mA → 설정 %d mA %s(최대 %d mA)\n",
              ma, pmicGetChargeCurrent(), set_ret ? "" : ": Fail ", HW_PMIC_ICHG_MAX_MA);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "ce"))
  {
    bool enable = args->isStr(1, "on");

    cliPrintf("charge enable %s : %s\n", enable ? "on" : "off",
              pmicSetChargeEnable(enable) ? "OK" : "Fail");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("pmic info\n");
    cliPrintf("pmic show\n");
    cliPrintf("pmic reg\n");
    cliPrintf("pmic ichg mA   (최대 %d mA)\n", HW_PMIC_ICHG_MAX_MA);
    cliPrintf("pmic ce on:off\n");
  }
}
#endif

#endif
