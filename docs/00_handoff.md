# 00. 작업 인계 (여기부터 읽기)

다른 PC 나 새 세션에서 작업을 이어갈 때 이 문서부터 본다.
작업을 마칠 때마다 **진행 상황**과 **다음 할 일**을 갱신한다.

## 문서 목록 (번호 = 구현 순서)

| 번호 | 문서 | 내용 |
|---|---|---|
| 00 | [00_handoff.md](00_handoff.md) | 인계, 진행 상황, 결정 사항, 작업 규칙 |
| 01 | [01_memory_map.md](01_memory_map.md) | nRF54L15 메모리맵, 파티션 |
| 02 | [02_board_package.md](02_board_package.md) | 보드 패키지, 핀맵, 전원 |
| 03 | [03_build_debug_env.md](03_build_debug_env.md) | 폴더 구조, 빌드·다운로드·디버그 (3개 OS, VS Code) |
| 04 | [04_led.md](04_led.md) | LED 예제 |
| 05 | [05_uart.md](05_uart.md) | UART(async) + CLI — 이후 예제의 공통 기반 |
| 06 | [06_i2c.md](06_i2c.md) | I2C, `i2c scan` |
| 07 | [07_shtc3.md](07_shtc3.md) | Qwiic SHTC3 온습도 센서 |
| 08 | [08_button.md](08_button.md) | 버튼: 인터럽트 방식, 클릭 / 길게 누름 |
| 09 | [09_log.md](09_log.md) | 로그: boot/list 버퍼, `log` CLI |
| 10 | [10_module.md](10_module.md) | ap 모듈 구조, 모듈별 스레드 (cli_mgr) |
| 11 | [11_power.md](11_power.md) | 리셋 원인, System OFF (버튼/GRTC 깨우기), **시험 절차(SWD 분리)** |
| 12 | [12_adc.md](12_adc.md) | 배터리 전압(ADC), 칩 온도 |
| 13 | [13_pmic.md](13_pmic.md) | BQ25186 충전기 (상태 읽기, 충전 전류 설정) |
| 14 | [14_nvs.md](14_nvs.md) | 설정 저장 (Settings + ZMS) |
| 15 | [15_rtc.md](15_rtc.md) | 날짜·시계, 로그 타임스탬프 |
| 16 | [16_ble_nus.md](16_ble_nus.md) | BLE NUS (cli 를 BLE 로), 서비스 확장 구조, 송신 MTU 모으기 |
| 18 | [18_dfu.md](18_dfu.md) | MCUboot 펌웨어 업데이트 (BLE / 시리얼 SMP, 보드와 호스트 도구) |
| 19 | [19_web_dfu.md](19_web_dfu.md) | 웹 업데이트 도구 — SWD(WebUSB) / BLE / 시리얼, dapjs 버그, 배포 |
| - | [roadmap.md](roadmap.md) | 브링업 로드맵 (05 이후 예제 계획, BLE NUS ↔ baram-term) |

새 기능은 [roadmap.md](roadmap.md) 의 번호대로 `NN_<기능>.md` 를 추가하고 위 표에 적는다.

## 프로젝트 개요

- 보드: NUCODE **NU54-DK** (Variant NU-54DK-C), 모듈 NCRB54N01VC = **nRF54L15**
- 회로도: `hardware/NU54_DK_2026-07-13T15_16_09_UTC_SCH.pdf`
- 온보드 디버거: nRF52840 CMSIS-DAP (USB 이름 `NU54DK_v2`) → pyOCD
- SDK: nRF Connect SDK (Zephyr), 버전은 `firmware/ncs_config.json`
- 레퍼런스 프로젝트 (로컬: 이 저장소와 같은 폴더)
  - https://github.com/chcbaram/nu54dk `firmware/nu54l15-fw` : 이전 nRF54L 보드용 (Zephyr). hw/driver 모듈(button, i2c, spi, sd, fatfs, lcd, i2s, log …)
  - https://github.com/chcbaram/NU87-TinyDK `firmware/nu87-fw` : 더 최신 구조 (uart 가상 채널 `uart_driver_t`, cli, ap 모듈). 같은 모듈이 있으면 이쪽을 먼저 본다
  - https://github.com/chcbaram/nrf54l15-bd `firmware/*` : nRF54L15 Zephyr 프로젝트 모음 (button/adc/eeprom/lcd, power, **BLE NUS + FOTA**). 단계별 대응은 roadmap.md
  - https://github.com/chcbaram/qmk-zephyr `firmware/nrf52-qmk-fw` : Zephyr 기반 QMK (Settings/NVS, emu-eeprom 지연 기록, BLE, USB HID). `docs/PORTING-NOTES.md` 에 실기 함정
  - https://github.com/chcbaram/baram-nrf54-arduino : **같은 보드(NU54V-DK)** Arduino 코어. 보드 실측 기록(`docs/boards/NU54V-DK.md`)과 실기 함정 목록(`CLAUDE.md` §7)

