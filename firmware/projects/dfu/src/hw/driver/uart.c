/*
 * uart.c — UART 드라이버 (nRF54L15 UARTE, Zephyr async API)
 *
 * 하드웨어 채널은 VCOM1(uart20) 하나. 온보드 CMSIS-DAP 를 거쳐 USB 로 나간다.
 * 나머지 채널은 가상 채널이다. uart_driver_t 를 등록(uartSetDriver)하면 열린다.
 * (예: BLE NUS 를 HW_UART_CH_BLE 에 붙이면 cli 는 채널 번호만 바꿔 BLE 로 동작)
 * 가상 채널 드라이버는 데이터를 받으면 uartRxNotify(ch) 를 불러 uartWaitRx() 를 깨운다.
 *
 * RX 는 DMA 이중 버퍼로 받아 콜백에서 qbuffer 에 쌓는다. 수신이 멈추면 UARTE 의
 * 하드웨어 frame timeout 으로 곧바로 전달되므로 별도 TIMER 나 수신 스레드가 없다.
 *
 * TX 는 DMA 다. 전송이 끝날 때까지 호출한 스레드는 sleep 한다.
 * fault 처럼 인터럽트를 쓸 수 없는 곳에서는 printk(poll out) 를 쓴다.
 *
 * 저전력 : RX 를 켜 둔 동안에는 UARTE 와 클럭이 동작해 대기 전류가 늘어난다.
 *          uartClose() 로 RX 를 끄면 PM runtime 이 UARTE 를 완전히 끈다 (송신할 때만 켜짐).
 *          수신 대기는 uartWaitRx() 로 폴링 없이 sleep 한다.
 */

#include "uart.h"


#ifdef _USE_HW_UART
#include "qbuffer.h"
#include "cli.h"
#include <zephyr/drivers/uart.h>


// 수신 큐. SMP(시리얼 DFU)는 한 패킷이 여러 줄로 연속해서 들어와 버스트가 크다
// (1218 바이트 패킷 → base64 약 1640 바이트). 1024 면 업로드 중에 넘친다.
#define UART_RX_BUF_LEN       4096
#define UART_RX_DMA_LEN       64
#define UART_RX_TIMEOUT_US    1000
#define UART_TX_TIMEOUT_MS    1000


typedef struct
{
  uint8_t              ch;
  const struct device *h_dev;

  qbuffer_t      rx_q;
  uint8_t        rx_buf[UART_RX_BUF_LEN];
  uint8_t        rx_dma_buf[2][UART_RX_DMA_LEN];
  uint8_t        rx_dma_index;

  struct k_sem   rx_off_sem;
  struct k_sem   tx_sem;
  struct k_mutex tx_mutex;

  // 조용히 잃는 자리를 드러내기 위한 계수기 (SMP 업로드에서 한 번씩 응답이 빈다)
  uint32_t       rx_drop_cnt;     // 수신 큐가 꽉 차 버린 바이트
  uint32_t       rx_stop_cnt;     // UARTE 가 수신을 멈췄다 (오버런·프레이밍)
  uint32_t       rx_stop_reason;  // 마지막 멈춤 이유
} uart_hw_t;

typedef struct
{
  bool     is_open;
  uint32_t baud;
  uint32_t rx_cnt;
  uint32_t tx_cnt;

  uart_driver_t *p_driver;
  uart_hw_t     *p_hw;

  struct k_sem   rx_sem;          // 수신 알림 (하드웨어 콜백 / 드라이버의 uartRxNotify)
} uart_tbl_t;

/* 수신 알림을 한 곳에서 더 받고 싶은 쪽이 거는 훅 (ISR 문맥에서 불린다).
 *
 * 여러 채널을 함께 기다려야 하는 쪽이 있다 (cli 는 로컬 UART 와 BLE 를 오간다).
 * 어느 채널을 묶을지는 uart 가 정할 일이 아니므로 알림만 넘기고 정책은 맡긴다.
 */
static uart_rx_notify_t rx_notify_cb = NULL;


static const char *uart_name[UART_MAX_CH] =
{
  "VCOM1    uart20",
#ifdef HW_UART_CH_VCOM0
  "VCOM0    uart30",
#endif
};

// 하드웨어 채널. 여기에 없는 채널은 uartSetDriver() 로 드라이버를 붙여야 열린다.
//
/* cli/log 포트는 반드시 async(DMA) API 여야 한다.
 * UART_INTERRUPT_DRIVEN 을 select 하는 옵션(예: UART_MCUMGR)이 켜지면 인스턴스 기본값이
 * 인터럽트 방식으로 뒤집혀 uart_rx_enable() 이 NULL 이 된다 → 부팅 직후 USAGE FAULT. */
