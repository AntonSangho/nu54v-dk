# 09. 로그 (`firmware/projects/log`)

button 예제(08) 위에 log 모듈을 올린다. `logPrintf()` 출력을 로그 채널(VCOM1)로 내보내면서 RAM 버퍼에도 남겨,
연결이 늦었거나 지나간 부팅 메시지를 cli 로 다시 볼 수 있다.

## 1. 구성

| 항목 | 내용 |
|---|---|
| 채널 | `HW_LOG_CH = HW_UART_CH_LOG` (uart20, VCOM1 — cli 와 같은 포트) |
| boot 버퍼 | 2 KB. `logBoot(false)` 전까지(=`hwInit()` 끝까지)의 로그만 |
| list 버퍼 | 4 KB 링 버퍼. 모든 로그, 가득 차면 가장 오래된 것부터 덮어씀 |
| 줄 머리 | `%04X\t` 줄 번호 (`logBufHeader()` 한 곳에서 만듦 → rtc 이후 타임스탬프 추가) |

`hwInit()` 순서 (NU87 과 같음):

```
cliInit → logInit → ledInit → uartInit → uartOpen
→ logOpen(HW_LOG_CH) → 부팅 메시지 → 나머지 모듈 init → logBoot(false)
```

`logOpen()` 전의 로그는 버퍼에만 남고, 이후 로그는 UART 와 버퍼 양쪽에 간다.

## 2. CLI 명령

```
cli# log info        버퍼 줄 수 / 길이
cli# log boot        부팅 로그 다시 보기
0000
[ Firmware Begin... ]
0001	Booting..Name 		: NU54-DK-LOG
...
0007	[OK] buttonInit()
cli# log list        전체 로그 다시 보기 (항상 오래된 것 → 최신 순, 최신이 마지막)
```

버퍼가 한 바퀴 돌면 덮어써서 잘린 맨 앞 줄은 건너뛰고, 온전한 가장 오래된 줄부터 출력한다.
(list 버퍼를 200 바이트로 줄여 확인: `0002 … 0007` 순서로 출력)

## 3. 레퍼런스와 다른 점

기반은 nu54dk `log.c` (Zephyr `k_mutex`). API 는 NU87 `log.h` (`logIsOpen` 포함).

| 항목 | 내용 |
|---|---|
| 초기화 전 호출 | NU87 은 lock 을 잡은 뒤 `is_init` 을 봐서, 초기화 전에 불리면 lock 을 쥔 채 return. nu54dk 처럼 확인 후 lock |
| ISR 에서 호출 | mutex 와 DMA 송신(`uartWrite`)은 ISR 에서 쓸 수 없다 → `k_is_in_isr()` 이면 `printk`(poll out) 로만 출력 (버퍼에는 안 남음) |
| 긴 문자열 | `vsnprintf` 반환값이 버퍼(256)를 넘으면 버퍼 밖을 보내던 것을 잘라서 보냄 |
| 버퍼 | 레퍼런스는 가득 차면 index 를 0 으로 돌려 덮어써서 `log list` 에 최신 로그가 중간에 섞였다 → 링 버퍼로 바꾸고 오래된 것 → 최신 순으로 출력 (`logBufDump`) |
| `bsp.c` 의 `logPrintf` | `__weak` 기본 구현(printk). log 모듈이 있으면 log.c 가 우선 |

Zephyr 커널 메시지(부팅 배너, fault)는 여전히 `printk` → UART 콘솔로 나간다.

## 4. 저전력

| 항목 | 내용 |
|---|---|
| 끄기 | `logDisable()` : UART 로 내보내지 않고 버퍼에만 남긴다 (송신할 때만 UARTE 가 켜지므로 전류 절약) |
| 버퍼 | RAM 6 KB 사용 (256 KB 중). 전원과는 무관 |

## 5. 검증 결과 (2026-09-20, NCS v3.4.1, macOS)

- [x] 빌드: FLASH 60 KB / RAM 20.2 KB
- [x] 부팅 메시지 출력, `log info` / `log boot` / `log list` (baram-term)
- [x] 링 버퍼가 넘친 경우 순서 (list 200 바이트로 임시 시험)