## 진행 상황

원 저장소(https://github.com/chcbaram/nu54v-dk, Hancheol Cho)를 이어받아 **2026-09-25 부터 AntonSangho 가 작업**한다.
그 이전 항목은 원저자의 브링업 기록이라 그대로 둔다.

### AntonSangho (2026-09-25 ~)

| 날짜 | 내용 |
|---|---|
| 2026-09-25 | **Linux 빌드·다운로드 확인** (Ubuntu). `firmware/scripts/fw` 버그 수정: 툴체인 python3(`usr/local/bin/python3`)가 자체 `libpython3.12.so.1.0` 을 못 찾아 즉시 죽었다 — `fw.py` 를 실행하기도 전에 나는 에러라 SDK 문제로 오인하기 쉽다. `fw` 스크립트가 python3 실행 전에 툴체인의 `lib`/`lib/x86_64-linux-gnu`/`usr/local/lib` 를 `LD_LIBRARY_PATH` 에 넣도록 고쳤다. NCS v3.4.1 을 `nrfutil toolchain-manager`/`sdk-manager` 로 설치(`~/ncs/v3.4.1`), `projects/led` 를 `fw build -p` 로 빌드하고 `fw flash --probe <UID>` 로 다운로드까지 확인. 온보드 CMSIS-DAP(`NU54DK_v2_Pre-release`) 은 `/etc/udev/rules.d/50-nu54dk.rules` (`idVendor 0d28`, `idProduct 0204`, `MODE 0666`) 없이는 `/dev/bus/usb/...` 가 `root:root 664` 라 pyOCD 가 못 본다 — 규칙 추가 후 `pyocd list` 로 확인됨. VCOM(`/dev/ttyACM*`) 은 기본이 이미 666 이라 별도 규칙 불필요. SEGGER J-Link 를 동시에 물리면 pyOCD 가 두 프로브를 다 보므로 `fw flash --probe <UID>` 로 온보드 프로브를 명시해야 한다 (동시 SWD 연결은 §"내장 프로브" 경고대로 피할 것) |

### Hancheol Cho (원저자, ~ 2026-09-21)

| 날짜 | 내용 |
|---|---|
| 2026-09-21 | **웹 도구 정리** : 조각 크기를 전송 계층이 정하게 했다 (BLE 160 / 시리얼 512). 한 값으로 묶었다가 BLE 를 망가뜨렸다 — 첫 요청만 `len`·`sha` 로 75 B 커져 MTU(241)를 넘었고, 증상은 오류 없이 "응답이 오지 않는다" 였다. 안전장치도 512(브라우저 한계)로 둬서 못 잡았다 → 실제 한계 241 로 고침. **보드 없이 도는 회귀 시험 `web/selftest.js` 추가** (회귀를 실제로 잡는지까지 검사). 진단 로그는 [자세히] 뒤로, js 에 캐시 버스터(`?v=`) |
| 2026-09-20 | **시리얼 DFU 속도·안정성 마무리** ([18_dfu.md](18_dfu.md) §9) : 조각 200 → **512** (5.0 → 6.0 KB/s). 더 키우면 프로브가 버스트를 감당 못 해 조용히 잃거나 덮어쓴다 — 크기가 아니라 타이밍이다 (1024 를 한 번에 쓰면 3/25, 전선보다 느리게 쓰면 25/25). 남은 손상(7,000 프레임에 1회)은 **페이싱**(전선 속도에 맞춰 나눠 쓰기)으로 없앴다 (13,235 프레임 2회 → 17,543 프레임 0회, 속도 손해 없음). 재전송 대기 3 초 → 0.6 초. 제조사 보고서에 §6-2 추가 |
| 2026-09-20 | **웹 DFU 디버깅** ([18_dfu.md](18_dfu.md) §4 함정 7, §9) : BLE 가 붙으면 시리얼 SMP 왕복이 30 ms → **1001 ms** 가 되던 것을 잡았다. `cli_mgr` 이 BLE 로 채널을 넘긴 뒤 **그 채널의 세마포어만** 기다려, 시리얼로 온 요청이 cli 스레드를 못 깨우고 `CLI_MGR_IDLE_WAIT_MS` 를 통째로 쓴 것. 계층을 나눠 고쳤다 — uart 는 알림 훅만(`uartSetRxNotify`), cli 는 `cliFilterPump(ch)` 로 **지정 채널**을 필터에 물리고, 중재는 cli_mgr 이 한다. **SMP 는 cli 의 채널 선택과 무관하게 자기 포트에서 돈다.** 웹 쪽도 셋 고침 (표식이 줄 중간에 와도 찾기 / cli 엔터는 CR / rc 거절은 재전송 안 함). 측정 도구 `scripts/dfu_serial_bench.py` 추가 |
| 2026-09-20 | `projects/dfu` **완료** : MCUboot(0x0, 56 KB) + 앱 slot0(0x10000), ED25519 서명(키 저장소 포함). SMP 를 BLE 와 **cli 포트(VCOM1)** 양쪽에. 시리얼 SMP 는 Zephyr 의 UART 전송 대신 우리 uart 모듈 위에 직접 얹었다 (cli 는 필터 훅만 제공 — dfu 를 모른다). `dfu` CLI, `fw dfu` + VS Code 태스크. BLE 249 KB/15.8초, 시리얼 249 KB/44초 왕복 확인 |
| 2026-09-20 | **내장 프로브(DAPLink) 결함**: VCOM0 를 쓰면 같은 프로브의 SWD 가 죽는다. 원인 분리 후 제조사 보고서 작성 ([reports/2026-09-20_daplink_vcom0_swd.md](reports/2026-09-20_daplink_vcom0_swd.md)). 그래서 시리얼 SMP 를 VCOM1 로 옮겼다 |
| 2026-09-20 | `projects/ble_nus` : BLE 스택/역할/서비스 3층 구조, NUS 를 uart 가상 채널로 붙여 cli 가 BLE 에서 동작. 호스트(bleak)로 스캔·연결·명령 확인. baram-term 은 BLE 미지원 → socket 다리 필요 (사용자 결정) |
| 2026-09-20 | `projects/rtc` : 기준 epoch + GRTC 카운터, 보존 RAM 4 KB(보드 DTS), 시간대 nvs 저장, 로그 타임스탬프. System OFF 는 카운터 유지, 소프트 리셋은 0 부터 (데이터시트와 다름 — 15_rtc §3) |
| 2026-09-20 | `projects/nvs` : Zephyr Settings + ZMS(RRAM 용) 로 이름 기반 저장, 리셋 후 유지 확인. 참조에 qmk-zephyr 추가 |
| 2026-09-20 | `projects/pmic` : BQ25186 상태/이상 읽기, 충전 전류 10 mA → 150 mA (배터리 300 mAh, 0.5C), /CE 제어. Zephyr charger 드라이버는 초기화 시 설정을 덮어써서 보류 |
| 2026-09-20 | 08 button ~ 11 power 예제에 빠져 있던 i2c/shtc3 를 넣어 **누적 규칙**을 맞춤 (각 예제 = 앞 단계 + 새 모듈). 네 예제 모두 보드에서 i2c scan / shtc3 read 확인 |
| 2026-09-20 | `projects/adc` : VBAT(AIN5, ×1.470, 40 µs + 오버샘플링/평균), 칩 온도. 보드 DTS 에 ADC 채널 추가 |
| 2026-09-20 | `projects/power` : reset/power 모듈. System OFF 가 안 깨어나던 원인 = SWD 디버그 모드 (baram-nrf54-arduino F8) → DISABLE_SWD + 전원 재인가로 GRTC/버튼 깨우기 확인. 전류 측정은 나중에 |
| 2026-09-20 | GitHub 공개 저장소 https://github.com/chcbaram/nu54v-dk (MIT). `projects/module` : NU87 module + cli_mgr 스레드, `moduleWaitReady`, main 은 잠듦 |
| 2026-09-20 | `projects/log` : nu54dk log.c (+ ISR 안전, 길이 제한), NU87 순서로 hwInit 정리 |
| 2026-09-20 | `projects/button` : GPIO SENSE 인터럽트 + 디바운스/길게 누름 1회 타이머 (주기 스캔 없음). 보드 DTS 에 버튼 핀 `sense-edge-mask`. 로드맵에서 e-paper 를 마지막(20)으로 |
| 2026-09-20 | **NCS v3.4.1 로 전환** (macOS 12 에서는 cmake 만 시스템 cmake 로 임시 우회, fw 스크립트 자동). 4개 예제 빌드, shtc3 보드 동작·GDB 디버깅 확인. uart 모듈에 `uartRxNotify` (가상 채널 드라이버용). 첫 커밋 `bae82d7` |
| 2026-09-20 | `projects/uart`, `i2c`, `shtc3` 로 분리. uart 모듈을 NU87 구조(가상 채널) + Zephyr async(DMA) 로 새로 작성, cli 는 NU87 버전. 각 모듈 CLI 로 시험 (VCOM0/1, i2c scan, shtc3). NCS v3.4.1 설치 실패 원인 확인 (아래) |
| 2026-09-20 | (이전) `projects/i2c_shtc3` : i2c 모듈 이식, SHTC3 드라이버, 보드 `board.c` 에서 NFC 패드 끄기 (P1.02/03 I2C). SHTC3 측정·PMIC(0x6A) 응답 확인. 로드맵에 e-paper(WeAct 4.2", SSD1683) 추가, cli 를 06 으로 앞당김 |
| 2026-09-20 | git 저장소 생성 (`main`). 보드 패키지 `nu54v_dk`, 빌드 스크립트 `fw`, `projects/led` 작성. macOS 에서 빌드·다운로드·콘솔·GDB 디버깅 확인 (당시 NCS v3.3.0) |

## 다음 할 일

- [ ] **cmake 임시 우회 제거**: NCS v3.4.1 macOS 툴체인의 cmake 는 macOS 14+ 전용 (`minos 14.0`). 이 PC(macOS 12.7.6)에서는
  SDK Manager 의 `west zephyr-export` 가 실패로 끝났지만 SDK 파일은 정상. `fw.py` 의 `fix_cmake()` 가 시스템 cmake(Homebrew 4.4.3)를 대신 쓴다.
  macOS 14+ 로 올리면 우회가 저절로 꺼진다 (코드는 남겨도 무해). 다른 도구(gcc 14.3, gdb 16.2, ninja, pyocd)는 macOS 12 에서도 동작 확인.
- [ ] `NRF_PLATFORM_LUMOS` deprecated 경고: SDK(zephyr/soc/nordic/Kconfig)가 nRF54L 에 기본 y 로 켜는 호환 심볼. SDK 안에서 쓰는 곳 없음.
  다음 릴리스에서 삭제 예정이라 보드에서 끄지 않고 둔다 (끄면 삭제된 버전에서 오히려 에러).
- [ ] LED 육안 확인, VS Code F5 디버깅 확인
- [x] Linux 에서 빌드·다운로드 확인 (2026-09-25, 위 진행 상황 참조). GDB 디버깅(F5)은 아직
- [ ] Windows 에서 빌드·다운로드·디버깅 확인
- [ ] 소비전류 측정 (J1 + PPK2, SWD 분리) — [11_power.md](11_power.md) §5 표 채우기, DC/DC 판단
- [ ] **시리얼 DFU — 브라우저에서 페이싱이 듣지 않는 이유 (선택, 영향 작음)** :
  파이썬은 페이싱으로 손상이 0 이 되는데(17,543 프레임) 웹은 그대로다(약 7,000 프레임에 2 회).
  `writer.write()` 가 실제 USB 전송 시점을 보장하지 않기 때문으로 **추정**하나 확인은 안 했다.
  영향은 43.8 초 중 1.2 초(2.7 %) 뿐이다 ([18_dfu.md](18_dfu.md) §9)
- [ ] **시리얼 DFU 보率 올리기 (선택)** — 지금 249 KB 에 42 초. 1 Mbaud 로 올리면 6 초 예상이다.
  `cli_baud` 까지 바꾸는 런타임 명령을 먼저 만들어 시험한다 (실패해도 리셋하면 115200 으로 복귀).
  같이 봐야 할 것 : `UART_RX_TIMEOUT_US`(1000 → 200 µs), `UART_RX_DMA_LEN`(64 → 128/256),
  그리고 **페이싱 기준 속도** — 지금은 보率로 계산하는데 1 Mbaud 에서는 프로브가 더 느린 고리가
  될 수 있다 ([18_dfu.md](18_dfu.md) §9 "페이싱의 전제"). 보率이 바뀌면 웹·bench·baram-term 설정이 같이 움직인다
- [ ] 다음 예제: [roadmap.md](roadmap.md) 순서 (**17 ble_power** → 18 dfu → … → 16 ble_nus → … → 20 epaper(마지막))
- [ ] e-paper 모델(흑백/흑백적)과 실제 배선 핀 확정
- [ ] 보드 미확인 항목 ([02_board_package.md](02_board_package.md) §5): DC/DC, HFXO 부하, 솔더 브리지

## 결정 사항

| 항목 | 결정 | 이유 |
|---|---|---|
| 프로젝트 위치 | `firmware/projects/<이름>` | 공용 자산(boards, scripts, 설정)과 분리. 모든 프로젝트가 `../../` 로 같은 공용 자산 참조 |
| 보드 패키지 | `firmware/boards` 하나를 공유, `-DBOARD_ROOT` 로 전달 | 보드 수정이 모든 예제에 바로 반영 |
| 보드 이름 | `nu54v_dk/nrf54l15/cpuapp` | 저장소 이름 기준 |
| 빌드 도구 | 자체 스크립트 `fw` (Python + sh/cmd 진입점) | 3개 OS 공통, 전역 PATH·nRF 터미널 불필요, VS Code 태스크와 터미널 명령 동일 |
| 디버거 | Cortex-Debug + pyOCD, `.tools` 링크 | 온보드 CMSIS-DAP 사용. SDK 버전이 바뀌어도 launch.json 고정 |
| 파티션 | DTS 파티션 (Partition Manager 끔) | NCS 3.3 부터 PM deprecated |
| 전원 | DC/DC 끔(LDO) | 모듈 인덕터 유무 미확인 — 안전한 쪽 |
| UART | Zephyr async(DMA) API 를 uart 모듈에서 직접 사용 (console 서브시스템 X) | 수신 스레드 없음, RX 끄면 UARTE suspend, TX 중 CPU sleep ([05_uart](05_uart.md)) |
| 예제 시험 방식 | 모듈마다 CLI 명령, ap 에 시험 코드 넣지 않음 | 사용자 요청 |
| UART 하드웨어 흐름제어(RTS/CTS) | **쓰지 않는다** | 사용자 결정. 보드 DTS 에 핀(SB9~SB12)은 잡혀 있으나 `hw-flow-control` 을 켜지 않는다. CTS 가 `bias-pull-up` 이라 프로브가 구동하지 않으면 보드 TX 가 막힌다 |

## 작업 규칙

1. **펌웨어 구조와 모듈화는 사용자 방식을 유지한다.**
   `main → hwInit/apInit/apMain`, `ap / hw / hw/driver / bsp / common / common/hw/include` 계층,
   `hw_def.h` 의 `_USE_HW_xxx` / `HW_xxx_MAX_CH` 기능 선택, `xxxInit/xxxOpen…` 명명, `_DEF_xxx` 상수.
2. **모듈을 추가할 때는 레퍼런스 저장소(NU87-TinyDK → nu54dk 순, nRF54L15 Zephyr 사용법은 nrf54l15-bd)의 같은 모듈을 먼저 참조**하고,
   구조를 유지하면서 이 보드와 저전력에 맞게 더 적절한 형태로 구현한다.
3. **항상 저전력을 고려한다.**
   바쁜 폴링 대신 sleep/이벤트, 미사용 주변장치는 PM runtime 으로 suspend, 슬립 전 핀 disconnect,
   디버그 전용 옵션은 측정/양산 시 끄기.
4. 핀·주변장치는 코드에 하드코딩하지 않고 보드 DTS(노드/alias)에서 가져온다.
   모듈은 자기 CLI 명령을 가지고, 시험은 cli 로 한다 (ap 에 임시 시험 함수 금지).
5. 기능을 추가하면 `docs/NN_<기능>.md` 를 작성하고, 이 문서의 진행 상황·다음 할 일을 갱신한다.
6. 코드 스타일은 `firmware/.clang-format`.
7. 커밋 메시지에 Claude 서명(Co-Authored-By 등)을 넣지 않는다.