BUILD_ASSERT(IS_ENABLED(CONFIG_UART_20_ASYNC),
             "uart20 은 async API 로 써야 한다. CONFIG_UART_20_INTERRUPT_DRIVEN=n 을 넣어라");

static uart_hw_t uart_hw[] =
{
  { .ch = HW_UART_CH_LOG,   .h_dev = DEVICE_DT_GET(DT_NODELABEL(uart20)) },
#ifdef HW_UART_CH_VCOM0
  { .ch = HW_UART_CH_VCOM0, .h_dev = DEVICE_DT_GET(DT_NODELABEL(uart30)) },
#endif
};

static uart_tbl_t uart_tbl[UART_MAX_CH];
static bool       is_init = false;


static void uartEventCallback(const struct device *dev, struct uart_event *evt, void *user_data);
static bool uartRxStart(uart_hw_t *p_hw);

#if CLI_USE(HW_UART)
static void cliUart(cli_args_t *args);
#endif




bool uartInit(void)
{
  for (int i = 0; i < UART_MAX_CH; i++)
  {
    uart_tbl[i].is_open  = false;
    uart_tbl[i].baud     = 115200;
    uart_tbl[i].rx_cnt   = 0;
    uart_tbl[i].tx_cnt   = 0;
    uart_tbl[i].p_driver = NULL;
    uart_tbl[i].p_hw     = NULL;
    k_sem_init(&uart_tbl[i].rx_sem, 0, 1);
  }

  for (int i = 0; i < ARRAY_SIZE(uart_hw); i++)
  {
    uart_tbl[uart_hw[i].ch].p_hw = &uart_hw[i];
    qbufferCreate(&uart_hw[i].rx_q, uart_hw[i].rx_buf, UART_RX_BUF_LEN);
    k_sem_init(&uart_hw[i].rx_off_sem, 0, 1);
    k_sem_init(&uart_hw[i].tx_sem, 0, 1);
    k_mutex_init(&uart_hw[i].tx_mutex);
    uart_callback_set(uart_hw[i].h_dev, uartEventCallback, &uart_hw[i]);
  }

#if CLI_USE(HW_UART)
  cliAdd("uart", cliUart);
#endif

  is_init = true;
  return true;
}

bool uartDeInit(void)
{
  is_init = false;
  return true;
}

bool uartIsInit(void)
{
  return is_init;
}

bool uartSetDriver(uint8_t ch, uart_driver_t *p_driver)
{
  if (ch >= UART_MAX_CH) return false;

  uart_tbl[ch].p_driver = p_driver;
  return true;
}

bool uartOpen(uint8_t ch, uint32_t baud)
{
  uart_hw_t *p_hw;
  struct uart_config cfg;


  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].is_open == true && uart_tbl[ch].baud == baud)
  {
    return true;
  }

  uart_tbl[ch].baud = baud;

  if (uart_tbl[ch].p_driver != NULL)
  {
    uart_tbl[ch].is_open = uart_tbl[ch].p_driver->open(baud);
    return uart_tbl[ch].is_open;
  }

  p_hw = uart_tbl[ch].p_hw;
  if (p_hw == NULL) return false;

  if (!device_is_ready(p_hw->h_dev)) return false;

  // 보레이트 변경 : RX 정지는 비동기이므로 RX_DISABLED 이벤트까지 기다린 뒤 설정한다.
  if (uart_tbl[ch].is_open)
  {
    uart_tbl[ch].is_open = false;
    k_sem_reset(&p_hw->rx_off_sem);
    if (uart_rx_disable(p_hw->h_dev) == 0)
    {
      k_sem_take(&p_hw->rx_off_sem, K_MSEC(100));
    }
  }

  if (uart_config_get(p_hw->h_dev, &cfg) == 0 && cfg.baudrate != baud)
  {
    cfg.baudrate = baud;
    uart_configure(p_hw->h_dev, &cfg);
  }

  qbufferFlush(&p_hw->rx_q);

  uart_tbl[ch].is_open = true;
  uart_tbl[ch].is_open = uartRxStart(p_hw);

  return uart_tbl[ch].is_open;
}

// RX 를 끈다 → UARTE 가 suspend 된다 (송신은 보낼 때만 켜져서 계속 가능).
//
bool uartClose(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  uart_tbl[ch].is_open = false;

  if (uart_tbl[ch].p_driver != NULL)
  {
    uart_tbl[ch].p_driver->close();
  }
  else if (uart_tbl[ch].p_hw != NULL)
  {
    uart_rx_disable(uart_tbl[ch].p_hw->h_dev);
  }

  return true;
}

bool uartIsOpen(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  return uart_tbl[ch].is_open;
}

