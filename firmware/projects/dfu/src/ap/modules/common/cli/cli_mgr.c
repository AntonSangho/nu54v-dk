/*
 * cli_mgr.c — cli 모듈 (NU87 구조)
 *
 * cli 를 자기 스레드에서 돌린다. cliMain() 은 기다리지 않는 함수 그대로 두고,
 * 입력이 없으면 여기서 uartWaitRx() 로 다음 입력까지 잠든다 (폴링 없음).
 *
 * BLE(NUS)가 붙으면 cli 를 그 채널로 넘긴다. 로컬 UART 에 입력이 들어오면 언제든 되돌아온다
 * (NU87 cli_mgr 과 같은 방식) — 원격에 물려 있어도 콘솔을 잃지 않는다.
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
    // is_enable 이 꺼져 있으면 cliMain() 뿐 아니라 채널 전환도 멈춘다.
    // 명령을 실행하는 중에 채널이 바뀌면 cliKeepLoop() 이 엉뚱한 포트의 입력을 보게 되어
    // 반복 명령(shtc3 read 300, adc show …)이 빠져나오지 못한다. (stm32h5-w6300 cli_mgr 주석)
    if (is_enable != true)
    {
      delay(10);
      continue;
    }

    cliMain();

#ifdef _USE_HW_BLE_NUS
    // 채널 선택 : BLE 가 준비되면(연결 + notify) 그쪽으로, 로컬 입력이 오면 되돌아온다.
    if (bleNusIsReady())
    {
      cli_ch = HW_UART_CH_BLE;
    }
    else if (cli_ch == HW_UART_CH_BLE)
    {
      cli_ch = HW_UART_CH_CLI;
    }

    if (uartAvailable(HW_UART_CH_CLI) > 0)
    {
      cli_ch = HW_UART_CH_CLI;
    }

    if (cliGetPort() != cli_ch)
    {
      cliOpen(cli_ch, cli_baud);
#ifdef _USE_HW_LOG
      logOpen(cli_ch, cli_baud);    // 로그도 cli 채널을 따라간다
#endif
      cliBegin();                   // 바뀐 채널에 프롬프트를 한 번 찍는다 (baram-term 쪽 상태 맞추기)
    }
#endif

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
