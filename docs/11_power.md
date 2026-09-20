# 11. 전원 (`firmware/projects/power`)

앞 단계(10 module) 위에 **reset** 모듈(리셋 원인)과 **power** 모듈(System OFF, 전원 모드)을 올린다.
포함 모듈: `led uart cli i2c shtc3 button log reset power` + ap 모듈 구조
소비전류 측정은 뒤로 미루고(§5), 측정 없이 확인할 수 있는 동작을 먼저 검증했다.

## 1. 기능

| 모듈 | 내용 |
|---|---|
| reset (`hw/driver/reset.c`) | 부팅할 때 리셋 원인을 읽고 바로 지운다 (RESETREAS 는 지우기 전까지 누적). NU87 `RESET_BIT_xxx` 구조 + System OFF 깨어남 원인 |
| power (`hw/driver/power.c`) | `powerOff(wake_ms)` : System OFF. 버튼 SW1~4 와 (선택) GRTC 로 깨어남. `powerIsDcdc()` : 레귤레이터 모드 |

리셋 원인 (`reset info`, 부팅 로그):

| 비트 | 뜻 | hwinfo |
|---|---|---|
| `RESET_BIT_POWER` | 전원 인가 (RESETREAS 비어 있음) / 브라운아웃 | `RESET_POR`, `RESET_BROWNOUT` |
| `RESET_BIT_PIN` | nRESET 핀 (SW6, 디버거 하드 리셋) | `RESET_PIN` |
| `RESET_BIT_WDG` | 워치독 | `RESET_WATCHDOG` |
| `RESET_BIT_SOFT` | 소프트 리셋 (`reset run`, 다운로드 후 리셋) | `RESET_SOFTWARE` |
| `RESET_BIT_WAKE_GPIO` | System OFF 에서 버튼으로 깨어남 | `RESET_LOW_POWER_WAKE` |
| `RESET_BIT_WAKE_TIMER` | System OFF 에서 GRTC 로 깨어남 | `RESET_CLOCK` |
| `RESET_BIT_DEBUG` | 디버거 | `RESET_DEBUG` |
| `RESET_BIT_ETC` | lockup 등 | |

## 2. CLI 명령

```
cli# power info              레귤레이터 모드 (LDO / DC/DC), uptime
cli# power off               System OFF, 버튼으로 깨어남
cli# power off 3000          System OFF, 버튼 또는 3초 뒤 깨어남
cli# reset info              이번 부팅의 리셋 원인
cli# reset run               소프트 리셋
```

## 3. System OFF 순서

```
버튼 핀 → GPIO_INT_LEVEL_ACTIVE (SENSE)     깨우기 핀 (보드 DTS sense-edge-mask 핀)
ledToSleep()                                LED 끄고 핀 분리
uartClose(모든 채널) + delay(10)            UART RX 정지 → UARTE suspend
z_nrf_grtc_wakeup_prepare(wake_ms)          (wake_ms > 0) 다른 GRTC 채널을 모두 끄므로 이 뒤에 커널 타이머 사용 금지
sys_poweroff()                              RAM 리텐션 해제, RESETREAS 지움, System OFF (Zephyr 가 처리)
```

깨어나면 리셋으로 처음부터 부팅한다. RAM 은 남지 않는다 (필요하면 retained RAM — 로드맵 15 rtc).

## 4. ⚠ System OFF 시험 절차 — 디버거를 떼고 전원을 다시 넣는다

**SWD 디버거가 붙어 있으면 System OFF 에서 깨어나지 않는다.**
디버그 모드에서는 nrfx `nrf_regulators_system_off()` 가 흉내 코드(`while(1){__WFE();}`)로 빠진다
(baram-nrf54-arduino CLAUDE.md F8 과 같은 현상). GRTC 로도, 버튼으로도 깨어나지 않아 "System OFF 가 안 된다"고 착각하기 쉽다.

- 한 번 디버그 모드에 들어가면 **전원을 다시 넣기 전까지** 유지된다. 핀 리셋(SW6)으로는 풀리지 않는다.
- 이 보드의 온보드 CMSIS-DAP 은 연결되어 있기만 해도 디버그 모드를 건다 (전원 재인가 직후에도 실패).

절차:

1. 펌웨어 다운로드 (`fw flash`, SWD 필요 → DAP 스위치 SW1 `DISABLE_SWD` **OFF**)
2. SW1 `DISABLE_SWD` **ON** (SWD 레벨시프터 분리. `DISABLE_UART` 는 OFF 로 둬야 cli 사용 가능)
3. **USB 를 뽑았다 다시 꽂는다** (전원 재인가 → 디버그 모드 해제)
4. cli 로 `power off …` 시험
5. 다시 다운로드하려면 1 부터 (`DISABLE_SWD` OFF)

`DISABLE_SWD` 가 ON 이면 pyOCD 는 `SWD/JTAG communication failure (No ACK)` 로 붙지 못한다 (분리 확인 방법).

## 5. 소비전류 (측정 예정)

J1 점퍼를 빼고 전류계(PPK2)로 VDD_MOD 를 잰다. 위 §4 처럼 SWD 를 분리하고 잰다.
Qwiic 센서, Qwiic 풀업, LED 버퍼(U8/U9), 리셋 풀업도 VDD_MOD 에 물려 있으므로 µA 단위는 Qwiic 을 빼고 잰다.

| 상태 | 조건 | 전류 |
|---|---|---|
| System OFF | 버튼 깨우기만 | 측정 예정 |
| System OFF | + GRTC 깨우기 (LFXO) | 측정 예정 |
| System ON idle | cli RX 켜짐 (UARTE 동작) | 측정 예정 |
| System ON idle | cli RX 꺼짐 (`uart close`) | 측정 예정 |
| DC/DC 켬 | 위와 비교 | 모듈 인덕터 확인 후 |

현재 레귤레이터는 **LDO** (`power info`). DC/DC 는 모듈의 인덕터 실장이 확인되면 켠다 (02_board_package).

## 6. 검증 결과 (2026-09-20, NCS v3.4.1, macOS)

- [x] 빌드: FLASH 65 KB / RAM 24.4 KB
- [x] 리셋 원인: 다운로드 후 `SOFT`(+`DEBUG`), SW6/하드 리셋 `PIN`, `reset run` → `SOFT`, USB 재인가 → `POWER`
- [x] `power info` : LDO
- [x] `power off 3000` → 3초 뒤 깨어남, `RESET_BIT_WAKE_TIMER` (SWD 분리 + 전원 재인가 상태)
- [x] `power off` → 버튼으로 깨어남, `RESET_BIT_WAKE_GPIO`
- [ ] 소비전류 (§5)
