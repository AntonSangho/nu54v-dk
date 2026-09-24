# NU54-DK

NUCODE NU54-DK (nRF54L15) 보드 브링업 펌웨어.

> **브라우저에서 바로 굽기** → <https://chcbaram.github.io/nu54v-dk/>
> 설치할 것 없이 USB 케이블만 꽂으면 된다 (Chrome / Edge). 빈 보드도 된다.

## 개발 환경

| 항목 | 버전 / 내용 |
|---|---|
| SoC | nRF54L15 (모듈 NCRB54N01VC), 앱 코어 Cortex-M33 `cpuapp` |
| 보드 타깃 | `nu54v_dk/nrf54l15/cpuapp` (`firmware/boards/nucode/nu54v_dk`) |
| nRF Connect SDK | **v3.4.1** (`firmware/ncs_config.json` 에서 관리) |
| Zephyr | 4.4.2 (NCS v3.4.1 포함) |
| 툴체인 | NCS v3.4.1 툴체인 (Zephyr SDK 1.0.1, GCC 14.3) |
| 디버거 | 온보드 CMSIS-DAP (`NU54DK_v2`) + pyOCD 0.42, VS Code Cortex-Debug |
| 빌드 호스트 | macOS · Linux · Windows (`firmware/scripts/fw`, VS Code 태스크) |
| 시리얼 | VCOM1 = cli / 로그 / 시리얼 DFU, VCOM0 = 두 번째 채널 (115200 8N1) |
| 펌웨어 업데이트 | MCUboot + SMP — BLE 또는 cli 포트 (`fw dfu`, VS Code 태스크) |

