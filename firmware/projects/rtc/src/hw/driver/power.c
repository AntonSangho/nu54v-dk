/*
 * power.c — 전원 관리 (System OFF, 전원 모드)
 *
 * System OFF : 가장 깊은 절전. RAM 도 꺼지고, 깨어나면 리셋으로 다시 부팅한다.
 *   - 깨우기 : 버튼 SW1~SW4 (GPIO SENSE, LEVEL) + 선택적으로 GRTC 타이머
 *   - 들어가기 전에 LED 를 끄고 핀을 분리(ledToSleep), UART RX 를 닫는다.
 *   - 깨어난 원인은 다음 부팅의 resetInit() 이 알려 준다 (RESET_BIT_WAKE_GPIO / WAKE_TIMER).
 *
 * 디버거가 붙어 있으면 System OFF 가 흉내(emulated) 모드로 동작한다.
 * 실제 전류를 잴 때는 전원을 한 번 껐다 켜서 디버그 연결을 끊는다.
 */

#include "power.h"


#ifdef _USE_HW_POWER
#include "cli.h"
#include "led.h"
#ifdef _USE_HW_RTC
#include "rtc.h"
#endif
#include "uart.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/timer/nrf_grtc_timer.h>
#include <zephyr/sys/poweroff.h>
#include <hal/nrf_regulators.h>


#if CLI_USE(HW_POWER)
static void cliPower(cli_args_t *args);
#endif


// System OFF 에서 깨우는 버튼 (보드 DTS sw0~sw3, sense-edge-mask 핀)
static const struct gpio_dt_spec wake_pin[] =
{
  GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios),
  GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios),
  GPIO_DT_SPEC_GET(DT_ALIAS(sw2), gpios),
  GPIO_DT_SPEC_GET(DT_ALIAS(sw3), gpios),
};




bool powerInit(void)
{
  logPrintf("[OK] powerInit()\n");
  logPrintf("     Regulator : %s\n", powerIsDcdc() ? "DC/DC" : "LDO");

#if CLI_USE(HW_POWER)
  cliAdd("power", cliPower);
#endif
  return true;
}

bool powerIsDcdc(void)
{
  return (NRF_REGULATORS->VREGMAIN.DCDCEN & REGULATORS_VREGMAIN_DCDCEN_VAL_Msk) != 0;
}

// System OFF 로 들어간다. 성공하면 돌아오지 않는다 (깨어나면 리셋).
// wake_ms > 0 이면 버튼과 함께 GRTC 로도 깨운다.
//
bool powerOff(uint32_t wake_ms)
{
  for (int i=0; i<ARRAY_SIZE(wake_pin); i++)
  {
    if (gpio_pin_configure_dt(&wake_pin[i], GPIO_INPUT) < 0 ||
        gpio_pin_interrupt_configure_dt(&wake_pin[i], GPIO_INT_LEVEL_ACTIVE) < 0)
    {
      logPrintf("[E_] powerOff() : wake pin %d\n", i);
      return false;
    }
  }

#ifdef _USE_HW_RTC
  rtcSync();      // System OFF 전에 시각을 보존 RAM 에 적는다
#endif
#ifdef _USE_HW_LED
  ledToSleep();
#endif
  for (int i=0; i<HW_UART_MAX_CH; i++)
  {
    uartClose(i);
  }
  delay(10);    // UART RX 정지(비동기) 완료

  // GRTC 깨우기는 System OFF 바로 앞에서 준비한다.
  // z_nrf_grtc_wakeup_prepare() 는 다른 GRTC 채널(커널 타이머 포함)을 모두 끄므로
  // 이 뒤에 delay()/k_sleep() 같은 커널 타이머를 쓰면 안 된다.
  if (wake_ms > 0)
  {
    int err = z_nrf_grtc_wakeup_prepare((uint64_t)wake_ms * 1000);

    if (err < 0)
    {
      printk("[E_] powerOff() : grtc wakeup %d\n", err);
      return false;
    }
  }

  sys_poweroff();

  return false;
}


#if CLI_USE(HW_POWER)
void cliPower(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("regulator : %s\n", powerIsDcdc() ? "DC/DC" : "LDO");
    cliPrintf("uptime    : %d ms\n", millis());
    ret = true;
  }

  // power off        : 버튼으로 깨어남
  // power off 5000   : 버튼 또는 5초 뒤 깨어남
  if ((args->argc == 1 || args->argc == 2) && args->isStr(0, "off"))
  {
    uint32_t wake_ms = 0;

    if (args->argc == 2)
    {
      wake_ms = args->getData(1);
    }

    if (wake_ms > 0)
      cliPrintf("System OFF : wake by button or %d ms\n", wake_ms);
    else
      cliPrintf("System OFF : wake by button\n");

    powerOff(wake_ms);
    cliPrintf("powerOff() Fail\n");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("power info\n");
    cliPrintf("power off [wake_ms]\n");
  }
}
#endif

#endif
