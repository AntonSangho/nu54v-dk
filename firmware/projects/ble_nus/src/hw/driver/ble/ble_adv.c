/*
 * ble_adv.c — 광고 (주변기기 역할, _USE_HW_BLE_PERIPHERAL)
 *
 * 광고 내용 : 이름 + NUS UUID. 호스트(baram-term)가 무엇으로 보드를 고를지에 맞춰 조정한다.
 *
 * 저전력 : 광고 간격이 곧 대기 전류다. 연결이 없으면 계속 광고하므로,
 *          나중에 "빠른 광고 → 느린 광고 → 정지(버튼으로 재시작)" 로 바꾼다 (로드맵 17 ble_power).
 */

#include "ble.h"


#if defined(_USE_HW_BLE) && defined(_USE_HW_BLE_PERIPHERAL)
#include <bluetooth/services/nus.h>

#if !defined(CONFIG_BT_PERIPHERAL)
#error "_USE_HW_BLE_PERIPHERAL 을 켰으면 conf/ble_peripheral.conf 를 붙여야 한다"
#endif


#define BLE_ADV_INT_MIN   BT_GAP_ADV_FAST_INT_MIN_2      // 100 ms
#define BLE_ADV_INT_MAX   BT_GAP_ADV_FAST_INT_MAX_2      // 150 ms


static bool is_adv = false;

/*
 * 광고 패킷(31 바이트) : flags(3) + NUS UUID 128비트(18) = 21
 * 스캔 응답          : 이름 + 제조사 데이터
 *
 * 도구는 UUID 로 거르고 사람은 이름으로 고른다. 제조사 데이터에는 보드 종류·펌웨어 버전과
 * 칩 고유 ID 하위 4바이트를 넣는다 — 같은 이름의 보드가 여럿일 때 자동 시험에서 고르기 위함.
 * (baram-term 쪽 요청)
 */
static uint8_t mfg_data[9] =
{
  0xFF, 0xFF,               // 회사 ID : 0xFFFF (내부·시험용)
  HW_BLE_BOARD_ID,          // 보드 종류
  0, 0,                     // 펌웨어 버전 (major, minor)
  0, 0, 0, 0,               // 칩 고유 ID 하위 4바이트
};

static const struct bt_data adv_data[] =
{
  BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
  BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

static struct bt_data scan_data[] =
{
  BT_DATA(BT_DATA_NAME_COMPLETE, HW_BLE_DEVICE_NAME, sizeof(HW_BLE_DEVICE_NAME) - 1),
  BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_data, sizeof(mfg_data)),
};


bool bleAdvInit(void)
{
  uint32_t dev_id;

  // 칩 고유 ID (FICR) 하위 4바이트
  dev_id = NRF_FICR->INFO.DEVICEID[0];

  mfg_data[3] = HW_BLE_FW_VER_MAJOR;
  mfg_data[4] = HW_BLE_FW_VER_MINOR;
  mfg_data[5] = (uint8_t)(dev_id >> 0);
  mfg_data[6] = (uint8_t)(dev_id >> 8);
  mfg_data[7] = (uint8_t)(dev_id >> 16);
  mfg_data[8] = (uint8_t)(dev_id >> 24);

  is_adv = false;
  return true;
}

bool bleAdvStart(void)
{
  int err;
  struct bt_le_adv_param param = *BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN,
                                                  BLE_ADV_INT_MIN, BLE_ADV_INT_MAX, NULL);

  if (is_adv) return true;

  err = bt_le_adv_start(&param, adv_data, ARRAY_SIZE(adv_data), scan_data, ARRAY_SIZE(scan_data));
  if (err && err != -EALREADY)
  {
    logPrintf("[E_] bleAdvStart() : %d\n", err);
    return false;
  }

  is_adv = true;
  return true;
}

bool bleAdvStop(void)
{
  int err = bt_le_adv_stop();

  if (err && err != -EALREADY)
  {
    return false;
  }

  is_adv = false;
  return true;
}

bool bleAdvIsRunning(void)
{
  return is_adv;
}

#endif
