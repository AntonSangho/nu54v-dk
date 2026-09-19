# NU54-DK

NUCODE NU54-DK (nRF54L15) 보드 브링업 펌웨어.

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
| 시리얼 | VCOM1 = cli / 로그, VCOM0 = 두 번째 채널 (115200 8N1) |

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
| `firmware/scripts/` | 빌드·다운로드·디버그 스크립트 |
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

앞으로의 계획: [docs/roadmap.md](docs/roadmap.md)

## 빠른 시작

```sh
cd firmware/projects/uart
../../scripts/fw build     # Windows: ..\..\scripts\fw.cmd build
../../scripts/fw flash
```

VS Code 로 프로젝트 폴더를 열고 `Ctrl/Cmd+Shift+B` 빌드, `F5` 디버그.
자세한 내용은 [docs/03_build_debug_env.md](docs/03_build_debug_env.md).
