/*
 * ble_nus.c — NUS(Nordic UART Service) 서버를 uart 가상 채널로 (_USE_HW_BLE_NUS)
 *
 * uart 모듈의 uart_driver_t 를 구현해 HW_UART_CH_BLE 에 등록한다.
 * 그러면 cli·log 는 채널 번호만 바꿔서 그대로 BLE 위에서 돈다 (NU87 의 cli_ble 과 같은 방식).
 *
 *   호스트 → RX 특성 write → received 콜백 → qbuffer → uartRead()
 *   uartWrite() → bt_nus_send() → TX 특성 notify → 호스트
 *
 * 저전력 : 받은 데이터는 콜백에서 qbuffer 에 넣고 uartRxNotify() 로 기다리던 스레드를 깨운다 (폴링 없음).
 */

#include "ble_nus.h"


#ifdef _USE_HW_BLE_NUS
#include "ble.h"
#include "uart.h"
#include "qbuffer.h"
#include <bluetooth/services/nus.h>

#if !defined(CONFIG_BT_NUS)
#error "_USE_HW_BLE_NUS 를 켰으면 conf/ble_nus.conf 를 붙여야 한다"
#endif


#define BLE_NUS_RX_BUF_LEN    512


static bool     bleNusInit(void);
static void     bleNusConnected(struct bt_conn *p_conn);
static void     bleNusDisconnected(struct bt_conn *p_conn);

static bool     bleNusDrvOpen(uint32_t baud);
static bool     bleNusDrvClose(void);
static uint32_t bleNusDrvAvailable(void);
static bool     bleNusDrvFlush(void);
static uint8_t  bleNusDrvRead(void);
static uint32_t bleNusDrvWrite(uint8_t *p_data, uint32_t length);

static void bleNusReceived(struct bt_conn *p_conn, const uint8_t *const p_data, uint16_t length);
static void bleNusSendEnabled(enum bt_nus_send_status status);


static uart_driver_t nus_drv;
static qbuffer_t     rx_q;
static uint8_t       rx_buf[BLE_NUS_RX_BUF_LEN];

static bool     is_notify_enabled = false;
static uint32_t rx_cnt = 0;
static uint32_t tx_cnt = 0;

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
}

void bleNusDisconnected(struct bt_conn *p_conn)
{
  is_notify_enabled = false;
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

// notify 가 꺼져 있으면(=듣는 사람이 없으면) 버린다.
// MTU 를 넘는 길이는 나눠 보낸다.
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

  mtu = bt_nus_get_mtu(p_conn);
  if (mtu == 0)
  {
    return 0;
  }

  while (sent < length)
  {
    uint32_t len = MIN(mtu, length - sent);

    if (bt_nus_send(p_conn, &p_data[sent], len) != 0)
    {
      break;
    }
    sent += len;
  }

  tx_cnt += sent;
  return sent;
}


BLE_SVC_DEF(nus)
{
  .name         = "nus",
  .init         = bleNusInit,
  .connected    = bleNusConnected,
  .disconnected = bleNusDisconnected,
};

#endif
