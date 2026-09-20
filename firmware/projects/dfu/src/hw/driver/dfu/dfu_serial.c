/*
 * dfu_serial.c — SMP(시리얼 DFU)를 cli 가 쓰는 포트 위에 얹는다 (_USE_HW_DFU_SERIAL)
 *
 * Zephyr 의 MCUMGR_TRANSPORT_UART 는 전용 포트(zephyr,uart-mcumgr)를 잡고
 * UART_INTERRUPT_DRIVEN 을 select 한다. 그러면
 *   - VCOM0 를 따로 써야 하고 (이 보드의 내장 프로브가 VCOM0 를 쓰면 SWD 가 죽는다.
 *     docs/reports/2026-09-20_daplink_vcom0_swd.md)
 *   - cli 가 쓰는 uart20 까지 인터럽트 방식으로 넘어간다 (docs/18_dfu.md §4 함정 6)
 *
 * 그래서 전송 계층만 직접 만든다. BLE 에서 NUS 와 SMP 가 한 연결 위에 공존하는 것과 같은 방식이다.
 *
 *   uart(VCOM1) ─→ cliMain() ─→ dfuSerialRxByte()
 *                                  │ 0x06 0x09 (또는 0x04 0x14) 로 시작하면 이쪽이 가져간다
 *                                  │ 아니면 false 를 돌려 cli 가 처리한다
 *                                  ↓
 *                        mcumgr_serial_process_frag()   base64 + CRC16 해독
 *                                  ↓
 *                            smp_rx_req()               mcumgr 처리
 *                                  ↓
 *                        mcumgr_serial_tx_pkt() ─→ uartWrite()
 *
 * 프레이밍은 표준이라 mcumgr CLI / smpclient 가 그대로 붙는다.
 */

#include "dfu.h"

#include <string.h>


#ifdef _USE_HW_DFU_SERIAL
#include "uart.h"
#include "cli.h"
#include <zephyr/net_buf.h>
#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/mgmt/mcumgr/transport/smp.h>
#include <zephyr/mgmt/mcumgr/transport/serial.h>
#include <mgmt/mcumgr/transport/smp_internal.h>

#if !defined(CONFIG_NU54_DFU_SMP_UART)
#error "_USE_HW_DFU_SERIAL 를 켰으면 CONFIG_NU54_DFU_SMP_UART=y 가 필요하다 (프로젝트 Kconfig)"
#endif


// 한 줄(프레임) 최대 길이. 호스트는 보드가 알려 준 버퍼 크기에 맞춰 나눠 보낸다.
#define DFU_SERIAL_FRAG_MAX       512

// 프레임 시작 표식 (SMP Transport 스펙)
#define DFU_SERIAL_MARK_PKT_1     0x06      // 첫 프레임
#define DFU_SERIAL_MARK_PKT_2     0x09
#define DFU_SERIAL_MARK_FRAG_1    0x04      // 이어지는 프레임
#define DFU_SERIAL_MARK_FRAG_2    0x14


static bool     dfuSerialFeed(uint8_t rx_data);
static int      dfuSerialTxCb(const void *p_data, int length);
static int      dfuSerialTxPkt(struct net_buf *p_nb);
static uint16_t dfuSerialGetMtu(const struct net_buf *p_nb);


static struct smp_transport         transport;
static struct mcumgr_serial_rx_ctxt rx_ctxt;

static uint8_t  frag_buf[DFU_SERIAL_FRAG_MAX];
static uint16_t frag_len = 0;

static uint8_t  mark_1    = 0;        // 표식 첫 바이트를 본 상태
static bool     is_frame  = false;    // 프레임을 받는 중
static bool     is_enable = true;
static uint8_t  tx_ch     = HW_UART_CH_CLI;

static uint32_t rx_pkt_cnt  = 0;
static uint32_t tx_pkt_cnt  = 0;
static uint32_t err_cnt     = 0;
static uint32_t frag_cnt    = 0;   // 받은 줄 수
static uint32_t frag_drop   = 0;   // 줄은 다 받았는데 패킷이 안 된 횟수

// 버려진 줄을 그대로 남겨 둔다.
//
// mcumgr_serial_process_frag() 는 버퍼 할당 실패 / base64 / 길이 / CRC 를 모두
// 같은 NULL 로 돌려줘 이유를 구분할 수 없다. 줄 자체를 보면 갈린다.
//   - 시작줄(0x06 0x09)에서만 버퍼 할당이 일어난다 → 이어짐(0x04 0x14)이면 할당 실패가 아니다
//   - 길이가 4 의 배수가 아니거나 글자가 깨졌으면 눈으로 보인다
static uint8_t  drop_buf[DFU_SERIAL_FRAG_MAX];
static uint16_t drop_len = 0;




bool dfuSerialInit(void)
{
  rx_ctxt.nb = NULL;

  transport.functions.output = dfuSerialTxPkt;
  transport.functions.get_mtu = dfuSerialGetMtu;

  if (smp_transport_init(&transport) != 0)
  {
    return false;
  }

  // cli 수신 바이트를 먼저 보도록 자기를 등록한다 (cli 는 dfu 를 알지 못한다).
  return cliSetRxFilter(dfuSerialRxByte);
}

void dfuSerialEnable(bool enable)
{
  is_enable = enable;
  if (enable != true)
  {
    mark_1 = 0;
    is_frame = false;
    frag_len = 0;
  }
}

bool dfuSerialIsEnable(void)
{
  return is_enable;
}

void dfuSerialGetCnt(uint32_t *p_rx, uint32_t *p_tx, uint32_t *p_err)
{
  if (p_rx  != NULL) *p_rx  = rx_pkt_cnt;
  if (p_tx  != NULL) *p_tx  = tx_pkt_cnt;
  if (p_err != NULL) *p_err = err_cnt;
}

void dfuSerialGetFragCnt(uint32_t *p_frag, uint32_t *p_drop)
{
  if (p_frag != NULL) *p_frag = frag_cnt;
  if (p_drop != NULL) *p_drop = frag_drop;
}

uint16_t dfuSerialGetDropFrag(uint8_t **pp_buf)
{
  if (pp_buf != NULL) *pp_buf = drop_buf;
  return drop_len;
}

/*
 * cli 가 읽은 바이트를 먼저 보여 준다. SMP 프레임이면 true 를 돌려 cli 가 무시하게 한다.
 *
 * 0x06 / 0x04 는 표식의 첫 바이트다. 뒤가 표식이 아니면 그 바이트는 버린다
 * (cli 입력으로 쓰는 문자가 아니다). 그 대신 상태가 꼬이지 않는다.
 */
bool dfuSerialRxByte(uint8_t ch, uint8_t rx_data)
{
  if (is_enable != true) return false;
  if (ch != HW_UART_CH_CLI) return false;      // BLE 채널에는 자체 SMP 서비스가 있다

  tx_ch = ch;

  if (dfuSerialFeed(rx_data) != true)
  {
    return false;
  }

  // 들어와 있는 것을 여기서 다 비운다.
  // cliMain() 은 한 번에 한 바이트만 처리하므로, 그대로 두면 115200 bps 연속 수신을
  // 따라가지 못해 RX 큐가 넘친다 (업로드가 중간에 멈춘다).
  //
  // is_frame 은 줄 끝(\n)마다 false 가 된다. 그것만 보면 한 패킷이 여러 줄일 때
  // 줄 경계마다 한 바이트씩 처리하는 길로 되돌아간다. rx_ctxt.nb 가 있으면
  // 아직 패킷을 모으는 중이라는 뜻이므로 그 동안에도 계속 비운다.
  // 큐가 비면 바로 돌아오므로 cliMain() 이 기다리지 않는다는 성질은 그대로다.
  while (uartAvailable(ch) > 0 &&
         (is_frame == true || mark_1 != 0 || rx_ctxt.nb != NULL))
  {
    if (dfuSerialFeed(uartRead(ch)) != true)
    {
      // 패킷을 모으는 중에 프레임이 아닌 바이트가 섞였다. 큐에서 이미 꺼냈으므로
      // cli 로 돌려줄 수 없다. 모으던 것을 버리고 cli 에게 넘긴다.
      err_cnt++;
      break;
    }
  }

  return true;
}

