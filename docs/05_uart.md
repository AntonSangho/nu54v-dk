# 05. UART + CLI (`firmware/projects/uart`)

이후 모든 예제의 공통 기반. VCOM 두 채널을 UART 모듈로 열고, cli 로 명령을 주고받는다 (baram-term 시리얼 연결).

## 1. 하드웨어

| 채널 | UARTE | 핀 | 용도 |
|---|---|---|---|
| `_DEF_UART1` = `HW_UART_CH_LOG` | uart20 (VCOM1) | P1.04 TX, P1.05 RX (RTS/CTS 미사용) | cli, 부팅 로그(printk) |
| `_DEF_UART2` = `HW_UART_CH_VCOM0` | uart30 (VCOM0) | P0.00 TX, P0.01 RX | 시험용 두 번째 채널 (`app.overlay` 로 활성) |

macOS 에서는 `/dev/cu.usbmodem*` 두 개로 보인다 (이 PC: `…1412304` = VCOM1, `…1412302` = VCOM0).

## 2. CLI 명령

cli 는 baram-term 으로 VCOM1 에 연결해 쓴다. Claude 세션에서는 baram-term 스킬로 같은 창에 명령을 보낸다
(`baram-ctl --match NU54DK_v2 send "uart info" --until 'cli# $'`). baram-term 이 연 포트를 다른 도구로 동시에 열지 않는다.


```
cli# help               명령 목록 (HELP, MD, LED, UART)
cli# uart info          채널별 보레이트, open 상태, rx/tx 바이트, 수신 큐
cli# uart test 2        VCOM0 로 받은 바이트를 16진으로 출력하고 그대로 돌려보냄 (q 로 종료)
cli# uart open 2 115200 채널 열기 (RX 시작)
cli# uart close 2       채널 닫기 (RX 정지 → UARTE suspend). cli 포트는 닫지 않음
cli# led toggle 1 500   LED1 500ms 토글 (아무 키로 종료)
cli# md 0x0 32          메모리 덤프
```

## 3. 소스 구조

| 파일 | 출처 | 내용 |
|---|---|---|
| `common/hw/src/cli.c`, `common/hw/include/cli.h` | NU87-TinyDK | cli (명령 등록, 히스토리, 인자 파싱, `cliLoopIdle` 훅) |
| `common/core/qbuffer.c/h` | nu54dk (NU87 과 동일) | 링 버퍼 |
| `common/hw/include/uart.h` | NU87-TinyDK | `uart_driver_t` 가상 채널 구조 + `uartWaitRx` 추가 |
| `hw/driver/uart.c` | NU87 구조 + 새로 작성 | Zephyr UARTE async API |
| `ap/ap.c` | | `cliOpen` → `cliMain` 루프 |

레퍼런스 저장소 두 곳을 참조했다.
- [nu54dk](https://github.com/chcbaram/nu54dk) `firmware/nu54l15-fw` : 이전 nRF54L 보드 (Zephyr)
- [NU87-TinyDK](https://github.com/chcbaram/NU87-TinyDK) `firmware/nu87-fw` : 더 최신의 uart/cli 구조 (RTL8720DF)

### uart 모듈 구조 (NU87 방식)

```
uartOpen/Read/Write/Available(ch)
   │
   ├─ uart_tbl[ch].p_driver != NULL  → 등록된 가상 채널 드라이버 (uartSetDriver)  예) BLE NUS, 텔넷
   └─ uart_tbl[ch].p_hw     != NULL  → 하드웨어 채널 (uart_hw[] 테이블: ch ↔ UARTE 장치)
```

- 하드웨어 채널은 `uart_hw[]` 에 `{ .ch, .h_dev }` 로 등록한다. `hw_def.h` 에 `HW_UART_CH_VCOM0` 이 있을 때만 uart30 이 들어간다.
- BLE NUS(로드맵 17) 는 `uart_driver_t` 를 구현해 `uartSetDriver(HW_UART_CH_BLE, …)` 로 붙인다.

### 수신 방식 선택 (console 서브시스템 → async API)

nu54dk 의 uart.c 는 Zephyr console 서브시스템(`console_read`) + 수신 스레드였다. 전력과 구조를 비교해 **async(DMA) API 로 직접 처리**하도록 바꿨다.

| | console 서브시스템 (nu54dk) | async API (현재) |
|---|---|---|
| 수신 | 인터럽트, RX 항상 켜짐 | DMA 이중 버퍼, HW frame timeout(1 ms) 으로 전달 |
| 스레드 | 수신 스레드 + 이중 버퍼링 | 없음 (콜백에서 qbuffer 로 바로) |
| 송신 | 링버퍼 + 인터럽트 | DMA, 끝날 때까지 호출 스레드 sleep |
| UARTE 끄기 | 불가 | `uartClose()` → RX 정지 → PM runtime 이 UARTE suspend |
| RAM | 18.4 KB (i2c+shtc3 기준) | 14.7 KB |

- 송신 중 fault 처럼 인터럽트를 못 쓰는 곳은 `printk`(poll out) 를 쓴다. NU87 은 이 때문에 TX 를 폴링으로 뒀다.
- 보레이트를 바꿔 다시 열 때는 RX 정지(비동기)를 `RX_DISABLED` 이벤트까지 기다린 뒤 설정한다.
- 에러로 RX 가 멈추면(`RX_DISABLED` 인데 채널이 열린 상태) 자동으로 다시 시작한다.

### 레퍼런스와 다른 점

| 항목 | 내용 |
|---|---|
| `uartWaitRx(ch, timeout_ms)` 추가 | 수신까지 폴링 없이 sleep. cli 루프, `uart test`, 센서 반복 측정에서 사용 |
| `uart open/close` CLI 추가 | 채널을 끄고 켜며 전류 비교 |
| `uart test` | 받은 바이트를 되돌려 보내 TX 도 확인, 종료 키는 명령줄에 남기지 않음 |
| `CLI_USE()` | NU87 은 매크로 안에서 `defined()` 사용 → Zephyr 빌드에서 `-Wexpansion-to-defined` 경고. `#ifdef _USE_HW_CLI` 로 나눠 같은 의미로 정의 |
| `__WEAK` | `bsp.h` 에서 Zephyr `__weak` 로 정의 (cli.c 의 `cliLoopIdle`) |

## 4. 저전력

| 항목 | 내용 |
|---|---|
| cli 루프 | `cliMain()` 후 `uartWaitRx(ch, 1000)` → 입력이 없으면 sleep (nu54dk 는 5 ms 마다 깨어남) |
| RX 켜진 동안 | UARTE + 클럭 동작 → 대기 전류 증가. **power 단계(11)에서 측정** |
| 개선 예정 | 입력이 한동안 없으면 RX 를 끄고, RX 핀 GPIO 인터럽트로 다시 켜기 (첫 글자는 버려짐) |

## 5. 검증 결과 (2026-09-20, NCS v3.3.0, macOS)

- [x] 빌드: FLASH 55 KB / RAM 16 KB
- [x] cli 명령 (`help`, `uart info`) VCOM1
- [x] `uart test 2`: VCOM0 로 보낸 `AB\r\n` 수신 및 되돌림 확인 → VCOM0 RX/TX 모두 정상
- [x] `uart close 2` / `uart open 2 115200`, cli 포트 닫기 거부
- [ ] 채널 열림/닫힘 전류 비교
