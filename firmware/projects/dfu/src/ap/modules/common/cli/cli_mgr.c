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

/* 여러 채널 중 어디에 입력이 와도 깨어나기 위한 세마포어.
 *
 * cli 는 로컬 UART 와 BLE 를 오가며 쓴다. 한 채널만 기다리면 다른 채널로 온
 * 입력을 타임아웃까지 못 본다 (BLE 가 붙어 있으면 시리얼 SMP 왕복이 매번
 * CLI_MGR_IDLE_WAIT_MS 씩 걸렸다). 어느 채널을 묶을지는 여기서 정한다. */
static struct k_sem rx_sem;

static void cliMgrRxNotify(uint8_t ch);

static /* ISR 문맥에서 불린다. 어느 채널이든 알림은 하나로 모은다. */
void cliMgrRxNotify(uint8_t ch)
{
  (void)ch;
  k_sem_give(&rx_sem);
}

/* 지금 봐야 할 채널들에 처리할 입력이 있나 */
static bool cliMgrHasInput(void)
{
  // 필터가 돌려보낸 바이트를 손에 쥔 채 잠들면 안 된다.
  // 그 바이트는 다음 바퀴 cliMain() 이 꺼내므로 큐에는 안 보인다.
  if (cliHasPending() == true) return true;
  if (cliAvailable() > 0) return true;
#ifdef _USE_HW_BLE_NUS
  if (uartAvailable(HW_UART_CH_CLI) > 0) return true;
#endif
  return false;
}

void cliMgrThread(void *arg1, void *arg2, void *arg3);

static K_THREAD_STACK_DEFINE(cli_stack, _HW_DEF_RTOS_THREAD_MEM_CLI);
static struct k_thread cli_thread;




bool cliMgrInit(void)
{
  bool ret;
  k_tid_t tid;


  k_sem_init(&rx_sem, 0, 1);
  if (uartSetRxNotify(cliMgrRxNotify) != true)
  {
    return false;                 // 이미 다른 쪽이 걸었다. 조용히 지면 안 된다
  }

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
    // 로컬 UART 는 cli 가 어디에 있든 계속 비운다.
    //
    // 필터(시리얼 SMP)는 이 포트에 묶여 있다. cli 가 BLE 로 넘어갔다고 이 포트를
    // 놓아 버리면 SMP 가 cli 의 채널 선택에 끌려다닌다. 필터가 가져가지 않은
    // 바이트가 나올 때만 — 즉 사람이 친 입력일 때만 — cli 를 이쪽으로 되돌린다.
    bool is_local_input = false;

    if (cli_ch != HW_UART_CH_CLI && uartAvailable(HW_UART_CH_CLI) > 0)
    {
      is_local_input = (cliFilterPump(HW_UART_CH_CLI) != true);
    }

    // 채널 선택 : BLE 가 준비되면(연결 + notify) 그쪽으로, 로컬 입력이 오면 되돌아온다.
    if (bleNusIsReady())
    {
      cli_ch = HW_UART_CH_BLE;
    }
    else if (cli_ch == HW_UART_CH_BLE)
    {
      cli_ch = HW_UART_CH_CLI;
    }

    if (is_local_input == true || cliAvailable() > 0)
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
    // 어느 채널이든 수신되면 cliMgrRxNotify() 가 깨운다.
    if (cliMgrHasInput() != true)
    {
      k_sem_reset(&rx_sem);
      if (cliMgrHasInput() != true)       // 재우기 직전에 온 것을 놓치지 않는다
      {
        k_sem_take(&rx_sem, K_MSEC(CLI_MGR_IDLE_WAIT_MS));
      }
    }
  }
}

MODULE_DEF(cli){
  .name     = "cli",
  .priority = MODULE_PRI_LOW,
  .init     = cliMgrInit,
};

#endif
