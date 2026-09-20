/*
 * ble.c — BLE 스택 (연결 관리, 서비스 등록)
 *
 * 층을 셋으로 나눈다 (hw_def.h 에서 각각 켠다).
 *   _USE_HW_BLE              스택      : 이 파일
 *   _USE_HW_BLE_PERIPHERAL   역할      : ble_adv.c (광고)
 *   _USE_HW_BLE_CENTRAL      역할      : ble_scan.c (스캔·연결) - 자리만
 *   _USE_HW_BLE_NUS 등       서비스    : ble/svc 폴더
 *
 * 서비스는 BLE_SVC_DEF() 로 자기 자신을 .ble_svc 섹션에 등록한다. 서비스를 추가할 때
 * 이 파일은 건드리지 않는다 (파일 하나 + hw_def.h 한 줄).
 *
 * Kconfig 는 firmware/conf/ble*.conf 를 프로젝트 CMakeLists 에서 붙인다.
 */

#include "ble.h"


#ifdef _USE_HW_BLE
#include "cli.h"
#include <zephyr/settings/settings.h>
#include <zephyr/bluetooth/hci.h>

#if !defined(CONFIG_BT)
#error "_USE_HW_BLE 를 켰으면 conf/ble.conf 를 EXTRA_CONF_FILE 에 붙여야 한다"
#endif


#if CLI_USE(HW_BLE)
static void cliBle(cli_args_t *args);
#endif
static void bleConnected(struct bt_conn *p_conn, uint8_t err);
#ifdef _USE_HW_BLE_PERIPHERAL
static void bleAdvRestartWork(struct k_work *p_work);
#endif
static void bleDisconnected(struct bt_conn *p_conn, uint8_t reason);


extern uint32_t _sble_svc;
extern uint32_t _eble_svc;

static bool            is_init = false;
static struct bt_conn *p_cur_conn = NULL;
static char            device_name[32] = HW_BLE_DEVICE_NAME;

#ifdef _USE_HW_BLE_PERIPHERAL
// 연결 해제 콜백 안에서 bt_le_adv_start() 를 부르면 -ENOMEM 이 난다.
// 그 시점에는 연결 객체가 아직 정리되지 않았다 → 워크큐로 미뤄서 시작한다.
static K_WORK_DEFINE(adv_restart_work, bleAdvRestartWork);
#endif

static struct bt_conn_cb conn_cb =
{
  .connected    = bleConnected,
  .disconnected = bleDisconnected,
};




bool bleInit(void)
{
  int   err;
  int32_t svc_count = ((int)&_eble_svc - (int)&_sble_svc) / sizeof(ble_svc_t);
  ble_svc_t *p_svc = (ble_svc_t *)&_sble_svc;


  err = bt_enable(NULL);
  if (err)
  {
    logPrintf("[E_] bleInit() : bt_enable %d\n", err);
    return false;
  }

  if (IS_ENABLED(CONFIG_SETTINGS))
  {
    settings_load();      // 본드 등 저장된 값 (nvs 예제의 Settings + ZMS)
  }

  bt_set_name(device_name);
  bt_conn_cb_register(&conn_cb);

  is_init = true;

  logPrintf("[OK] bleInit()\n");
  logPrintf("     name : %s\n", bleGetDeviceName());
  logPrintf("     svc  : %d\n", svc_count);

  for (int i = 0; i < svc_count; i++)
  {
    bool ret = true;

    if (p_svc[i].init != NULL)
    {
      ret = p_svc[i].init();
    }
    logPrintf("       %s %s\n", p_svc[i].name, ret ? "OK" : "Fail");
    is_init &= ret;
  }

#ifdef _USE_HW_BLE_PERIPHERAL
  bleAdvInit();
  bleAdvStart();
#endif

#if CLI_USE(HW_BLE)
  cliAdd("ble", cliBle);
#endif

  return is_init;
}

bool bleIsInit(void)
{
  return is_init;
}