// 한 바이트 처리. SMP 가 가져갔으면 true.
//
bool dfuSerialFeed(uint8_t rx_data)
{
  if (is_frame == true)
  {
    if (rx_data == '\n')                       // 한 줄이 끝나면 해독한다
    {
      struct net_buf *p_nb;

      frag_cnt++;
      p_nb = mcumgr_serial_process_frag(&rx_ctxt, frag_buf, frag_len);
      if (p_nb != NULL)
      {
        rx_pkt_cnt++;
        smp_rx_req(&transport, p_nb);
      }
      else if (rx_ctxt.nb == NULL)
      {
        // 아직 모으는 중이면 NULL 이 정상이다. 모으는 것도 없는데 NULL 이면
        // 버린 것이다 (버퍼 할당 실패 / base64 / 길이 / CRC — 전부 조용히 NULL).
        // 이 경우 호스트는 응답을 못 받고 타임아웃을 본다.
        frag_drop++;

        drop_len = frag_len;
        memcpy(drop_buf, frag_buf, frag_len);
      }
      frag_len = 0;
      is_frame = false;
      return true;
    }

    if (rx_data == '\r')                       // CRLF 의 CR 은 버린다
    {
      return true;
    }

    if (frag_len < sizeof(frag_buf))
    {
      frag_buf[frag_len++] = rx_data;
    }
    else
    {
      err_cnt++;                               // 한 줄이 너무 길다 → 이 프레임은 버린다
      frag_len = 0;
      is_frame = false;
    }
    return true;
  }

  if (mark_1 != 0)
  {
    uint8_t first = mark_1;

    mark_1 = 0;

    if ((first == DFU_SERIAL_MARK_PKT_1  && rx_data == DFU_SERIAL_MARK_PKT_2) ||
        (first == DFU_SERIAL_MARK_FRAG_1 && rx_data == DFU_SERIAL_MARK_FRAG_2))
    {
      is_frame = true;
      frag_len = 0;
      frag_buf[frag_len++] = first;            // 표식도 해독기에 넘긴다
      frag_buf[frag_len++] = rx_data;
      return true;
    }
    return false;                              // 표식이 아니었다 → 이 바이트는 cli 가 처리
  }

  if (rx_data == DFU_SERIAL_MARK_PKT_1 || rx_data == DFU_SERIAL_MARK_FRAG_1)
  {
    mark_1 = rx_data;
    return true;
  }

  return false;
}


//-- SMP 전송 콜백
//
int dfuSerialTxCb(const void *p_data, int length)
{
  if (uartWrite(tx_ch, (uint8_t *)p_data, (uint32_t)length) != (uint32_t)length)
  {
    return -EIO;
  }
  return 0;
}

int dfuSerialTxPkt(struct net_buf *p_nb)
{
  int ret;

  ret = mcumgr_serial_tx_pkt(p_nb->data, p_nb->len, dfuSerialTxCb);
  if (ret == 0)
  {
    tx_pkt_cnt++;
  }
  else
  {
    err_cnt++;
  }
  smp_packet_free(p_nb);

  return ret;
}

uint16_t dfuSerialGetMtu(const struct net_buf *p_nb)
{
  (void)p_nb;

  // 한 프레임에 실을 수 있는 원본 바이트 수. base64(4/3) + 길이·CRC 자리를 뺀다.
  return (DFU_SERIAL_FRAG_MAX / 4) * 3 - 8;
}

#endif
