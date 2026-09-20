#include "ap.h"


// 살아 있다는 표시 (heartbeat). 펌웨어가 도는지 눈으로 확인한다.
#define AP_LED_PERIOD_MS      500


void apInit(void)
{
  moduleInit();

  // 모듈 초기화 로그까지 부팅 로그에 남긴다.
  logBoot(false);
}

void apMain(void)
{
  uint32_t pre_time = millis();

  // 모듈은 각자 스레드에서 돈다 (cli : cli_mgr).
  // main 스레드는 LED1 만 깜빡여 펌웨어가 살아 있음을 보인다.
  //
  // 저전력 : 폴링하지 않고 다음 토글까지 잠든다 (그 동안 CPU 는 System ON idle).
  //
  while (1)
  {
    ledToggle(_DEF_LED1);

    pre_time += AP_LED_PERIOD_MS;

    uint32_t elapsed = millis() - pre_time;
    if (elapsed < AP_LED_PERIOD_MS)
    {
      delay(AP_LED_PERIOD_MS - elapsed);
    }
    else
    {
      pre_time = millis();          // 많이 밀렸으면 기준을 다시 잡는다
      delay(AP_LED_PERIOD_MS);
    }
  }
}
