/*
 * temp.c — 칩 내부 온도 (nRF54L15 TEMP, Zephyr sensor API)
 *
 * 저전력 : 읽을 때만 측정한다 (드라이버가 측정 후 TEMP 를 끈다).
 *          주변 온도가 아니라 다이 온도이므로 동작 중에는 실제보다 높게 나온다.
 */

#include "temp.h"


#ifdef _USE_HW_TEMP
#include "cli.h"
#include <zephyr/drivers/sensor.h>


#if CLI_USE(HW_TEMP)
static void cliTemp(cli_args_t *args);
#endif

static bool is_init = false;
static const struct device *h_temp = DEVICE_DT_GET(DT_NODELABEL(temp));




bool tempInit(void)
{
  is_init = device_is_ready(h_temp);

  logPrintf("[%s] tempInit()\n", is_init ? "OK":"E_");

#if CLI_USE(HW_TEMP)
  cliAdd("temp", cliTemp);
#endif

  return is_init;
}

bool tempIsInit(void)
{
  return is_init;
}

bool tempRead(int16_t *p_temp)
{
  struct sensor_value value;


  if (is_init != true) return false;

  if (sensor_sample_fetch(h_temp) < 0) return false;
  if (sensor_channel_get(h_temp, SENSOR_CHAN_DIE_TEMP, &value) < 0) return false;

  // val1 : ℃, val2 : 100만분의 1 ℃
  *p_temp = (int16_t)(value.val1 * 100 + value.val2 / 10000);

  return true;
}


#if CLI_USE(HW_TEMP)
static void cliTempPrint(void)
{
  int16_t temp;

  if (tempRead(&temp) == true)
    cliPrintf("temp : %s%d.%02d C\n", temp < 0 ? "-" : "", abs(temp) / 100, abs(temp) % 100);
  else
    cliPrintf("tempRead() Fail\n");
}

void cliTemp(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("is_init : %s\n", is_init ? "True":"False");
    cliTempPrint();
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "show"))
  {
    while(cliKeepLoop())
    {
      cliTempPrint();
      delay(500);
    }
    cliRead();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("temp info\n");
    cliPrintf("temp show\n");
  }
}
#endif

#endif