**확인된 PC**: Ubuntu 22.04 + J-Link EDU + NU-54DK — 세부 내용은 [03_build_debug_env.md](docs/03_build_debug_env.md#linux-실측-2026-09-25-ubuntu-antonsangho-pc)

## 사용 도구

| 도구 | 용도 |
|---|---|
| [nRF Connect SDK / Toolchain Manager](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK) | SDK · 툴체인 설치 (`/opt/nordic/ncs`, `C:\ncs`, `~/ncs`) |
| [VS Code](https://code.visualstudio.com/) + [Cortex-Debug](https://github.com/Marus/cortex-debug) + C/C++ | 편집 · 빌드 태스크 · 디버깅 |
| [pyOCD](https://github.com/pyocd/pyOCD) | 다운로드 · 디버그 (툴체인에 포함) |
| [baram-term](https://github.com/chcbaram/baram-term) | 시리얼 터미널 — VCOM1 로 cli 사용. Claude Code 플러그인으로 세션에서 같은 창에 명령 전송 (`baram-ctl`) |

> **macOS 참고**: NCS v3.4.1 macOS 툴체인의 cmake 는 macOS 14 이상에서만 실행된다.
> macOS 13 이하에서는 `fw` 스크립트가 시스템 cmake(예: `brew install cmake`) 또는 설치된 다른 툴체인의 cmake 를
> 임시로 대신 쓴다 (경고 출력). 나머지 도구(gcc, gdb, ninja, pyocd)는 v3.4.1 툴체인 그대로 동작한다.
> macOS 14 이상 / Windows / Linux 에서는 해당 없음.

## 폴더

| 폴더 | 내용 |
|---|---|
| `hardware/` | 회로도 |
| `firmware/boards/` | Zephyr 보드 패키지 |
| `firmware/projects/` | 예제 프로젝트 (각 폴더를 VS Code 로 연다) |
| `firmware/scripts/` | 빌드·다운로드·디버그·업데이트 스크립트 |
| `firmware/keys/` | MCUboot 서명 키 (**개발용**. 양산에서는 교체한다) |
| `web/` | 브라우저에서 굽는 페이지 (WebUSB / Web Bluetooth / Web Serial) |
| `docs/` | 설명 문서 — [00_handoff.md](docs/00_handoff.md) 부터 |

## 예제

| 번호 | 프로젝트 | 내용 |
|---|---|---|
| 04 | `led` | LED 점멸, 빌드/다운로드/디버그 확인 |
| 05 | `uart` | UART(async DMA, 가상 채널) + CLI — 이후 예제의 기반 |
| 06 | `i2c` | I2C, `i2c scan` |
| 07 | `shtc3` | Qwiic SHTC3 온습도 센서 |
| 08 | `button` | 버튼 (인터럽트 방식), 클릭 / 길게 누름 |
| 09 | `log` | 로그 버퍼, `log boot/list` |
| 10 | `module` | ap 모듈 구조, 모듈별 스레드 (cli_mgr) |
| 11 | `power` | 리셋 원인, System OFF (버튼/GRTC 깨우기) |
| 12 | `adc` | 배터리 전압, 칩 온도 |
| 13 | `pmic` | 배터리 충전기 BQ25186 |
| 14 | `nvs` | 설정 저장 (Settings + ZMS) |
| 15 | `rtc` | 날짜·시계, 로그 타임스탬프 |
| 16 | `ble_nus` | BLE NUS — cli 를 BLE 로 |
| 18 | `dfu` | MCUboot 펌웨어 업데이트 (BLE / 시리얼) — [문서](docs/18_dfu.md) |
| 19 | `web_dfu` | 브라우저에서 굽기 — SWD(WebUSB) / BLE / 시리얼 — [문서](docs/19_web_dfu.md) |

앞으로의 계획: [docs/roadmap.md](docs/roadmap.md)

## 빠른 시작

```sh
cd firmware/projects/uart
../../scripts/fw build     # Windows: ..\..\scripts\fw.cmd build
../../scripts/fw flash
```

VS Code 로 프로젝트 폴더를 열고 `Ctrl/Cmd+Shift+B` 빌드, `F5` 디버그.
자세한 내용은 [docs/03_build_debug_env.md](docs/03_build_debug_env.md).

### 브라우저에서 굽기

<https://chcbaram.github.io/nu54v-dk/> 를 Chrome / Edge 로 연다. 설치할 것이 없다.

USB 케이블만 꽂고 `firmware/projects/<이름>/build/merged.hex` 를 고르면 된다
(MCUboot + 앱이 합쳐져 있다). 빈 보드도 되고, 디버그가 막힌 보드는 [전체 삭제] 로 되살린다.

> WebUSB 를 쓰므로 **Chrome / Edge / Opera** (데스크톱, Android) 에서만 동작한다.
> 다른 프로그램(pyOCD, VS Code 디버거)이 프로브를 쥐고 있으면 안 된다.

### 펌웨어 업데이트 (MCUboot 를 쓰는 프로젝트)

```sh
cd firmware/projects/dfu
../../scripts/fw dfu                    # 시리얼 (cli 포트, 자동 탐색)
../../scripts/fw dfu --transport ble    # BLE
```

업로드 → test → 리셋 → confirm 까지 한 번에 한다. VS Code 태스크로도 같다.

| | 굽는 것 | 경로 |
|---|---|---|
| `fw flash` | MCUboot + 앱 | SWD (프로브) |
| `fw dfu` | 앱만 | SMP (시리얼 / BLE) |

버전은 올리지 않아도 된다 (내려받기 방지가 꺼져 있다). 다만 **완전히 같은 이미지**면
test 표시가 거부되고, **실행 중 이미지가 확정 전(test)이면** 새 업로드가 막힌다 — 먼저 confirm 한다.
그 포트를 쓰는 프로그램(baram-term 등)은 먼저 닫는다. 자세한 내용은 [docs/18_dfu.md](docs/18_dfu.md).

> **알려진 문제** — 이 보드의 온보드 프로브는 **VCOM0 를 쓰면 SWD 가 죽는다**.
> 그래서 시리얼 DFU 는 VCOM0 가 아니라 cli 포트(VCOM1)에 얹었다.
> 원인 분석과 재현 조건: [docs/reports/2026-09-20_daplink_vcom0_swd.md](docs/reports/2026-09-20_daplink_vcom0_swd.md)
