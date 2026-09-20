# 16. BLE NUS (`firmware/projects/ble_nus`)

rtc 예제(15) 위에 BLE 를 올린다. **NUS 를 uart 가상 채널로 붙여 cli 가 그대로 BLE 위에서 돌게** 한다.
포함 모듈: 지금까지 만든 것 전부 + `ble`

## 1. 구조 — 스택 / 역할 / 서비스 3층

옵션을 층마다 따로 켠다. 서비스가 늘어도 위층 코드는 그대로다.

```
hw/driver/ble/ble.c            스택    : bt_enable, 연결 테이블, 서비스 등록·이벤트 분배   _USE_HW_BLE
hw/driver/ble/ble_adv.c        역할    : 광고 (주변기기)                                  _USE_HW_BLE_PERIPHERAL
hw/driver/ble/ble_scan.c       역할    : 스캔·연결 (중앙)                                 _USE_HW_BLE_CENTRAL   (자리만)
hw/driver/ble/svc/ble_nus.c    서비스  : NUS 서버 → uart 채널 HW_UART_CH_BLE               _USE_HW_BLE_NUS
                               서비스  : NUS 클라이언트, 배터리, DFU …                     (추가 예정)
```

서비스는 `module.c` 의 `MODULE_DEF` 와 같은 방식으로 **자기 자신을 링커 섹션(`.ble_svc`)에 등록**한다.

```c
BLE_SVC_DEF(nus)
{
  .name         = "nus",
  .init         = bleNusInit,
  .connected    = bleNusConnected,
  .disconnected = bleNusDisconnected,
};
```

→ 서비스 추가 = **파일 하나 + `hw_def.h` 한 줄**. `ble.c` 는 건드리지 않는다.

### Kconfig 도 층마다 나눈다

`CONFIG_BT_*` 는 C 매크로로 끌 수 없으므로 조각 파일을 두고 프로젝트 CMakeLists 에서 붙인다.

| 파일 | 내용 |
|---|---|
| `firmware/conf/ble.conf` | 스택 공통 (BT, 이름, 본드 저장, MTU 247 / DLE) |
| `firmware/conf/ble_peripheral.conf` | 주변기기 역할, 연결 간격 선호값 |
| `firmware/conf/ble_central.conf` | 중앙 역할 (자리만) |
| `firmware/conf/ble_nus.conf` | NUS 서버 |
| `firmware/conf/ble_nus_client.conf` | NUS 클라이언트 (자리만) |

```cmake
list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_LIST_DIR}/../../conf/ble.conf …)
```

BLE 를 안 쓰는 예제는 아무것도 붙이지 않으므로 **코드도 Kconfig 도 전부 빠진다**.
`hw_def.h` 와 Kconfig 가 어긋나면 `#error` 로 잡는다 (예: `_USE_HW_BLE_NUS` 인데 `CONFIG_BT_NUS` 없음).

## 2. cli 가 BLE 로 넘어가는 방식

```
호스트 → RX 특성 write → received 콜백 → qbuffer → uartRead()
uartWrite() → bt_nus_send() → TX 특성 notify → 호스트
```

`cli_mgr` 스레드가 채널을 고른다 (NU87 과 같은 방식).

- BLE 가 준비되면(연결 + notify 켜짐) cli 를 BLE 채널로 넘기고 **프롬프트를 한 번 찍는다**
- 로컬 VCOM 에 입력이 들어오면 **즉시 되돌아온다** — 원격에 물려 있어도 콘솔을 잃지 않는다
- notify 가 꺼져 있으면 출력은 버린다 (막히지 않는다)

## 3. baram-term 연동 (baram-term 세션 검토 결과)

| 항목 | 결정 |
|---|---|
| BLE 지원 | baram-term 은 현재 **pyserial 전용, BLE 없음**. `socket://IP:PORT` 를 포트로 받을 수 있어, PC 쪽 BLE↔TCP 다리를 쓰면 터미널·로그·그래프·외부 제어가 그대로 동작한다 (다리를 만들지, baram-term 에 BLE 를 넣을지는 사용자 결정) |
| 식별 | 광고에 **NUS UUID**(도구가 거름) + 스캔 응답에 **완전한 이름**(사람이 고름) |
| 제조사 데이터 | `0xFFFF` + 보드 종류 + 펌웨어 major/minor + **칩 고유 ID 하위 4바이트** — 같은 이름 보드가 여럿일 때 자동 시험에서 고르기 위함 |
| MTU / PHY | MTU 247, DLE 251, 2M PHY. 23 바이트면 부팅 로그·help 출력에서 패킷 수가 10배 |
| 연결 간격 | 타이핑 왕복이 간격의 2배 → 활성 15~30 ms. 대기 중에는 늘리되 입력·출력이 생기면 바로 빠르게 (17 ble_power) |
| 줄끝·프롬프트 | 시리얼과 동일하게 CR + `cli# `. baram-term 의 Tab 자동완성·여러 줄 보내기가 `^\S*# ` 로 프롬프트를 찾는다 |
| 보안 | 본딩 요구 없음 (`CONFIG_BT_NUS_AUTHEN=n`). 이 SDK 에서는 `BT_NUS_SECURITY_ENABLED` 가 없어졌다 |
| baram-ctl | 포트 종류와 무관하게 동작. 다리를 쓰면 `--match <TCP 포트>` 로 창을 고른다 |

## 4. 저전력

| 항목 | 내용 |
|---|---|
| 광고 | 연결 전에는 계속 광고한다 (100~150 ms). 빠른 광고 → 느린 광고 → 정지는 17 ble_power |
| 연결 | 지금은 항상 빠른 간격. 대기 중 간격 늘리기 + 슬레이브 레이턴시도 17 에서 |
| 수신 | notify 콜백에서 qbuffer 에 넣고 `uartRxNotify()` 로 깨운다 (폴링 없음) |
| 크기 | BLE 를 켜면 FLASH +130 KB, RAM +25 KB (226 KB / 50 KB) |

## 5. 검증 결과 (2026-09-20, NCS v3.4.1, macOS)

- [x] 빌드: FLASH 226 KB / RAM 50 KB
- [x] `ble info` : 광고 동작, 서비스 목록에 `nus`
- [x] 호스트 스캔(bleak): 이름 `NU54V-DK`, NUS UUID, 제조사 데이터 `0101005fdf6eb4`
- [x] 연결 MTU **247**, notify 켠 직후 프롬프트 수신
- [x] BLE 로 `ble info` / `rtc info` / `adc info` 실행
- [ ] baram-term 연동 (BLE↔TCP 다리 또는 baram-term BLE 지원 — 사용자 결정)
- [ ] 소비전류 (17 ble_power)
