/*
 * dfu.c — MCUboot 이미지 관리 (_USE_HW_DFU)
 *
 * 업로드 자체는 mcumgr(SMP)가 한다 (BLE : ble/svc/ble_smp.c 쪽 Kconfig, 시리얼 : dfu_serial.c).
 * 여기서는 올라온 이미지를 확인하고 확정/되돌리기를 하는 CLI 를 제공한다.
 *
 * 흐름
 *   1. 호스트가 slot1 에 새 이미지를 올린다 (mcumgr / smpclient / 웹)
 *   2. `dfu test`  → 다음 부팅에 slot1 을 시도한다 (MCUboot 가 swap)
 *   3. 리셋 → 새 이미지가 뜬다. 아직 **확정 전** 이라 다시 리셋하면 되돌아간다
 *   4. `dfu confirm` → 확정. 되돌아가지 않는다
 *
 * 3번에서 확정하지 않는 것이 안전장치다. 새 이미지가 부팅에 실패하거나 확정을 못 하면
 * 자동으로 이전 버전으로 돌아간다 (swap using move).
 */

#include "dfu.h"


#ifdef _USE_HW_DFU
#include "cli.h"
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/storage/flash_map.h>


#define DFU_SLOT0_ID    FIXED_PARTITION_ID(slot0_partition)
#define DFU_SLOT1_ID    FIXED_PARTITION_ID(slot1_partition)


#if CLI_USE(HW_DFU)
static void cliDfu(cli_args_t *args);
#endif


static bool is_init = false;




bool dfuInit(void)
{
  is_init = true;

#ifdef _USE_HW_DFU_SERIAL
  if (dfuSerialInit() != true)
  {
    logPrintf("[E_] dfuSerialInit()\n");
    is_init = false;
  }
#endif

  logPrintf("[%s] dfuInit()\n", is_init ? "OK" : "E_");
  logPrintf("     image   : %s\n", boot_is_img_confirmed() ? "confirmed" : "test (확정 전)");

#if CLI_USE(HW_DFU)
  cliAdd("dfu", cliDfu);
#endif

  return is_init;
}

bool dfuIsInit(void)
{
  return is_init;
}

bool dfuGetImage(uint8_t slot, dfu_img_t *p_img)
{
  struct mcuboot_img_header header;
  uint8_t area_id = (slot == 0) ? DFU_SLOT0_ID : DFU_SLOT1_ID;


  if (p_img == NULL) return false;

  memset(p_img, 0, sizeof(dfu_img_t));
  p_img->slot = slot;

  if (boot_read_bank_header(area_id, &header, sizeof(header)) != 0)
  {
    return false;                 // 비어 있거나 유효한 헤더가 없다
  }
  if (header.mcuboot_version != 1)
  {
    return false;
  }

  p_img->is_valid   = true;
  p_img->version[0] = header.h.v1.sem_ver.major;
  p_img->version[1] = header.h.v1.sem_ver.minor;
  p_img->version[2] = (uint8_t)header.h.v1.sem_ver.revision;
  p_img->build      = header.h.v1.sem_ver.build_num;

  if (slot == 0)
  {
    p_img->is_active    = true;
    p_img->is_confirmed = boot_is_img_confirmed();
  }

  return true;
}

bool dfuIsConfirmed(void)
{
  return boot_is_img_confirmed();
}

bool dfuConfirm(void)
{
  return boot_write_img_confirmed() == 0;
}

// slot1 이미지를 다음 부팅에 시도한다 (확정하지 않는다 → 실패하면 되돌아온다)
bool dfuTest(void)
{
  return boot_request_upgrade(BOOT_UPGRADE_TEST) == 0;
}

// 확정 전이면 다음 부팅에 이전 이미지로 돌아간다.
// 이미 확정된 뒤에는 slot1 에 이전 이미지가 남아 있어야 하며, test 와 같은 절차로 되돌린다.
bool dfuRevert(void)
{
  if (boot_is_img_confirmed() != true)
  {
    return true;                  // 아직 확정 전 → 그냥 리셋하면 돌아간다
  }
  return boot_request_upgrade(BOOT_UPGRADE_TEST) == 0;
}

bool dfuErase(void)
{
  return boot_erase_img_bank(DFU_SLOT1_ID) == 0;
}


