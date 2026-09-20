#include "ap.h"




void apInit(void)
{
  moduleInit();

  // 모듈 초기화 로그까지 부팅 로그에 남긴다.
  logBoot(false);
}

void apMain(void)
{
  // 모듈은 각자 스레드에서 돈다 (cli : cli_mgr). main 스레드는 할 일이 없으므로 잠든다.
  //
  while (1)
  {
    k_sleep(K_FOREVER);
  }
}
