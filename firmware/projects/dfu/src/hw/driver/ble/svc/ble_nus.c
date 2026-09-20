/*
 * ble_nus.c — NUS(Nordic UART Service) 서버를 uart 가상 채널로 (_USE_HW_BLE_NUS)
 *
 * uart 모듈의 uart_driver_t 를 구현해 HW_UART_CH_BLE 에 등록한다.
 * 그러면 cli·log 는 채널 번호만 바꿔서 그대로 BLE 위에서 돈다 (NU87 의 cli_ble 과 같은 방식).
 *
 *   호스트 → RX 특성 write → received 콜백 → qbuffer → uartRead()
 *   uartWrite() → tx_buf 에 모음 → bt_nus_send() → TX 특성 notify → 호스트
 *
 * 송신은 MTU 만큼 모아서 보낸다. cliPrintf() 는 한 줄씩 부르는데 그대로 내보내면
 * 20 바이트짜리 notify 가 줄 수만큼 나가고, TX 버퍼(BT_BUF_ACL_TX_COUNT)가 금방 말라
 * 출력이 잘린다. tx_buf 에 모아 MTU 가 차면 보내고, 남은 조각은 워크로 조금 뒤에 내보낸다.
 *
 * 저전력 : 받은 데이터는 콜백에서 qbuffer 에 넣고 uartRxNotify() 로 기다리던 스레드를 깨운다 (폴링 없음).
 *          송신 버퍼가 없을 때도 돌지 않고 sleep 하며 기다린다.
 */

#include "ble_nus.h"


#ifdef _USE_HW_BLE_NUS
#include "ble.h"
#include "uart.h"
#include "qbuffer.h"
#include "cli.h"
#include "bsp.h"
#include <bluetooth/services/nus.h>

#if !defined(CONFIG_BT_NUS)
#error "_USE_HW_BLE_NUS 를 켰으면 conf/ble_nus.conf 를 붙여야 한다"
#endif


#define BLE_NUS_RX_BUF_LEN    512
#define BLE_NUS_TX_BUF_LEN    512     // MTU(244) 보다 크게. 한 번에 MTU 까지만 보낸다.
#define BLE_NUS_TX_FLUSH_MS   2       // 남은 조각을 내보내기까지 기다리는 시간
#define BLE_NUS_TX_RETRY_MS   2       // 송신 버퍼가 빌 때까지 쉬는 간격
#define BLE_NUS_TX_TIMEOUT_MS 500     // 이 시간이 지나면 포기하고 버린다


static bool     bleNusInit(void);
static void     bleNusConnected(struct bt_conn *p_conn);
static void     bleNusDisconnected(struct bt_conn *p_conn);
static void     bleNusInfo(void);

static bool     bleNusDrvOpen(uint32_t baud);
static bool     bleNusDrvClose(void);
static uint32_t bleNusDrvAvailable(void);
static bool     bleNusDrvFlush(void);
static uint8_t  bleNusDrvRead(void);
static uint32_t bleNusDrvWrite(uint8_t *p_data, uint32_t length);

static void bleNusReceived(struct bt_conn *p_conn, const uint8_t *const p_data, uint16_t length);
static void bleNusSendEnabled(enum bt_nus_send_status status);

static bool bleNusTxSend(struct bt_conn *p_conn);
static void bleNusTxFlushWork(struct k_work *p_work);


static uart_driver_t nus_drv;
static qbuffer_t     rx_q;
static uint8_t       rx_buf[BLE_NUS_RX_BUF_LEN];

static uint8_t       tx_buf[BLE_NUS_TX_BUF_LEN];
static uint32_t      tx_len = 0;
static K_MUTEX_DEFINE(tx_mutex);
static K_WORK_DELAYABLE_DEFINE(tx_flush_work, bleNusTxFlushWork);

static bool     is_notify_enabled = false;
static uint32_t rx_cnt = 0;
static uint32_t tx_cnt = 0;
static uint32_t tx_pkt_cnt = 0;
static uint32_t tx_drop_cnt = 0;

static struct bt_nus_cb nus_cb =
{
  .received     = bleNusReceived,
  .send_enabled = bleNusSendEnabled,
};




bool bleNusInit(void)
{
  if (bt_nus_init(&nus_cb) != 0)
  {
    return false;
  }

  qbufferCreate(&rx_q, rx_buf, BLE_NUS_RX_BUF_LEN);

  nus_drv.open      = bleNusDrvOpen;
  nus_drv.close     = bleNusDrvClose;
  nus_drv.available = bleNusDrvAvailable;
  nus_drv.flush     = bleNusDrvFlush;
  nus_drv.read      = bleNusDrvRead;
  nus_drv.write     = bleNusDrvWrite;

  // uart 가상 채널로 등록 → cli/log 가 그대로 쓸 수 있다.
  return uartSetDriver(HW_UART_CH_BLE, &nus_drv);
}

void bleNusConnected(struct bt_conn *p_conn)
{
  qbufferFlush(&rx_q);

  k_mutex_lock(&tx_mutex, K_FOREVER);
  tx_len = 0;
  k_mutex_unlock(&tx_mutex);
}

void bleNusDisconnected(struct bt_conn *p_conn)
{
  is_notify_enabled = false;

  k_work_cancel_delayable(&tx_flush_work);

  k_mutex_lock(&tx_mutex, K_FOREVER);
  tx_len = 0;                         // 보낼 곳이 없어졌다
  k_mutex_unlock(&tx_mutex);
}