uint32_t uartAvailable(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->available();
  }
  if (uart_tbl[ch].p_hw == NULL) return 0;

  return qbufferAvailable(&uart_tbl[ch].p_hw->rx_q);
}

// 수신 데이터가 있거나 timeout_ms 가 지날 때까지 sleep 한다 (폴링 대신 사용).
//
bool uartWaitRx(uint8_t ch, uint32_t timeout_ms)
{
  if (ch >= UART_MAX_CH) return false;

  if (uartAvailable(ch) > 0) return true;

  k_sem_reset(&uart_tbl[ch].rx_sem);
  if (uartAvailable(ch) == 0)
  {
    k_sem_take(&uart_tbl[ch].rx_sem, K_MSEC(timeout_ms));
  }

  return uartAvailable(ch) > 0;
}

// 수신 알림. 하드웨어 채널은 uart.c 가 부르고, 가상 채널 드라이버는 데이터를 받으면 부른다.
// (ISR 에서 불러도 된다)
//
void uartRxNotify(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return;

  k_sem_give(&uart_tbl[ch].rx_sem);

  if (rx_notify_cb != NULL)
  {
    rx_notify_cb(ch);
  }
}

void uartSetRxNotify(uart_rx_notify_t cb)
{
  rx_notify_cb = cb;
}

bool uartFlush(uint8_t ch)
{
  uint32_t pre_time;


  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->flush();
  }
  if (uart_tbl[ch].p_hw == NULL) return false;

  pre_time = millis();
  while (uartAvailable(ch) > 0 && millis() - pre_time < UART_FLUSH_TIMEOUT_MS)
  {
    qbufferFlush(&uart_tbl[ch].p_hw->rx_q);
    delay(1);
  }
  return true;
}

uint8_t uartRead(uint8_t ch)
{
  uint8_t data = 0;


  if (ch >= UART_MAX_CH) return 0;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->read();
  }
  if (uart_tbl[ch].p_hw == NULL) return 0;

  qbufferRead(&uart_tbl[ch].p_hw->rx_q, &data, 1);
  uart_tbl[ch].rx_cnt++;
  return data;
}

uint32_t uartWrite(uint8_t ch, uint8_t *p_data, uint32_t length)
{
  uint32_t   ret = 0;
  uart_hw_t *p_hw;


  if (ch >= UART_MAX_CH) return 0;
  if (p_data == NULL || length == 0) return 0;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->write(p_data, length);
  }
  p_hw = uart_tbl[ch].p_hw;
  if (p_hw == NULL) return 0;

  // p_data 는 호출자 버퍼이므로 DMA 전송이 끝날 때까지 기다린다. 기다리는 동안 sleep.
  //
  k_mutex_lock(&p_hw->tx_mutex, K_FOREVER);
  k_sem_reset(&p_hw->tx_sem);
  if (uart_tx(p_hw->h_dev, p_data, length, SYS_FOREVER_US) == 0)
  {
    if (k_sem_take(&p_hw->tx_sem, K_MSEC(UART_TX_TIMEOUT_MS)) == 0)
    {
      ret = length;
    }
    else
    {
      uart_tx_abort(p_hw->h_dev);
    }
  }
  k_mutex_unlock(&p_hw->tx_mutex);

  uart_tbl[ch].tx_cnt += ret;
  return ret;
}

