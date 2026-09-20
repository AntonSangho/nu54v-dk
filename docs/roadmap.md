# 브링업 로드맵

보드 기능을 하나씩 살리면서 `firmware/projects/<이름>` 예제와 `docs/NN_<이름>.md` 문서를 함께 만든다.
**각 예제는 앞 단계 예제를 복사해서 모듈을 하나씩 더한다** (누적). 그래서 뒤로 갈수록 한 펌웨어에 기능이 모인다.
04 led 만 단독이고, 05 uart 부터는 앞 단계의 모듈을 모두 포함한다.
**05 `uart` 이후 예제는 모두 cli 를 포함한다** → 새 모듈은 자기 CLI 명령(`i2c`, `shtc3`, `button` …)을 넣고, ap 에 시험 코드를 넣지 않고 cli 에서 먼저 시험한다.

- 문서 번호 = 구현 순서. 순서가 바뀌면 번호도 바꾼다.
- **참조 모듈**: 레퍼런스의 같은 이름 모듈. 구조는 유지하고 이 보드와 저전력에 맞게 고친다.
  - [nu54dk](https://github.com/chcbaram/nu54dk) `firmware/nu54l15-fw` : 이전 nRF54L 보드 (Zephyr)
  - [NU87-TinyDK](https://github.com/chcbaram/NU87-TinyDK) `firmware/nu87-fw` : 더 최신 구조 (uart 가상 채널, cli, ap 모듈). 두 곳에 같은 모듈이 있으면 NU87 을 먼저 본다.
  - [nrf54l15-bd](https://github.com/chcbaram/nrf54l15-bd) `firmware/*` : nRF54L15 (Zephyr) 프로젝트 여러 개. 같은 SoC 라 Zephyr API 사용법 참고용
    | 프로젝트 | 참고할 단계 |
    |---|---|
    | `nrf54l-fw` | button, adc, eeprom(→ nvs), log, ap/modules(module·system·cli) |
    | `an54l-power` | 주변장치 suspend/resume (uart, spi 의 `pm_device`) → power |
    | `an54l-oled`, `an54l-fw` | i2c, spi, spi_flash, lcd(+hangul, resize) → epaper (20) |
    | `nrf54l-fw-fota` | **BLE NUS (`CONFIG_BT_NUS`, ble_uart 모듈) + MCUboot + MCUmgr BT OTA DFU** → ble_nus, dfu |
    | `xiao-nrf54l-fw` | 최소 구성 (led, log, uart, cli) |
  - [qmk-zephyr](https://github.com/chcbaram/qmk-zephyr) `firmware/nrf52-qmk-fw` : Zephyr 기반 QMK. Settings/NVS, emu-eeprom RAM 미러 + settle-flush, BLE 프로파일 저장, USB HID. `docs/PORTING-NOTES.md` 에 실기 함정 정리
  - [baram-nrf54-arduino](https://github.com/chcbaram/baram-nrf54-arduino) : **같은 보드(NU54V-DK)** 의 Arduino 코어. `docs/boards/NU54V-DK.md`(실측한 솔더 브리지·핀·PMIC·J1), `CLAUDE.md` §7 (F8 디버거와 System OFF, F9 WFI/BASEPRI 등 실기에서 잡은 함정)
- 모든 단계에서 저전력 항목을 확인한다. 전류는 J1(VDD_MOD)에서 PPK2 로 잰다.

## 1. 단계별 계획

| 문서 | 프로젝트 | 내용 | 보드 자원 | 참조 모듈 | 저전력 확인 | 상태 |
|---|---|---|---|---|---|---|
| 04 | `led` | LED 점멸, 빌드/다운로드/디버그 경로 확인 | LED1~4 | led | 폴링 없는 루프 | ✅ |
| 05 | `uart` | **공통 기반**: uart(VCOM1/VCOM0, async DMA, 가상 채널 구조) + qbuffer + cli. baram-term 시리얼로 명령 | uart20, uart30 | uart, cli (NU87), qbuffer | RX 켜짐/꺼짐 전류, 입력 대기 sleep | ✅ |
| 06 | `i2c` | i2c 모듈, `i2c scan/read/write` 로 장치 확인 | i2c21 (P1.02/03), Qwiic J5, PMIC | i2c | TWIM PM runtime | ✅ |
| 07 | `shtc3` | Qwiic SHTC3 온습도 센서 드라이버 + `shtc3` CLI | I2C 0x70 | (신규) | 측정 시에만 센서 wakeup | ✅ |
| 08 | `button` | 스위치 입력, 디바운스, 클릭/길게 누름 이벤트, `button` CLI | SW1~4 | button (stm32h7-lvgl 최신판 API 일부) | GPIO SENSE 인터럽트 + 1회 타이머, 주기 스캔 없음 | ✅ |
| 09 | `log` | 부팅 로그 버퍼, 로그 채널, `log` CLI | VCOM1 | log (nu54dk, API NU87) | `logDisable()` 로 UART 송신 끄기 | ✅ |
| 10 | `module` | **ap 모듈 구조**: `MODULE_DEF` 로 모듈 등록, 모듈별 스레드, cli_mgr(cli 스레드, 입력 대기 sleep), `module info/thread` (§4) | | module (NU87), ldscript (nu54dk) | 모든 스레드가 이벤트로만 깨어남, main 은 잠듦 | ✅ |
| 11 | `power` | reset(리셋 원인) + power(System OFF, 버튼/GRTC 깨우기, 레귤레이터 모드). **전류 측정은 나중에** | SW, GRTC, J1 | reset (NU87), System OFF (Zephyr 샘플, baram-nrf54-arduino) | SWD 분리 + 전원 재인가 후 시험 (11_power §4) | ✅ (측정 예정) |
| 12 | `adc` | 배터리 전압(VBAT_MON, 분압 ×1.470) + 칩 온도, `adc`/`temp` CLI | P1.12(AIN5), TEMP | adc (nrf54l15-bd) | 읽을 때만 SAADC 동작, 분압 누설 2.8 µA | ✅ |
| 13 | `pmic` | BQ25186 충전기: 상태·이상·설정 읽기, 충전 전류 설정, /CE 제어, `/INT` 이벤트 | I2C 0x6A, P1.11 INT, P2.08 PG, P2.10 CE | (신규, Arduino pmic 예제 참고) | 폴링 없이 /INT 인터럽트 | ✅ |
| 14 | `nvs` | 설정 저장 (Zephyr Settings + ZMS, storage 파티션), `nvs` CLI | RRAM `storage_partition` | nvs (NU87 API), qmk-zephyr (쓰기 합치기) | 값이 바뀔 때만 쓰기, 잦으면 모아서 | ✅ |
| 15 | `rtc` | 날짜·시계 (기준 epoch + GRTC, 보존 RAM), 시간대(nvs), log 타임스탬프, `rtc` CLI | GRTC, 보존 RAM 4 KB | rtc (NU87 API) | 틱 없음, System OFF 에서도 시각 유지 | ✅ |
| 16 | `ble_nus` | BLE 스택/역할/서비스 3층, NUS 를 uart 가상 채널로 → cli 가 BLE 에서 동작 | RADIO | uart(`uartSetDriver`), cli_mgr (NU87) | 수신 콜백 → 알림, 광고/연결 간격은 17 | ✅ |
| 17 | `ble_power` | BLE 저전력 튜닝: 광고 주기, 연결 파라미터, 슬레이브 레이턴시 | RADIO | | 광고/연결 상태별 평균 전류 표 | |
| 18 | `dfu` | MCUboot(swap using move) + SMP 로 펌웨어 업데이트. **시리얼 SMP 는 cli 포트(VCOM1) 위에 얹는다** (VCOM0 는 프로브 결함으로 못 쓴다). 서명 키는 저장소에 포함. `dfu info/test/confirm/revert/serial` CLI, `fw dfu` + VS Code 태스크 | slot0/slot1 파티션 | nrf/samples/dfu/smp_svr, nrf54l15-bd `nrf54l-fw-fota` | MCUboot 56 KB, 부팅 시간 미측정 | ✅ |
| 19 | `web_dfu` | <https://chcbaram.github.io/nu54v-dk/> — 탭으로 **WebUSB(SWD)** / **BLE** / **시리얼**. SWD 는 빈 보드에 MCUboot+앱 전체 설치, 나머지는 앱 무선·유선 업데이트 (§8) | | dapjs (버그 3개 우회), 직접 만든 SMP/CBOR | 업로드 중 연결 간격만 당기고 복귀 | ✅ |
| 20 | `app` | 위 모듈을 합친 기본 펌웨어 (cli + ble_nus + 센서 + 전원 관리) | 전체 | ap/system | 동작 모드별 전류 | |
| 21 | `epaper` | **WeAct 4.2" e-paper (SSD1683, 400×300)** (마지막 단계, app 에 화면 추가) : SPI, 화면 버퍼, 글자/도형, 전체/부분 갱신 | SPI00 + GPIO (P2 헤더, §3) | spi, lcd (+ lcd/ssd1306 구조) | 갱신 후 deep sleep, 부분 갱신, 필요 시 VCC 차단 | |

선택 예제 (필요할 때):

| 프로젝트 | 내용 |
|---|---|
| `pwm` | LED2/LED4 밝기 조절. PWM20 은 P1 포트에서만 동작 → LED2(P1.10), LED4(P1.14)만 가능 |
| `swo` | SWO 트레이스 (P2.07, LED3 와 공유 — SB13) |
| `flpr` | RISC-V 코프로세서(FLPR)로 소프트 주변장치 |

## 2. BLE NUS ↔ baram-term (16단계) 설계 방향

목표: 기존 cli 를 선 없이 BLE 로 쓴다. baram-term 이 BLE NUS 로 보드에 연결해 시리얼처럼 명령을 주고받는다.

```
baram-term ──BLE (NUS RX/TX 특성)──► nus 채널 ─┐
                                               ├─ uart.c (_DEF_UARTx 채널) ─► cli / log
VCOM1 (uart20) ─────────────────────► UART 채널 ┘
```

- **uart 가상 채널로 넣는다**: 05 의 uart 모듈(NU87 구조)은 채널마다 `uart_driver_t`(open/close/available/flush/read/write)를 등록할 수 있다.
  NUS 를 `uart_driver_t` 로 구현해 `uartSetDriver(HW_UART_CH_BLE, &nus_driver)` 로 붙이면 cli·log 는 채널 번호만 바꿔서 BLE 로 동작한다
  (NU87 의 `HW_UART_CH_BLE` / `cli_ble` 와 같은 방식).
- **수신**: NUS RX 콜백 → qbuffer 에 넣기 → `uartAvailable/uartRead` 로 꺼내기 (VCOM 수신과 같은 흐름)
- **송신**: `uartWrite` → 연결되어 있고 알림(notify)이 켜져 있으면 MTU 크기로 나눠 전송. 연결이 없으면 버리거나 버퍼에 둔다.
- **SDK**: NCS `bt_nus` 서비스(`CONFIG_BT_NUS`)를 우선 검토한다. nrf54l15-bd `nrf54l-fw-fota` 의 `ble_uart` 모듈이 같은 SoC 에서 이미 쓴 예다 (NCS peripheral_uart 샘플 기반). Zephyr 의 `CONFIG_BT_ZEPHYR_NUS` 도 비교한다.
- **MTU / 데이터 길이**: 처리량을 위해 MTU 247, Data Length Extension, 2M PHY 사용 여부 결정
- **저전력**:
  - 광고: 연결 전 빠른 광고 → 일정 시간 뒤 느린 광고(예: 1 s) 또는 멈춤, 버튼으로 다시 시작
  - 연결: 대기 중에는 긴 연결 간격 + 슬레이브 레이턴시, 데이터가 오갈 때만 짧은 간격 요청
  - TX 전력은 필요한 만큼만
- **옵션으로 뺄 수 있게 한다**: `hw_def.h` 의 `_USE_HW_BLE` 하나로 BLE 모듈·cli_ble 채널·`CONFIG_BT*` 까지 빠지게 한다
  (Kconfig 는 프로젝트의 `prj.conf` 가 아니라 BLE 를 쓰는 예제에서만 켠다). 끈 상태로도 빌드·동작이 그대로여야 한다.
- **확인 필요**: baram-term 의 NUS 접속 방식 (장치 이름/주소로 찾기, 재연결, 줄바꿈 처리)

## 3. e-paper (21단계, 마지막) 계획

자료: https://github.com/WeActStudio/WeActStudio.EpaperModule (`Doc/4.2 Inch Black&Write`, `Doc/4.2 Inch Black&Write&Red`, `Doc/SSD1683_Datasheet.PDF`)

| 항목 | 내용 |
|---|---|
| 패널 | 4.2", 400×300, SSD1683. 흑백(E042A87) / 흑백적(E042A88) 중 사용 모델 확인 필요 |
| 커넥터 | 8핀: VCC, GND, SDA(MOSI), SCL(SCK), CS, DC, RES, BUSY (모듈에 3.3 V LDO ME6231) |
| 인터페이스 | 4선 SPI (쓰기 전용, MISO 없음) + DC/RES 출력 + BUSY 입력 |
| 버퍼 | 흑백 400×300 / 8 = 15,000 바이트 (흑백적은 ×2) → RAM 188 KB 중 여유 있음 |
| 드라이버 | Zephyr(3.3.0) 에 SSD1683 없음 → 레퍼런스 `lcd.c` + `lcd/ssd1306.c` 구조로 `lcd/ssd1683.c` 작성. 예제 코드 `4D2_BW_400X300_1683_UT From MCU.c` 참고 |

핀 배치 (제안 — 실제 배선에 맞춰 확정). 헤더 P2 로만 나와 있는 P2.00~P2.06 을 쓴다.

| 신호 | 핀 | 비고 |
|---|---|---|
| SCL (SCK) | P2.01 | SPI00 SCK (nRF54L15 DK 와 같은 배치) |
| SDA (MOSI) | P2.02 | SPI00 MOSI |
| CS | P2.05 | |
| DC | P2.03 | |
| RES | P2.00 | |
| BUSY | P2.04 | 입력 (갱신 끝나면 인터럽트로 깨우기) |
| (VCC 스위치) | P2.06 | 선택: 부하 스위치로 모듈 전원 차단 |

저전력:
- 화면 갱신이 끝나면(BUSY) 패널을 deep sleep (SSD1683 `0x10` 명령) → e-paper 는 전원 없이 화면 유지
- BUSY 는 폴링 대신 GPIO 인터럽트 + 세마포어로 기다린다 (전체 갱신 수 초 동안 CPU sleep)
- 자주 바뀌는 부분(시간, 온습도)은 부분 갱신, 전체 갱신은 잔상 제거가 필요할 때만
- 오래 쓰지 않을 때는 모듈 VCC 를 끊는다 (모듈 LDO 대기 전류까지 제거, 하드웨어 추가 필요)
- SPI 는 PM runtime 으로 전송할 때만 켠다

## 4. dfu (18단계) 메모 — baram-term 쪽 요청

- baram-term 은 **NUS 만** 쓴다. SMP/MCUmgr 는 구현하지 않는다 → DFU 는 mcumgr / nRF Connect 로 하고, baram-term 은 그 전후로 CLI 를 쓰는 창이다.
- DFU 중에는 NUS 연결이 끊긴다. baram-term 의 **자동 재연결(1초 간격)** 이 계속 붙으려 하므로,
  사용자가 창에서 연결을 끊거나 자동화라면 `baram-ctl release` → 끝나고 `resume` 을 쓴다. 이 절차를 dfu 문서에 적는다.
- **DFU 모드에서도 광고 이름을 바꾸지 않는다.** baram-term 은 이름으로 장치를 기억한다 (`ble://NU54V-DK#5fdf6eb4`).
  바꿔야 할 일이 생기면 baram-term 쪽에 먼저 알린다.
- 업데이트 뒤 버전 확인은 CLI `info` 로 한다 (16 예제부터 있음).

## 5. ap 모듈 구조 (10단계) 계획

지금은 `apMain()` 에서 `cliMain()` 을 직접 돈다. 기능이 늘면 레퍼런스의 ap 모듈 구조로 옮긴다.

```
ap/
├── ap.c                 apInit() → moduleInit() → 각 모듈 init / 스레드 시작
└── modules/
    ├── module.c/h       MODULE_DEF(name) { .name, .priority, .init, (.update) }  → ".module" 링커 섹션에 모인다
    ├── common/cli/      cli 모듈 : 자기 스레드에서 cliMain()
    └── system/          system 모듈 : 준비 완료 신호(systemIsReady) 등
```

| 항목 | nu54dk | NU87-TinyDK | 이 보드에서 |
|---|---|---|---|
| 모듈 등록 | `MODULE_DEF` + `.module` 섹션 (링커 스크립트) | 같음 + `update` 콜백, `MODULE_PRI_MAX` | 같은 방식. Zephyr 에서는 `zephyr_linker_sources` 또는 `ITERABLE_SECTION` 으로 섹션 정의 |
| 실행 | 모듈이 스레드를 직접 만듦 (`cliThread`: `cliMain(); delay(5);`) | bare-metal: `moduleUpdate()` 순회, `cliLoopIdle()` 훅 | **모듈별 스레드**. 스레드는 폴링하지 않고 이벤트(`uartWaitRx`, 세마포어, 메시지 큐)로만 깨어난다 |
| 우선순위 | `_HW_DEF_RTOS_THREAD_PRI_xxx`, `_MEM_xxx` (hw_def.h) | | 같은 정의 사용 |

저전력: 모든 모듈 스레드가 이벤트를 기다리는 동안 idle 스레드가 WFI 로 들어간다. 주기 작업은 `k_timer`/`k_work_delayable` 로.

## 6. rtc 날짜·시계 (15단계) 계획

API 는 NU87 `rtc.h` 를 그대로 쓴다.

```c
rtcGetInfo / rtcSetInfo      // 날짜 + 시각 (지역 시각)
rtcGetDate / rtcSetDate      // year, month, day, week
rtcGetTime / rtcSetTime      // hours, minutes, seconds
rtcGetEpochTime / rtcSetEpochTime   // UTC epoch (초)
rtcGetTimeZone / rtcSetTimeZone     // UTC 로부터의 분 (한국 +540)
rtcIsTimeSet
```

nRF54L15 에는 달력 RTC 가 없다. NU87(RTL8720DF) 과 같은 방식으로 **기준 epoch + 카운터** 로 만든다.

```
epoch = base_epoch + (GRTC 카운터 - base_count) / 1 000 000
```

| 항목 | 내용 |
|---|---|
| 카운터 | GRTC (LFXO 32.768 kHz 기반, µs 단위). System OFF 에서도 계속 동작 |
| 기준값 보관 | `base_epoch`, `base_count`, 시간대를 **보존 RAM**(retained RAM, 리셋에도 유지)에 둔다. 전원이 완전히 꺼지면 시각을 잃는다 → `rtcIsTimeSet()` false |
| 시각 맞추기 | cli `rtc set date/time`, 이후 BLE(16)로 호스트(baram-term) 시각 동기화, 필요하면 nvs(14)에 시간대 저장 |
| 날짜 계산 | epoch ↔ 연월일/요일 변환은 NU87 `rtcCivilToEpoch` 방식 (윤년 포함) |
| 저전력 | 1초 틱 인터럽트를 쓰지 않는다. 시각은 조회할 때 계산하고, 알람/주기 깨우기만 GRTC compare 로 한다 |
| 정확도 | LFXO(외부 크리스털 Y1) 오차 ±20 ppm 수준 → 하루 약 ±2 초. 장기간이면 주기 동기화 |
| log 타임스탬프 | log.c 의 `logBufHeader()` 에서 줄 머리에 날짜·시각을 넣는다 (로그가 생긴 순간의 시각). 시각이 설정되지 않았으면 부팅 후 경과 시간 |

## 7. 단계 공통 체크리스트

- [ ] 레퍼런스 모듈 확인 (구조는 NU87 → nu54dk, nRF54L15 Zephyr 사용법은 nrf54l15-bd 도 확인) → 구조 유지하며 이식
- [ ] 모듈에 CLI 명령 추가 (`#if CLI_USE(HW_xxx)`), cli 로 먼저 시험
- [ ] 보드 DTS 에 필요한 노드/alias 추가 (핀 하드코딩 금지)
- [ ] 빌드 / 다운로드 / 디버그 / 콘솔 확인
- [ ] 저전력 항목 확인, 가능하면 전류 측정값 기록
- [ ] `docs/NN_<이름>.md` 작성, [00_handoff.md](00_handoff.md) 진행 상황·다음 할 일 갱신

## 8. 웹 업데이트 (19단계) 와 UF2 검토 결과

> **구현과 실측은 [19_web_dfu.md](19_web_dfu.md) 로 옮겼다.** 여기는 왜 그렇게 정했는지만 남긴다.

### 웹으로 간다 — 경로 셋을 한 페이지에

저장소의 GitHub Pages 에 정적 페이지 하나를 두고 브라우저에서 바로 굽는다.
GitHub Pages 가 HTTPS 라 세 API 모두의 요구 조건을 만족한다. 사용자는 아무것도 설치하지 않는다.

| 경로 | API | 쓰는 때 | 빈 보드 | 상태 |
|---|---|---|---|---|
| **SWD** | WebUSB + CMSIS-DAP (dapjs) | 부트로더+앱 전체 설치, 벽돌 복구 | **가능** | ✅ 304 KB / 9.5 초 |
| **BLE** | Web Bluetooth + SMP | 앱 무선 업데이트 | 불가 | ✅ 247 KB / 17~42 초 |
| **시리얼** | Web Serial + SMP | 업데이트 / 자동 시험 | 불가 | ✅ 247 KB / 48 초 |

페이지는 탭(SWD / BLE / 시리얼)으로 나뉘고 로그는 아래에 계속 보인다.

SMP 와 CBOR 는 직접 만들었다 (`web/js/smp.js`). 전송 계층만 갈아 끼우면 된다
(`ble.js`, `serial.js`).

**시리얼 프레이밍에서 걸린 것** : base64 는 줄마다 따로 인코딩하는 것이 아니라
**하나의 연속 문자열을 줄로 쪼갠 것**이다 (마지막 줄에만 `=` 패딩). 받을 때도 문자열을
먼저 이어 붙인 뒤 디코딩해야 한다. 줄마다 디코딩하면 아무것도 안 나온다.
보드는 길이를 미리 주지 않는 CBOR(indefinite length)로 답하므로 그것을 읽어야 한다.
광고에 SMP UUID 가 없으므로 (NUS UUID 만 실린다) 장치는 **이름**으로 찾는다.

### 버전과 상태 (실측)

버전을 올리지 않아도 올라간다 (`MCUBOOT_DOWNGRADE_PREVENTION` 꺼져 있음). 막히는 경우는 둘이다.

| 오류 | 뜻 | 풀기 |
|---|---|---|
| `rc=6` | 실행 중 이미지가 확정 전(test) | 먼저 confirm |
| `rc=1` | slot1 이 slot0 과 완전히 같은 이미지 | 바꿀 것이 없다 |

되는 브라우저 : WebUSB·Web Serial 은 Chrome / Edge 데스크톱 (WebUSB 는 Android 도), Web Bluetooth 는 여기에 Opera 추가.
**Safari · Firefox · iOS 는 전부 안 된다.**

#### dapjs 의 버그 세 가지 (2026-09-20 실측)

브라우저에서만 실패하고 pyOCD 로 같은 순서를 재현하면 잘 되어서, Node 에서 dapjs 를
그대로 돌려 좁혔다 (`usb` 패키지의 WebUSB 폴리필 → 브라우저와 같은 경로).

| 문제 | 증상 | 우회 |
|---|---|---|
| **`writeBlock` 이 256 워드까지만 맞다** | 그보다 크면 안에서 나눠 보내며 **주소를 진행시키지 않아** 덩어리가 모두 같은 자리에 겹쳐 쓰인다. 1024 워드를 쓰면 0 번째 자리에 768 번째 값이 들어온다 | 256 워드씩 나눠 쓴다 |
| `waitDelay(fn, timeout, interval)` | interval 이 아니라 timeout 만큼 잠든다. 알고리즘 호출 한 번에 최대 10 초 | `execute()` 를 쓰지 않고 직접 폴링 (2 ms) |
| `SELECT` / `CSW` 캐시 | 타깃을 리셋하면 하드웨어는 초기화되는데 캐시가 남아 이후 전송이 엉뚱한 AP·뱅크로 간다. `connect()` 도 지우지 않는다 | 리셋할 때마다 `selectedAddress`/`cswValue` 를 버린다 |

첫 번째가 결정적이었다. 굽기는 "성공" 하는데 내용이 쓰레기라, MCUboot 가 부팅할 이미지를
못 찾고 멈추고 → APPROTECT 가 걸린 채 남아 → 디버그 접근까지 막혔다.

#### APPROTECT 와 디버그 접근

**AHB-AP 가 비활성이면 APPROTECT 가 걸린 것이다.** nRF54L 은 리셋할 때 걸린 채 부팅하고,
정상 펌웨어가 돌면서 해제한다. 그래서 펌웨어가 돌지 않으면(패닉, 이미지 없음, 코어 halt)
디버그가 막힌다. 빠져나오려면 CTRL-AP 전체 삭제뿐이다.

- MCUboot 만 굽지 않는다 → `build/merged.hex` 를 쓴다
- 굽기 뒤에는 코어를 halt 로 두지 않는다. 먼저 `C_HALT` 를 풀고 리셋한다
  (halt 상태에서 `C_DEBUGEN` 을 0 으로 쓰면 ARM 사양상 동작 미정의)
- 리셋은 **CTRL-AP RESET 에 2 → 0** (pyOCD 와 같다). `AIRCR.SYSRESETREQ` 가 아니다

#### SWD (WebUSB) 가 되는 이유

- 보드의 프로브가 WebUSB 를 내놓는다 : `DETAILS.TXT` → `USB Interfaces: MSD, CDC, HID, WebUSB`.
  MSD 드래그앤드롭이 실패하는 것은 DAPLink 의 *타깃 인식* 부분이고, CMSIS-DAP 자체는 멀쩡하다 (pyOCD 가 그것으로 굽는다).
- nRF54L15 의 RRAM 쓰기 루틴이 아주 작다. pyOCD 의 `FLASH_ALGO` 는 명령어 240 바이트 남짓이고
  RRAM 컨트롤러(`0x5004B000`)를 찔러 워드 단위로 쓰는 게 전부다 (`page_size: 0x4`, erase 없음).
  → dapjs 에 그대로 실어 쓸 수 있다.
  참고 : `pyocd/target/builtin/target_nRF54L15.py`, `pyocd/target/family/target_nRF54L.py` (CTRL-AP 로 ERASEALL / APPROTECT 해제도 여기 있다)

- 시리얼 SMP 프레이밍 : `0x06 0x09` 시작 마커 + 2바이트 길이 + **base64 본문** + **CRC16(0x1021, 초기값 0)**.
  Zephyr 에 base64 를 쓰지 않는 `CONFIG_MCUMGR_TRANSPORT_RAW_UART` 도 있지만 mcumgr CLI 같은 표준 도구와 안 맞는다 → 표준 쪽을 쓴다.
- 펌웨어 바이너리(`zephyr.signed.bin`)를 Pages 에 같이 올리면 파일 고르기 없이 "최신 버전 굽기" 가 된다 (GitHub Actions 로 빌드 → 배포).
- **주의**: BLE 는 중앙이 하나뿐이라 baram-term 등이 연결 중이면 브라우저 장치 목록에 보드가 안 보인다 (연결 중에는 광고를 멈춘다).
  페이지에 안내문을 넣는다. 자동화는 `baram-ctl release` → 업데이트 → `resume`.
- `docs/` 는 한국어 문서라, Pages 는 별도 `web/` 폴더를 Actions 로 `gh-pages` 에 배포한다.

### UF2 는 하지 않는다

- **nRF54L15 에는 USB 주변장치가 없다** (Zephyr DTS 에 USB 노드가 없다). UF2 부트로더는 칩이 스스로 USB 저장장치로 떠야 하므로 성립하지 않는다.
- 보드를 꽂으면 보이는 드라이브(`NU54V2PRE`)는 **온보드 DAPLink 프로브**가 만든 것이다 (`DETAILS.TXT` : "based on DAPLink", HIC ID `6e052840`, MSD/CDC/HID/WebUSB).
  드롭한 파일을 프로브가 SWD 로 굽는 구조이고, `INFO_UF2.TXT` 가 없으니 UF2 모드가 아니라 `.hex`/`.bin` 을 받는다.
- 게다가 지금 이 프로브는 타깃을 인식하지 못한다 — `Target Detect: SWD init failed`,
  `FAILURE.TXT` : "Unable to read or identify supported target information over SWD."
  pyOCD(`fw flash`)는 CMSIS-DAP 명령으로 직접 SWD 를 다루므로 잘 된다. MSD 드래그앤드롭은 프로브 펌웨어에 타깃 알고리즘이 있어야 한다.
- Zephyr 의 `CONFIG_BUILD_OUTPUT_UF2` 는 family ID 목록에 nRF54L 이 없다.
- **결론**: 초기 설치·복구는 `fw flash`(SWD), 현장 업데이트는 SMP, 사용자 업데이트는 웹. UF2 / MSD 경로는 만들지 않는다.