bool bleIsConnected(void)
{
  return p_cur_conn != NULL;
}

struct bt_conn *bleGetConn(void)
{
  return p_cur_conn;
}

const char *bleGetDeviceName(void)
{
  return bt_get_name();
}

bool bleSetDeviceName(const char *p_name)
{
  if (p_name == NULL) return false;

  snprintf(device_name, sizeof(device_name), "%s", p_name);

  return bt_set_name(device_name) == 0;
}

// 고속 모드 : 인터벌을 최소(7.5 ms)로 당겨 처리량과 응답을 올린다. 그만큼 전류를 더 쓴다.
// 끄면 저전력 쪽 값(15~30 ms)으로 되돌린다. 상대가 거부할 수 있다 (요청일 뿐이다).
// 버퍼 쪽 한계는 conf/ble_throughput.conf 로 같이 올린다.
//
bool bleSetFastMode(bool enable)
{
  if (p_cur_conn == NULL) return false;

#if defined(CONFIG_BT_USER_PHY_UPDATE)
  {
    int err = bt_conn_le_phy_update(p_cur_conn, BT_CONN_LE_PHY_PARAM_2M);

    if (err != 0) logPrintf("[E_] ble phy 2M %d\n", err);
  }
#endif

  // 단위 1.25 ms : 6 = 7.5 ms, 12~24 = 15~30 ms. 타임아웃 4 초.
  return bt_conn_le_param_update(p_cur_conn,
                                 enable ? BT_LE_CONN_PARAM(6, 6, 0, 400)
                                        : BT_LE_CONN_PARAM(12, 24, 0, 400)) == 0;
}

// 연결/해제는 등록된 모든 서비스에 알린다.
//
void bleConnected(struct bt_conn *p_conn, uint8_t err)
{
  int32_t    svc_count = ((int)&_eble_svc - (int)&_sble_svc) / sizeof(ble_svc_t);
  ble_svc_t *p_svc = (ble_svc_t *)&_sble_svc;


  if (err)
  {
    logPrintf("[E_] ble connect %d\n", err);
    return;
  }

  p_cur_conn = bt_conn_ref(p_conn);

#if defined(CONFIG_BT_USER_PHY_UPDATE)
  // 2M PHY 를 요청한다. 같은 데이터를 절반 시간에 보내므로 처리량이 오르고 송신 시간이 줄어 전력에도 좋다.
  // 상대가 거부하면 1M 그대로 쓴다 (실패해도 문제 없음).
  {
    int phy_err = bt_conn_le_phy_update(p_conn, BT_CONN_LE_PHY_PARAM_2M);

    if (phy_err != 0) logPrintf("[E_] ble phy 2M %d\n", phy_err);
  }
#endif

#ifdef _USE_HW_BLE_PERIPHERAL
  bleAdvSetStopped();     // 연결되면 스택이 광고를 멈춘다
#endif

  for (int i = 0; i < svc_count; i++)
  {
    if (p_svc[i].connected != NULL)
    {
      p_svc[i].connected(p_conn);
    }
  }
}

void bleDisconnected(struct bt_conn *p_conn, uint8_t reason)
{
  int32_t    svc_count = ((int)&_eble_svc - (int)&_sble_svc) / sizeof(ble_svc_t);
  ble_svc_t *p_svc = (ble_svc_t *)&_sble_svc;


  for (int i = 0; i < svc_count; i++)
  {
    if (p_svc[i].disconnected != NULL)
    {
      p_svc[i].disconnected(p_conn);
    }
  }

  if (p_cur_conn != NULL)
  {
    bt_conn_unref(p_cur_conn);
    p_cur_conn = NULL;
  }

#ifdef _USE_HW_BLE_PERIPHERAL
  bleAdvSetStopped();
  k_work_submit(&adv_restart_work);      // 끊기면 다시 광고 (연결 객체 정리 뒤에)
#endif
}


