#include "bsp.h"
#include <zephyr/sys/printk.h>





bool bspInit(void)
{
  bool ret = true;


  return ret;
}

void delay(uint32_t ms)
{
  if (ms > 0)
  {
    k_msleep(ms);
  }
}

uint32_t millis(void)
{
  return k_uptime_get_32();
}

// log 모듈(log.c)이 추가되면 그쪽 구현이 우선한다.
//
__weak void logPrintf(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  vprintk(fmt, args);
  va_end(args);
}
