/*
 * cli_mgr.c — cli 모듈 (NU87 구조)
 *
 * cli 를 자기 스레드에서 돌린다. cliMain() 은 기다리지 않는 함수 그대로 두고,
 * 입력이 없으면 여기서 uartWaitRx() 로 다음 입력까지 잠든다 (폴링 없음).
 *
 * 이후 BLE(로드맵 16) 채널이 붙으면 NU87 처럼 이 스레드에서 cli 채널을 전환한다.
 */

#include "cli_mgr.h"


#ifdef _USE_HW_CLI


#define CLI_MGR_IDLE_WAIT_MS      1000


static uint8_t  cli_ch    = HW_UART_CH_CLI;
static uint32_t cli_baud  = 115200;
static bool     is_enable = true;

static void cliMgrThread(void *arg1, void *arg2, void *arg3);

static K_THREAD_STACK_DEFINE(cli_stack, _HW_DEF_RTOS_THREAD_MEM_CLI);
static struct k_thread cli_thread;




bool cliMgrInit(void)
{
  bool ret;
  k_tid_t tid;


  ret = cliOpen(cli_ch, cli_baud);

  tid = k_thread_create(&cli_thread, cli_stack, K_THREAD_STACK_SIZEOF(cli_stack),
                        cliMgrThread, NULL, NULL, NULL,
                        _HW_DEF_RTOS_THREAD_PRI_CLI, 0, K_NO_WAIT);
  k_thread_name_set(tid, "cli");

  return ret && tid != NULL;
}

void cliMgrEnable(bool enable)
{
  is_enable = enable;
}

void cliMgrThread(void *arg1, void *arg2, void *arg3)
{
  moduleWaitReady();

  while (1)
  {
    if (is_enable)
    {
      cliMain();
    }

    // 저전력 : 처리할 입력이 없으면 다음 입력이 올 때까지 잠든다.
    if (cliAvailable() == 0)
    {
      uartWaitRx(cli_ch, CLI_MGR_IDLE_WAIT_MS);
    }
  }
}

MODULE_DEF(cli){
  .name     = "cli",
  .priority = MODULE_PRI_LOW,
  .init     = cliMgrInit,
};

#endif
