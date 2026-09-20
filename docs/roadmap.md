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
| 13 | `pmic` | BQ25186 충전기: 상태/인터럽트/충전 제어 | I2C 0x6A, P1.11 INT, P2.08 PG, P2.10 CE | (신규, i2c 사용) | INT 인터럽트로 상태 변화 감지 | |
| 14 | `nvs` | 설정 저장 (storage 파티션), eeprom 에뮬레이션 | RRAM `storage_partition` | nvs, eeprom, flash | 쓰기 횟수·타이밍 | |
| 15 | `rtc` | **날짜·시계**: 연월일 시분초, epoch(UTC), 시간대, `rtc` CLI(`rtc info / set date / set time / tz`), 주기 깨우기, 워치독, **log 타임스탬프** (§5) | GRTC(LFXO), WDT31, 보존 RAM | rtc (NU87 API), reset | GRTC 는 System OFF 에서도 동작, 1초 틱 없이 조회 시 계산 | |
| 16 | `ble_nus` | **BLE NUS 를 uart 가상 채널로 추가 → baram-term 과 통신** | RADIO | uart(`uartSetDriver`), cli | 광고/연결 간격, TX 전력 | |
| 17 | `ble_power` | BLE 저전력 튜닝: 광고 주기, 연결 파라미터, 슬레이브 레이턴시 | RADIO | | 광고/연결 상태별 평균 전류 표 | |
| 18 | `dfu` | MCUboot + SMP 로 펌웨어 업데이트 (UART / BLE) | slot0/slot1 파티션 | loader, ymodem | 부트로더 크기와 부팅 시간 | |
| 19 | `app` | 위 모듈을 합친 기본 펌웨어 (cli + ble_nus + 센서 + 전원 관리) | 전체 | ap/system | 동작 모드별 전류 | |
| 20 | `epaper` | **WeAct 4.2" e-paper (SSD1683, 400×300)** (마지막 단계, app 에 화면 추가) : SPI, 화면 버퍼, 글자/도형, 전체/부분 갱신 | SPI00 + GPIO (P2 헤더, §3) | spi, lcd (+ lcd/ssd1306 구조) | 갱신 후 deep sleep, 부분 갱신, 필요 시 VCC 차단 | |

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
- **확인 필요**: baram-term 의 NUS 접속 방식 (장치 이름/주소로 찾기, 재연결, 줄바꿈 처리)

## 3. e-paper (20단계, 마지막) 계획

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

## 4. ap 모듈 구조 (10단계) 계획

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

## 5. rtc 날짜·시계 (15단계) 계획

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

## 6. 단계 공통 체크리스트

- [ ] 레퍼런스 모듈 확인 (구조는 NU87 → nu54dk, nRF54L15 Zephyr 사용법은 nrf54l15-bd 도 확인) → 구조 유지하며 이식
- [ ] 모듈에 CLI 명령 추가 (`#if CLI_USE(HW_xxx)`), cli 로 먼저 시험
- [ ] 보드 DTS 에 필요한 노드/alias 추가 (핀 하드코딩 금지)
- [ ] 빌드 / 다운로드 / 디버그 / 콘솔 확인
- [ ] 저전력 항목 확인, 가능하면 전류 측정값 기록
- [ ] `docs/NN_<이름>.md` 작성, [00_handoff.md](00_handoff.md) 진행 상황·다음 할 일 갱신