uint32_t uartPrintf(uint8_t ch, const char *fmt, ...)
{
  char buf[256];
  va_list args;
  int len;

  va_start(args, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (len <= 0) return 0;
  if (len > (int)sizeof(buf)) len = sizeof(buf);

  return uartWrite(ch, (uint8_t *)buf, (uint32_t)len);
}

uint32_t uartGetBaud(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;
  return uart_tbl[ch].baud;
}

uint32_t uartGetRxCnt(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;
  return uart_tbl[ch].rx_cnt;
}

uint32_t uartGetTxCnt(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;
  return uart_tbl[ch].tx_cnt;
}

bool uartRxStart(uart_hw_t *p_hw)
{
  p_hw->rx_dma_index = 0;

  return uart_rx_enable(p_hw->h_dev, p_hw->rx_dma_buf[0], UART_RX_DMA_LEN, UART_RX_TIMEOUT_US) == 0;
}

void uartEventCallback(const struct device *dev, struct uart_event *evt, void *user_data)
{
  uart_hw_t *p_hw = (uart_hw_t *)user_data;


  switch (evt->type)
  {
    case UART_TX_DONE:
    case UART_TX_ABORTED:
      k_sem_give(&p_hw->tx_sem);
      break;

    case UART_RX_RDY:
      // 큐가 꽉 차면 qbufferWrite() 는 false 를 돌려주고 그 바이트는 사라진다.
      // 그대로 두면 SMP 프레임 한 줄이 깨져 응답이 통째로 없어진다 (원인이 안 보인다).
      if (qbufferWrite(&p_hw->rx_q, &evt->data.rx.buf[evt->data.rx.offset],
                       evt->data.rx.len) != true)
      {
        p_hw->rx_drop_cnt += evt->data.rx.len;
      }
      uartRxNotify(p_hw->ch);
      break;

    case UART_RX_BUF_REQUEST:
      p_hw->rx_dma_index ^= 1;
      uart_rx_buf_rsp(dev, p_hw->rx_dma_buf[p_hw->rx_dma_index], UART_RX_DMA_LEN);
      break;

    case UART_RX_STOPPED:
      // 오버런·프레이밍·패리티 오류. 여기까지 오면 이미 데이터를 잃었다.
      // 곧 UART_RX_DISABLED 가 따라오고 아래에서 수신을 다시 켠다.
      p_hw->rx_stop_cnt++;
      p_hw->rx_stop_reason = evt->data.rx_stop.reason;
      break;

    case UART_RX_DISABLED:
      k_sem_give(&p_hw->rx_off_sem);
      // 에러 등으로 수신이 멈춘 경우 다시 시작한다. uartClose() 로 끈 경우는 제외.
      for (int i = 0; i < UART_MAX_CH; i++)
      {
        if (uart_tbl[i].p_hw == p_hw && uart_tbl[i].is_open && uart_tbl[i].p_driver == NULL)
        {
          uartRxStart(p_hw);
          break;
        }
      }
      break;

    default:
      break;
  }
}


#if CLI_USE(HW_UART)
void cliUart(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    for (int i = 0; i < UART_MAX_CH; i++)
    {
      cliPrintf("_DEF_UART%d : %s, %d bps%s%s\n",
                i + 1,
                uart_name[i],
                (int)uartGetBaud(i),
                uartIsOpen(i) ? "" : "  (closed)",
                (i == cliGetPort()) ? "  <- cli" : "");
      cliPrintf("             rx %d, tx %d bytes", (int)uartGetRxCnt(i), (int)uartGetTxCnt(i));
      if (uart_tbl[i].p_hw != NULL)
      {
        cliPrintf(", rx queue %d/%d", (int)qbufferAvailable(&uart_tbl[i].p_hw->rx_q), UART_RX_BUF_LEN);
        cliPrintf("\n             rx drop %d, rx stop %d (reason 0x%X)",
                  (int)uart_tbl[i].p_hw->rx_drop_cnt,
                  (int)uart_tbl[i].p_hw->rx_stop_cnt,
                  (unsigned)uart_tbl[i].p_hw->rx_stop_reason);
      }
      cliPrintf("\n");
    }
    ret = true;
  }

  /* 수신 바이트를 16진으로 계속 찍는다. q 로 빠져나온다. */
  if (args->argc == 2 && args->isStr(0, "test"))
  {
    uint8_t ch = constrain(args->getData(1), 1, UART_MAX_CH) - 1;

    if (ch == cliGetPort())
    {
      cliPrintf("cli 가 쓰는 포트다\n");
    }
    else
    {
      // 받은 바이트를 그대로 돌려보내 TX 도 함께 확인한다.
      while (cliKeepLoop())
      {
        if (uartWaitRx(ch, 10))
        {
          uint8_t data = uartRead(ch);

          uartWrite(ch, &data, 1);
          cliPrintf("<- _DEF_UART%d : 0x%02X\n", ch + 1, data);
        }
      }
      cliRead();
    }
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "open"))
  {
    uint8_t  ch   = constrain(args->getData(1), 1, UART_MAX_CH) - 1;
    uint32_t baud = args->getData(2);

    cliPrintf("_DEF_UART%d open %d : %s\n", ch + 1, (int)baud, uartOpen(ch, baud) ? "OK" : "Fail");
    ret = true;
  }

  // RX 를 끄면 UARTE 가 suspend 된다 (저전력 확인용). cli 포트는 닫지 않는다.
  if (args->argc == 2 && args->isStr(0, "close"))
  {
    uint8_t ch = constrain(args->getData(1), 1, UART_MAX_CH) - 1;

    if (ch == cliGetPort())
    {
      cliPrintf("cli 가 쓰는 포트다\n");
    }
    else
    {
      uartClose(ch);
      cliPrintf("_DEF_UART%d close\n", ch + 1);
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("uart info\n");
    cliPrintf("uart test  ch[1~%d]\n", UART_MAX_CH);
    cliPrintf("uart open  ch[1~%d] baud\n", UART_MAX_CH);
    cliPrintf("uart close ch[1~%d]\n", UART_MAX_CH);
  }
}
#endif

#endif