// 호스트가 notify 를 켰을 때만 출력이 나간다.
//
bool bleNusIsReady(void)
{
  return bleIsConnected() && is_notify_enabled;
}

uint32_t bleNusGetRxCnt(void)
{
  return rx_cnt;
}

uint32_t bleNusGetTxCnt(void)
{
  return tx_cnt;
}

void bleNusReceived(struct bt_conn *p_conn, const uint8_t *const p_data, uint16_t length)
{
  qbufferWrite(&rx_q, (uint8_t *)p_data, length);
  rx_cnt += length;

  uartRxNotify(HW_UART_CH_BLE);      // 기다리던 cli 스레드를 깨운다
}

void bleNusSendEnabled(enum bt_nus_send_status status)
{
  is_notify_enabled = (status == BT_NUS_SEND_STATUS_ENABLED);
}


//-- uart_driver_t
//
bool bleNusDrvOpen(uint32_t baud)
{
  (void)baud;         // BLE 에는 보레이트가 없다. API 모양을 맞추기 위한 인자.

  qbufferFlush(&rx_q);
  return true;
}

bool bleNusDrvClose(void)
{
  return true;
}

uint32_t bleNusDrvAvailable(void)
{
  return qbufferAvailable(&rx_q);
}

bool bleNusDrvFlush(void)
{
  qbufferFlush(&rx_q);
  return true;
}

uint8_t bleNusDrvRead(void)
{
  uint8_t data = 0;

  qbufferRead(&rx_q, &data, 1);
  return data;
}

// tx_buf 에 모아서 MTU 가 차면 보낸다. tx_mutex 를 잡은 채로 부른다.
// 송신 버퍼가 없으면(-ENOMEM) 컨트롤러가 비울 때까지 쉬었다가 다시 보낸다.
//
bool bleNusTxSend(struct bt_conn *p_conn)
{
  uint32_t pre_time = millis();
  bool     ret = false;


  while (tx_len > 0)
  {
    int err = bt_nus_send(p_conn, tx_buf, tx_len);

    if (err == 0)
    {
      tx_cnt += tx_len;
      tx_pkt_cnt++;
      ret = true;
      break;
    }
    if (err != -ENOMEM)               // 연결이 끊겼거나 구독이 꺼졌다. 기다려도 안 된다.
    {
      break;
    }
    if ((millis() - pre_time) >= BLE_NUS_TX_TIMEOUT_MS)
    {
      break;
    }
    k_msleep(BLE_NUS_TX_RETRY_MS);    // 폴링이 아니라 sleep
  }

  if (ret != true)
  {
    tx_drop_cnt += tx_len;
  }
  tx_len = 0;

  return ret;
}

// 모아 둔 나머지를 내보낸다. cli 가 프롬프트만 찍고 입력을 기다리는 경우가 여기에 해당한다.
//
void bleNusTxFlushWork(struct k_work *p_work)
{
  struct bt_conn *p_conn = bleGetConn();

  k_mutex_lock(&tx_mutex, K_FOREVER);
  if (tx_len > 0)
  {
    if (bleNusIsReady() == true && p_conn != NULL)
    {
      bleNusTxSend(p_conn);
    }
    else
    {
      tx_len = 0;
    }
  }
  k_mutex_unlock(&tx_mutex);
}

// notify 가 꺼져 있으면(=듣는 사람이 없으면) 버린다.
// 한 줄씩 들어와도 MTU 까지 모아서 보낸다. 남은 조각은 BLE_NUS_TX_FLUSH_MS 뒤에 워크가 보낸다.
//
uint32_t bleNusDrvWrite(uint8_t *p_data, uint32_t length)
{
  struct bt_conn *p_conn = bleGetConn();
  uint32_t sent = 0;
  uint32_t mtu;


  if (bleNusIsReady() != true || p_conn == NULL)
  {
    return 0;
  }
  if (k_is_in_isr())                  // BLE 송신은 스레드에서만 한다 (ISR 로그는 콘솔로 나간다)
  {
    return 0;
  }

  mtu = bt_nus_get_mtu(p_conn);
  if (mtu == 0)
  {
    return 0;
  }
  mtu = MIN(mtu, BLE_NUS_TX_BUF_LEN);

  k_mutex_lock(&tx_mutex, K_FOREVER);

  while (sent < length)
  {
    uint32_t len = MIN(mtu - tx_len, length - sent);

    memcpy(&tx_buf[tx_len], &p_data[sent], len);
    tx_len += len;
    sent   += len;

    if (tx_len >= mtu)                // 다 찼으면 바로 보낸다
    {
      if (bleNusTxSend(p_conn) != true)
      {
        break;
      }
    }
  }

  if (tx_len > 0)
  {
    k_work_reschedule(&tx_flush_work, K_MSEC(BLE_NUS_TX_FLUSH_MS));
  }

  k_mutex_unlock(&tx_mutex);

  return sent;
}


void bleNusInfo(void)
{
  struct bt_conn *p_conn = bleGetConn();

  cliPrintf("    mtu   : %d\n", p_conn != NULL ? bt_nus_get_mtu(p_conn) : 0);
  cliPrintf("    rx    : %d bytes\n", rx_cnt);
  cliPrintf("    tx    : %d bytes, %d pkt, %d drop\n", tx_cnt, tx_pkt_cnt, tx_drop_cnt);
}


BLE_SVC_DEF(nus)
{
  .name         = "nus",
  .init         = bleNusInit,
  .connected    = bleNusConnected,
  .disconnected = bleNusDisconnected,
  .info         = bleNusInfo,
};

#endif