#ifdef _USE_HW_BLE_PERIPHERAL
void bleAdvRestartWork(struct k_work *p_work)
{
  bleAdvStart();
}
#endif


#if CLI_USE(HW_BLE)
void cliBle(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    int32_t    svc_count = ((int)&_eble_svc - (int)&_sble_svc) / sizeof(ble_svc_t);
    ble_svc_t *p_svc = (ble_svc_t *)&_sble_svc;

    cliPrintf("init      : %s\n", is_init ? "True" : "False");
    cliPrintf("name      : %s\n", bleGetDeviceName());
    cliPrintf("connected : %s\n", bleIsConnected() ? "True" : "False");
    if (p_cur_conn != NULL)
    {
      struct bt_conn_info conn_info;

      if (bt_conn_get_info(p_cur_conn, &conn_info) == 0 && conn_info.type == BT_CONN_TYPE_LE)
      {
        // 인터벌 단위 1.25 ms, 타임아웃 단위 10 ms. 짧은 데이터의 지연은 대부분 이 인터벌이다.
        cliPrintf("  interval: %d.%02d ms\n",
                  (conn_info.le.interval * 125) / 100, (conn_info.le.interval * 125) % 100);
        cliPrintf("  latency : %d\n", conn_info.le.latency);
        cliPrintf("  timeout : %d ms\n", conn_info.le.timeout * 10);
#if defined(CONFIG_BT_USER_PHY_UPDATE)
        cliPrintf("  phy     : tx %d, rx %d\n", conn_info.le.phy->tx_phy, conn_info.le.phy->rx_phy);
#endif
#if defined(CONFIG_BT_USER_DATA_LEN_UPDATE)
        cliPrintf("  data len: tx %d, rx %d\n",
                  conn_info.le.data_len->tx_max_len, conn_info.le.data_len->rx_max_len);
#endif
      }
    }
#ifdef _USE_HW_BLE_PERIPHERAL
    cliPrintf("adv       : %s\n", bleAdvIsRunning() ? "running" : "stopped");
#endif
    cliPrintf("service   : %d\n", svc_count);
    for (int i = 0; i < svc_count; i++)
    {
      cliPrintf("  %s\n", p_svc[i].name);
      if (p_svc[i].info != NULL)
      {
        p_svc[i].info();
      }
    }
    ret = true;
  }

  // 보드 쪽에서 연결을 끊는다 (호스트가 링크를 붙잡고 있을 때)
  if (args->argc == 1 && args->isStr(0, "disconnect"))
  {
    if (p_cur_conn != NULL)
    {
      int err = bt_conn_disconnect(p_cur_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);

      cliPrintf("ble disconnect : %s (%d)\n", err == 0 ? "OK" : "Fail", err);
    }
    else
    {
      cliPrintf("연결 없음\n");
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "fast"))
  {
    bool enable = args->isStr(1, "on");

    cliPrintf("ble fast %s : %s\n", enable ? "on" : "off",
              bleSetFastMode(enable) ? "요청함" : "Fail");
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "name"))
  {
    cliPrintf("ble name %s : %s\n", args->getStr(1),
              bleSetDeviceName(args->getStr(1)) ? "OK" : "Fail");
    ret = true;
  }

#ifdef _USE_HW_BLE_PERIPHERAL
  if (args->argc == 2 && args->isStr(0, "adv"))
  {
    bool enable = args->isStr(1, "on");

    cliPrintf("ble adv %s : %s\n", enable ? "on" : "off",
              (enable ? bleAdvStart() : bleAdvStop()) ? "OK" : "Fail");
    ret = true;
  }
#endif

  if (ret == false)
  {
    cliPrintf("ble info\n");
    cliPrintf("ble name str\n");
    cliPrintf("ble disconnect\n");
    cliPrintf("ble fast on:off\n");
#ifdef _USE_HW_BLE_PERIPHERAL
    cliPrintf("ble adv on:off\n");
#endif
  }
}
#endif

#endif
