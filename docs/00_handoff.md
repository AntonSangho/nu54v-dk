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
  - https://github.com/chcbaram/baram-nrf54-arduino : **같은 보드(NU54V-DK)** Arduino 코어. 보드 실측 기록(`docs/boards/NU54V-DK.md`)과 실기 함정 목록(`CLAUDE.md` §7)

## 진행 상황

| 날짜 | 내용 |
|---|---|
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
- [ ] Windows / Linux 에서 빌드·다운로드·디버깅 확인
- [ ] 소비전류 측정 (J1 + PPK2, SWD 분리) — [11_power.md](11_power.md) §5 표 채우기, DC/DC 판단
- [ ] 다음 예제: [roadmap.md](roadmap.md) 순서 (**12 adc** → 13 pmic → … → 16 ble_nus → … → 20 epaper(마지막))
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
