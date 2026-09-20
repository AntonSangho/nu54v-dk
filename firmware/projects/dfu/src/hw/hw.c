#include "hw.h"



#if CLI_USE(HW_INFO)
static void cliInfo(cli_args_t *args);
#endif


bool hwInit(void)
{
  bspInit();

  cliInit();
  logInit();
  ledInit();
  uartInit();
  for (int i=0; i<HW_UART_MAX_CH; i++)
  {
    uartOpen(i, 115200);
  }

  logOpen(HW_LOG_CH, 115200);
  logPrintf("\r\n[ Firmware Begin... ]\r\n");
  logPrintf("Booting..Name \t\t: %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Booting..Ver  \t\t: %s\r\n", _DEF_FIRMWATRE_VERSION);
  logPrintf("Booting..Date \t\t: %s\r\n", __DATE__);
  logPrintf("Booting..Time \t\t: %s\r\n", __TIME__);
  logPrintf("Board         \t\t: %s\r\n", CONFIG_BOARD_TARGET);
  logPrintf("\n");

  resetInit();
  powerInit();
  buttonInit();
  i2cInit();
  shtc3Init();
  nvsInit();
  rtcInit();
  bleInit();
  dfuInit();
  pmicInit();
  adcInit();
  tempInit();

#if CLI_USE(HW_INFO)
  cliAdd("info", cliInfo);
#endif

  return true;
}


#if CLI_USE(HW_INFO)
// 펌웨어 정보. 자동 시험에서 버전을 확인할 때 쓴다 (baram-term 쪽 요청).
//
void cliInfo(cli_args_t *args)
{
  cliPrintf("name    : %s\n", _DEF_BOARD_NAME);
  cliPrintf("version : %s\n", _DEF_FIRMWATRE_VERSION);
  cliPrintf("build   : %s %s\n", __DATE__, __TIME__);
  cliPrintf("board   : %s\n", CONFIG_BOARD_TARGET);
  cliPrintf("uptime  : %d ms\n", millis());

#ifdef _USE_HW_RESET
  cliPrintf("reset   : 0x%02X\n", resetGetBits());
#endif
#ifdef _USE_HW_RTC
  {
    rtc_info_t info;

    if (rtcGetInfo(&info) == true)
    {
      cliPrintf("time    : 20%02d-%02d-%02d %02d:%02d:%02d\n",
                info.date.year, info.date.month, info.date.day,
                info.time.hours, info.time.minutes, info.time.seconds);
    }
  }
#endif
#ifdef _USE_HW_BLE
  cliPrintf("ble     : %s, %s\n", bleGetDeviceName(), bleIsConnected() ? "connected" : "advertising");
#endif
}
#endif
