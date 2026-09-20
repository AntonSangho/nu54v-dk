/*
 * reset.c — 리셋 사유 (NU87 reset 모듈 구조, Zephyr hwinfo 사용)
 *
 * nRF54L15 의 RESETREAS 는 지우기 전까지 누적된다. 부팅할 때 읽고 바로 지워서
 * 다음 부팅에는 그 사이에 생긴 사유만 남게 한다.
 * System OFF 에서 깨어나는 것도 리셋이므로 여기서 깨어난 원인(GPIO / GRTC)을 알 수 있다.
 */

#include "reset.h"


#ifdef _USE_HW_RESET
#include "cli.h"
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/reboot.h>


#if CLI_USE(HW_RESET)
static void cliReset(cli_args_t *args);
#endif


static bool     is_init     = false;
static uint32_t reset_bits  = 0;
static uint32_t reset_cause = 0;      // hwinfo 원본 (진단용)


static const char *reset_bit_str[RESET_BIT_MAX] =
  {
    "RESET_BIT_POWER",
    "RESET_BIT_PIN",
    "RESET_BIT_WDG",
    "RESET_BIT_SOFT",
    "RESET_BIT_ETC",
    "RESET_BIT_WAKE_GPIO",
    "RESET_BIT_WAKE_TIMER",
    "RESET_BIT_DEBUG",
  };




bool resetInit(void)
{
  if (hwinfo_get_reset_cause(&reset_cause) == 0)
  {
    hwinfo_clear_reset_cause();
  }

  if (reset_cause & (RESET_POR | RESET_BROWNOUT))  reset_bits |= (1<<RESET_BIT_POWER);
  if (reset_cause & RESET_PIN)                     reset_bits |= (1<<RESET_BIT_PIN);
  if (reset_cause & RESET_WATCHDOG)                reset_bits |= (1<<RESET_BIT_WDG);
  if (reset_cause & RESET_SOFTWARE)                reset_bits |= (1<<RESET_BIT_SOFT);
  if (reset_cause & RESET_LOW_POWER_WAKE)          reset_bits |= (1<<RESET_BIT_WAKE_GPIO);
  if (reset_cause & RESET_CLOCK)                   reset_bits |= (1<<RESET_BIT_WAKE_TIMER);
  if (reset_cause & RESET_DEBUG)                   reset_bits |= (1<<RESET_BIT_DEBUG);
  if (reset_cause & (RESET_CPU_LOCKUP | RESET_SECURITY))
                                                   reset_bits |= (1<<RESET_BIT_ETC);

  // RESETREAS 가 비어 있으면 전원 인가로 본다.
  if (reset_bits == 0)
  {
    reset_bits |= (1<<RESET_BIT_POWER);
  }

  logPrintf("[OK] resetInit()\n");
  for (int i=0; i<RESET_BIT_MAX; i++)
  {
    if (reset_bits & (1<<i))
    {
      logPrintf("     %s\n", reset_bit_str[i]);
    }
  }

  is_init = true;

#if CLI_USE(HW_RESET)
  cliAdd("reset", cliReset);
#endif

  return is_init;
}

void resetToReset(void)
{
  sys_reboot(SYS_REBOOT_COLD);
}

uint32_t resetGetBits(void)
{
  return reset_bits;
}

uint32_t resetGetCause(void)
{
  return reset_cause;
}


#if CLI_USE(HW_RESET)
void cliReset(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("cause : 0x%08X\n", reset_cause);
    for (int i=0; i<RESET_BIT_MAX; i++)
    {
      if (reset_bits & (1<<i))
      {
        cliPrintf("        %s\n", reset_bit_str[i]);
      }
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "run"))
  {
    cliPrintf("reset...\n");
    delay(10);
    resetToReset();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("reset info\n");
    cliPrintf("reset run\n");
  }
}
#endif

#endif
