#include "ap.h"




void apInit(void)
{
  cliOpen(HW_UART_CH_CLI, 115200);
}

void apMain(void)
{
  while (1)
  {
    cliMain();
    delay(1);
  }
}
