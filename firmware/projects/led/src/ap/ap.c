#include "ap.h"


#define LED_PERIOD_MS     250




void apInit(void)
{
}

void apMain(void)
{
  uint32_t pre_time;
  uint8_t  led_ch = 0;


  pre_time = millis();
  while (1)
  {
    if (millis() - pre_time >= LED_PERIOD_MS)
    {
      pre_time += LED_PERIOD_MS;

      // LED1 : 500ms 주기 점멸 (heartbeat)
      //
      ledToggle(_DEF_LED1);

      // LED2~4 : 순차 점등 (배선 확인용)
      //
      ledOff(_DEF_LED2 + led_ch);
      led_ch = (led_ch + 1) % (LED_MAX_CH - 1);
      ledOn(_DEF_LED2 + led_ch);
    }

    // 저전력 : 1ms 폴링 대신 다음 이벤트까지 sleep (그 동안 CPU 는 System ON idle)
    //
    uint32_t elapsed = millis() - pre_time;
    if (elapsed < LED_PERIOD_MS)
    {
      delay(LED_PERIOD_MS - elapsed);
    }
  }
}
