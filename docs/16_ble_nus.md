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
  (BLE 창과 시리얼 창을 같이 띄워 두면, 시리얼에 글자를 치는 순간 BLE 쪽 출력이 끊긴다)
- 채널이 바뀌면 **로그 출력도 같이 옮긴다** (`logOpen`)
- 명령을 실행하는 중에는 채널 전환을 멈춘다. 전환이 끼어들면 `cliKeepLoop()` 이 엉뚱한 포트를 보게 되어
  반복 명령(`shtc3 read 300`, `adc show` …)이 빠져나오지 못한다
- notify 가 꺼져 있으면 출력은 버린다 (막히지 않는다)

> 채널 전환은 사용자의 [stm32h5-w6300](https://github.com/chcbaram/stm32h5-w6300) `cli_mgr.c` (2026-08) 를 참조했다.
> 그쪽은 텔넷(`HW_UART_CH_NET`)과 USB CDC(보율로 CLI/cmd 를 가름)까지 같은 방식으로 전환한다.

## 3. baram-term 연동 (baram-term 세션 검토 결과)

| 항목 | 결정 |
|---|---|
| BLE 지원 | **baram-term 에 BLE 를 직접 넣기로 결정됨** (사용자 결정, 2026-09-20). `ble://` 전송(NUS 고정) + 포트 설정 창의 장치 검색. bleak 은 선택 설치, 포트 메뉴의 "BLE 장치 사용" 으로 켠다. 다리 프로그램은 만들지 않는다 |
| 식별 | 광고에 **NUS UUID**(도구가 거름) + 스캔 응답에 **완전한 이름**(사람이 고름) |
| 제조사 데이터 | `0xFFFF` + 보드 종류 + 펌웨어 major/minor + **칩 고유 ID 하위 4바이트** — 같은 이름 보드가 여럿일 때 자동 시험에서 고르기 위함 |
| MTU / PHY | MTU 247, DLE 251, 2M PHY. 23 바이트면 부팅 로그·help 출력에서 패킷 수가 10배 |
| 연결 간격 | 타이핑 왕복이 간격의 2배 → 활성 15~30 ms. 대기 중에는 늘리되 입력·출력이 생기면 바로 빠르게 (17 ble_power) |
| 줄끝·프롬프트 | 시리얼과 동일하게 CR + `cli# `. baram-term 의 Tab 자동완성·여러 줄 보내기가 `^\S*# ` 로 프롬프트를 찾는다 |
| 보안 | 본딩 요구 없음 (`CONFIG_BT_NUS_AUTHEN=n`). 이 SDK 에서는 `BT_NUS_SECURITY_ENABLED` 가 없어졌다 |
| baram-ctl | 포트 종류와 무관하게 동작 (창에 요청만 보내므로) |
| 포트 저장 형태 | `ble://NU54V-DK` 처럼 이름 기준 (macOS 는 주소가 PC 마다 다름). 이름이 겹치면 칩 ID 하위 4바이트를 덧붙임 |

## 4. 연결이 끊기면 다시 광고 (실기에서 잡은 것)

끊긴 뒤 광고가 살아나지 않았다. 두 가지가 겹쳐 있었다.

1. 연결되면 **스택이 광고를 멈춘다**. `is_adv` 플래그를 그대로 두면 `bleAdvStart()` 가 "이미 광고 중" 으로 보고 아무것도 하지 않는다
   → 연결 콜백에서 `bleAdvSetStopped()` 로 알려 준다.
2. **연결 해제 콜백 안에서 `bt_le_adv_start()` 를 부르면 `-ENOMEM`(-12) 이 난다.** 그 시점에는 연결 객체가 아직 정리되지 않았다
   → 시스템 워크큐(`k_work`)로 미뤄서 시작한다.

로그에 `[E_] bleAdvStart() : -12` 가 남아 원인을 찾았다. 연결·해제를 반복해도 매번 광고가 돌아오는 것을 확인했다.

### 광고가 안 보이면 먼저 "누가 연결해 있는지" 본다

BLE 는 **중앙 하나만** 붙고, 연결 중에는 광고하지 않는다. 그래서 스캔에 안 보이는 것이 정상인 경우가 많다.

```
cli# ble info
connected : True      ← 이미 누가 붙어 있다. 광고를 안 하는 게 맞다
adv       : stopped
cli# ble disconnect   ← 보드 쪽에서 끊기 (1초 뒤 광고 재시작)
```

실기에서 겪은 것: 스캔에 안 보여 펌웨어를 의심했는데, 사용자의 baram-term 창이 그 보드에 붙어 있었다.
`ble disconnect` 로 끊으면 1초 뒤 광고가 살아나지만 **baram-term 의 자동 재연결(1초 간격, 기본 켜짐)** 이 곧바로 다시 붙어서
2초 안에 `connected : True` 로 돌아간다. 펌웨어 다운로드(리셋) 직후 바로 연결되어 있던 것도 같은 이유다.

**보드 상태(`ble info`)를 먼저 확인하고, 호스트 쪽에서 누가 쓰고 있는지 본다.**
남이 쓰고 있을 수 있으므로 `ble disconnect` 는 함부로 쓰지 않는다.
자동 시험에서 BLE 를 잠시 비워야 하면 baram-term 쪽에서 `baram-ctl release` → 끝나고 `resume` 을 쓴다.

## 5. 저전력

| 항목 | 내용 |
|---|---|
| 광고 | 연결 전에는 계속 광고한다 (100~150 ms). 빠른 광고 → 느린 광고 → 정지는 17 ble_power |
| 연결 | 지금은 항상 빠른 간격. 대기 중 간격 늘리기 + 슬레이브 레이턴시도 17 에서 |
| 수신 | notify 콜백에서 qbuffer 에 넣고 `uartRxNotify()` 로 깨운다 (폴링 없음) |
| 크기 | BLE 를 켜면 FLASH +130 KB, RAM +25 KB (226 KB / 50 KB) |

## 6. 검증 결과 (2026-09-20, NCS v3.4.1, macOS)

- [x] 빌드: FLASH 226 KB / RAM 50 KB
- [x] `ble info` : 광고 동작, 서비스 목록에 `nus`
- [x] 호스트 스캔(bleak): 이름 `NU54V-DK`, NUS UUID, 제조사 데이터 `0101005fdf6eb4`
- [x] 연결 MTU **247**, notify 켠 직후 프롬프트 수신
- [x] BLE 로 `ble info` / `rtc info` / `adc info` / `log info` / `nvs set` 실행
- [x] 연결 → 해제 → **재광고** → 재연결 (2회 반복)
- [x] 채널 전환 : BLE ↔ 시리얼, 로그도 따라감
- [ ] baram-term 의 `ble://` 지원으로 연동 확인 (baram-term 쪽 작업 중)
- [ ] 소비전류 (17 ble_power)
