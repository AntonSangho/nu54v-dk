# 15. 날짜·시계 (`firmware/projects/rtc`)

nvs 예제(14) 위에 **rtc** 모듈을 올린다. 날짜·시각·시간대를 다루고, 로그에 타임스탬프를 붙인다.
포함 모듈: 지금까지 만든 것 전부 + `rtc`

## 1. 만드는 방법

nRF54L15 에는 달력 RTC 가 없다. NU87 과 같은 방식으로 **기준 시각 + 카운터**로 만든다.

```
epoch = base_epoch + (GRTC SYSCOUNTER - base_count) / 1 000 000
```

| 항목 | 내용 |
|---|---|
| 카운터 | GRTC SYSCOUNTER (1 µs 단위, LFXO 32.768 kHz) — always-on 전원 도메인, System OFF 에서도 동작 |
| 기준값 보관 | **보존 RAM** 4 KB (보드 DTS `retainedmem0`, SRAM 마지막 4 KB). 리셋·System OFF 에도 남는다 |
| 시간대 | 보존 RAM + **nvs**(`nu54/rtc_tz`) → 전원을 껐다 켜도 시간대는 남는다 (시각은 다시 맞춰야 한다) |
| 변환 | epoch ↔ 연월일·요일은 Howard Hinnant 의 civil ↔ days 알고리즘 (윤년 포함) |

API 는 NU87 `rtc.h` 그대로 (`rtcGetInfo/SetInfo`, `rtcGetDate/SetDate`, `rtcGetTime/SetTime`,
`rtcGetEpochTime/SetEpochTime`, `rtcGetTimeZone/SetTimeZone`, `rtcIsTimeSet`, `rtcSetReg/GetReg`).
`rtcSync()` 를 추가했다 (§3).

## 2. CLI 명령

```
cli# rtc info
2026-09-20 (일) 14:16:59   UTC+9:00
epoch : 1789881419 (UTC)
base  : epoch 1789881419, count 43124
now   : count 568346 (1000000 Hz)

cli# rtc set epoch 1789881419    호스트 시각으로 맞추기 (date -u +%s)
cli# rtc set date 2026 9 20
cli# rtc set time 14 16 59
cli# rtc tz 9                    시간대 (시 단위) — nvs 에도 저장
cli# rtc show                    1초마다 표시
```

## 3. 리셋·System OFF 에서 시각 유지 (실측)

| 상황 | GRTC 카운터 | 시계 |
|---|---|---|
| System OFF (10 초, GRTC 로 깨움) | **이어짐** (15.65 s → 26.35 s) | 정확 (호스트와 1 초 차) |
| 소프트 리셋 (`reset run`) | **0 부터 다시 시작** | 보존 RAM 덕분에 유지 (1 초 차) |
| 전원 완전 차단 | 0 부터 | 시각 사라짐 (`rtcIsTimeSet()` false), 시간대만 nvs 에 남음 |

카운터가 되감기는 경우에 대비해 `rtcSync()` 가 현재 시각을 보존 RAM 에 다시 적는다. 부르는 곳은 세 군데다.

- 60 초마다 (`RTC_SYNC_PERIOD_MS`)
- `resetToReset()` — 리셋 직전
- `powerOff()` — System OFF 직전

부팅할 때 카운터가 기준보다 작으면 되감긴 것으로 보고 마지막으로 적어 둔 시각에서 이어간다.
그래서 리셋에 걸린 시간(수백 ms)과 마지막 저장 이후의 시간만큼 뒤처진다.

### 소프트 리셋에서 카운터가 유지되지 않는 것

데이터시트와 Nordic 의 설명은 "유지된다" 이다.

> All GRTC registers are reset during wakeup from System OFF mode. However, the
> SYSCOUNTER[m].SYSCOUNTERL and SYSCOUNTER[m].SYSCOUNTERH registers are restored automatically
> on wakeup from System OFF mode and **after soft reset**.
> — nRF54L15 데이터시트 8.10 GRTC (p.296)

[DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/116971/reset-surviving-system-clock-on-nrf54) 도 같은 설명이고,
Zephyr 에는 반대로 "리셋해도 uptime 이 0 이 안 된다"는 이슈([#89147](https://github.com/zephyrproject-rtos/zephyr/issues/89147))가 있어
드라이버가 부팅 시 카운터를 `grtc_start_value` 로 기억한다. 즉 상류에서는 이어지는 것이 정상이다.

그런데 이 보드에서는 `reset run`(=`NVIC_SystemReset()`) 뒤 카운터가 0.5 초부터 시작한다. 확인한 것:

- nrfx/Zephyr 코드에 SYSCOUNTER 를 지우는 곳이 없다 (`nrfx_grtc_init`, `nrfx_grtc_syscounter_start` 모두 CLEAR 태스크를 부르지 않음)
- `z_nrf_grtc_timer_read()` 는 시작값을 빼지 않은 **원시 카운터**를 준다
- **Zephyr 버전 차이가 아니다** : NCS v3.3.0(Zephyr 4.3.99)과 v3.4.1(Zephyr 4.4.2)의 `nrf_grtc_timer.c` 가 완전히 동일하다.
  SoC 초기화와 MDK `SystemInit` 에도 GRTC 를 건드리는 곳이 없다
- 데이터시트가 말하는 "내부 저주파 타이머"(`TASKS_START`)를 켜 봤다 — rtcInit, 커널보다 먼저인 보드 초기화,
  그리고 `STATUS.LFTIMER.READY` 를 확인한 뒤(데이터시트 8.10.6 의 조건) — **세 경우 모두 결과가 같았다**
- 디버거 연결 여부와도 무관했다

부팅 직후 레지스터 값 (`MODE` 0x510, `STATUS.LFTIMER` 0x6B0, `CLKCFG` 0x718) 은 정상이다.

```
MODE          : 0x00000003 (AUTOEN 1, SYSCOUNTEREN 1)
STATUS.LFTIMER: 0x00000001 (READY 1)
CLKCFG        : 0x00000001
```

남은 가설은 "Zephyr 초기화가 `MODE.SYSCOUNTEREN` 을 잠깐 껐다 켜는데 그때 값이 날아간다" 이다.
확인하려고 CLI 에서 껐다 켜 봤더니 **보드가 멈췄다** — 커널 시계가 이 카운터를 쓰기 때문이다. 그래서 이 방법으로는 확인할 수 없다.
(`nrfx_grtc_init()` 안의 `nrfy_grtc_sys_counter_set(NRF_GRTC, false)`)

원인은 아직 모른다. 보존 RAM 방식으로 시계가 정상 동작하므로 여기서 멈춘다.
더 파려면 Nordic 에 문의하는 편이 빠르다 — 재현은 `rtc info` 의 `now` 값을 `reset run` 전후로 비교하면 된다.

> ⚠ `TASKS_START` 를 커널 시작 전에 거는 것은 위험하다. 데이터시트 8.10.1 : "The clock source cannot be
> changed after GRTC is started" — Zephyr 가 LFXO 를 고르기 전에 시작해 버리면 클럭 소스 선택을 막는다.

## 4. 로그 타임스탬프

`log.c` 의 `logBufHeader()` 한 곳에서 만든다 (로그가 생긴 순간의 시각).

```
0007 [    0.0xx]	[OK] buttonInit()      시각 미설정 → 부팅 후 초.밀리초
0010 [14:16:59]	[OK] rtcInit()           시각 설정됨 → 시:분:초
```

## 5. 저전력

| 항목 | 내용 |
|---|---|
| 틱 없음 | 1초 인터럽트를 쓰지 않는다. 시각은 물어볼 때 계산한다 |
| 주기 저장 | 60 초마다 보존 RAM 에 쓰기 (RAM 쓰기 + CPU 수 µs). 더 줄이려면 주기를 늘리고 리셋·OFF 직전 저장에 기댄다 |
| System OFF | 카운터가 계속 돌아 시계가 맞다. GRTC 는 always-on 도메인이고 LFXO 로 돈다 |
| 정확도 | LFXO(외부 캡) 오차 ±20 ppm → 하루 약 ±2 초. Arduino 코어 실측은 +25~38 ppm |

## 6. 검증 결과 (2026-09-20, NCS v3.4.1, macOS)

- [x] 빌드: FLASH 95 KB / RAM 25 KB (앱 RAM 은 보존 RAM 4 KB 를 뺀 252 KB)
- [x] `rtc set epoch` 로 호스트 시각과 일치, 요일 표시
- [x] 리셋 후 시각 유지 (1 초 차)
- [x] System OFF 10 초 후 시각 유지 (1 초 차, GRTC 깨우기)
- [x] 시간대 nvs 저장, 로그 타임스탬프
- [ ] 장시간 오차 측정 (하루 단위)