#if CLI_USE(HW_DFU)
void cliDfu(cli_args_t *args)
{
  bool ret = false;


  // 출력은 `키 : 값` 한 줄 형식을 지킨다 (자동 시험에서 파싱한다)
  if (args->argc == 1 && args->isStr(0, "info"))
  {
    dfu_img_t img;

    for (int slot = 0; slot < 2; slot++)
    {
      if (dfuGetImage(slot, &img) != true)
      {
        cliPrintf("slot%d.version : -\n", slot);
        continue;
      }
      cliPrintf("slot%d.version : %d.%d.%d+%d\n", slot,
                img.version[0], img.version[1], img.version[2], img.build);
      cliPrintf("slot%d.flags   : %s%s\n", slot,
                img.is_active ? "active" : "-",
                img.is_active ? (img.is_confirmed ? ",confirmed" : ",test") : "");
    }
#ifdef _USE_HW_DFU_SERIAL
    {
      uint32_t rx_cnt, tx_cnt, err_cnt;
      uint32_t frag_cnt, drop_cnt;

      dfuSerialGetCnt(&rx_cnt, &tx_cnt, &err_cnt);
      dfuSerialGetFragCnt(&frag_cnt, &drop_cnt);
      cliPrintf("serial.state  : %s\n", dfuSerialIsEnable() ? "on" : "off");
      cliPrintf("serial.pkt    : rx %d, tx %d, err %d\n", rx_cnt, tx_cnt, err_cnt);
      // frag 은 받은 줄 수, drop 은 줄은 다 받았는데 패킷이 안 된 횟수다.
      // drop 이 오르면 그만큼 호스트가 응답 없이 타임아웃을 봤다는 뜻이다.
      cliPrintf("serial.frag   : %d, drop %d\n", frag_cnt, drop_cnt);

      // 마지막으로 버린 줄. 시작줄(06 09)이면 버퍼 할당 실패일 수 있고,
      // 이어짐(04 14)이면 할당은 성공한 뒤라 base64 / 길이 / CRC 쪽이다.
      if (drop_cnt > 0)
      {
        uint8_t *p_buf;
        uint16_t len = dfuSerialGetDropFrag(&p_buf);

        int32_t  nb_len;
        uint16_t pkt_len;
        uint8_t  prev_mark[2];
        uint16_t prev_len;

        dfuSerialGetDropInfo(&nb_len, &pkt_len, prev_mark, &prev_len);
        cliPrintf("serial.drop2  : nb_len %d, pkt_len %d, 앞줄 %02X %02X len %d\n",
                  (int)nb_len, pkt_len, prev_mark[0], prev_mark[1], prev_len);
        cliPrintf("                %s\n",
                  nb_len < 0 ? "모으던 것이 없었다 → 할당 실패 또는 앞줄 유실"
                             : "모으고 있었다 → base64 / 길이 / CRC");
        cliPrintf("serial.drop   : mark %02X %02X, len %d (base64 %d, %s)\n",
                  p_buf[0], p_buf[1], len, len - 2,
                  ((len - 2) % 4) == 0 ? "4의 배수" : "4의 배수 아님");
        cliPrintf("                ");
        for (int j = 2; j < len; j++)
        {
          cliPrintf("%c", (p_buf[j] >= 32 && p_buf[j] < 127) ? p_buf[j] : '.');
        }
        cliPrintf("\n");
      }
    }
#endif
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "confirm"))
  {
    cliPrintf("dfu confirm : %s\n", dfuConfirm() ? "OK" : "Fail");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "test"))
  {
    cliPrintf("dfu test : %s (리셋하면 slot1 로 부팅한다)\n", dfuTest() ? "OK" : "Fail");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "revert"))
  {
    if (dfuIsConfirmed() != true)
    {
      cliPrintf("dfu revert : 확정 전이다. 리셋하면 이전 이미지로 돌아간다\n");
    }
    else
    {
      cliPrintf("dfu revert : %s\n", dfuRevert() ? "OK" : "Fail");
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "erase"))
  {
    cliPrintf("dfu erase slot1 : %s\n", dfuErase() ? "OK" : "Fail");
    ret = true;
  }

#ifdef _USE_HW_DFU_SERIAL
  if (args->argc == 2 && args->isStr(0, "serial"))
  {
    bool enable = args->isStr(1, "on");

    dfuSerialEnable(enable);
    cliPrintf("dfu serial %s\n", enable ? "on" : "off");
    ret = true;
  }
#endif

  if (ret == false)
  {
    cliPrintf("dfu info\n");
    cliPrintf("dfu test\n");
    cliPrintf("dfu confirm\n");
    cliPrintf("dfu revert\n");
    cliPrintf("dfu erase\n");
#ifdef _USE_HW_DFU_SERIAL
    cliPrintf("dfu serial on:off\n");
#endif
  }
}
#endif

#endif
