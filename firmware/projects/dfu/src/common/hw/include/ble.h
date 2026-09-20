#ifndef BLE_H_
#define BLE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_BLE
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>


// BLE 서비스는 각자 파일에서 자기 자신을 등록한다 (module.c 의 MODULE_DEF 와 같은 방식).
//
//   BLE_SVC_DEF(nus)
//   {
//     .name       = "nus",
//     .init       = bleNusInit,
//     .connected  = bleNusConnected,
//     .disconnected = bleNusDisconnected,
//   };
//
typedef struct
{
  const char name[16];
  bool (*init)(void);
  void (*connected)(struct bt_conn *p_conn);
  void (*disconnected)(struct bt_conn *p_conn);
  void (*info)(void);                 // ble info 에서 서비스가 자기 상태를 찍는다 (없어도 된다)
} ble_svc_t;

#define BLE_SVC_DEF(x_name) static __attribute__((section(".ble_svc"))) volatile ble_svc_t ble_svc_##x_name =


bool bleInit(void);
bool bleIsInit(void);
bool bleIsConnected(void);
struct bt_conn *bleGetConn(void);

bool bleSetFastMode(bool enable);   // 2M PHY + 짧은 인터벌 요청 (전류 ↑)

const char *bleGetDeviceName(void);
bool        bleSetDeviceName(const char *p_name);

#ifdef _USE_HW_BLE_PERIPHERAL
bool bleAdvInit(void);
bool bleAdvStart(void);
bool bleAdvStop(void);
void bleAdvSetStopped(void);    // 연결되어 스택이 광고를 멈췄음을 알린다
bool bleAdvIsRunning(void);
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif
