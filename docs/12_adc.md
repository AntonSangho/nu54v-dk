# 12. ADC · 칩 온도 (`firmware/projects/adc`)

power 예제(11) 위에 **adc** 모듈(배터리 전압)과 **temp** 모듈(칩 내부 온도)을 올린다.
여기서 06 i2c / 07 shtc3 모듈도 다시 합쳐, 지금까지 만든 모듈이 모두 들어 있다
(`led uart cli log button reset power i2c shtc3 adc temp module`).

## 1. 하드웨어

```
VBAT ─ R8 470K ─┬─ P1.12 (AIN5)
                └─ R11 1M ─ GND
```

| 항목 | 내용 |
|---|---|
| 채널 | `ADC_VBAT` : AIN5 (P1.12), 솔더 브리지 SB4 |
| 분압비 | 1M / (470K + 1M) = 0.680 → **배터리 전압 = 읽은 값 × 1.470** |
| 설정 | gain 1/4, 내부 기준 0.9 V → 풀스케일 3.6 V (입력 최대 4.2 × 0.68 = 2.86 V) |
| 획득 시간 | **40 µs** (nRF54L SAADC 최대). 분압기 출력 임피던스가 320 kΩ 이라 기본 10 µs 로는 낮게 읽힌다 |
| 오버샘플링 | DTS `zephyr,oversampling = <4>` (하드웨어 16회) + 소프트웨어 평균 16회 |
| 칩 온도 | **nRF54L15 칩 내부** TEMP (`&temp`, SoC dtsi 의 `temp@d7000`), Zephyr sensor API `SENSOR_CHAN_DIE_TEMP` |

칩 내부 TEMP 는 다이 온도라 주변 온도보다 높게 나온다 (실측 34 ℃ vs SHTC3 28 ℃).
주변 온도가 필요하면 Qwiic 의 SHTC3(07)를, 칩 발열/온도 보정이 필요하면 내부 TEMP 를 쓴다. 보드에는 별도 온도 센서 부품이 없다.

채널 정의는 보드 DTS(`zephyr,user` io-channels + `&adc channel@5`)에 있고, 프로젝트 `app.overlay` 가 `&adc` 를 켠다.

## 2. CLI 명령

```
cli# adc info
adc init : 1
00. ADC_VBAT     :  3164 raw,  4086 mV (x1.470)

cli# adc show          계속 표시 (아무 키로 종료)
cli# temp info
is_init : True
temp : 33.75 C
cli# temp show         계속 표시
```

## 3. 소스

| 파일 | 내용 |
|---|---|
| `hw/driver/adc.c` | nrf54l15-bd `adc.c` 구조. `adcInit/adcRead/adcRead8·10·12·16/adcReadVoltage` |
| `hw/driver/temp.c` | 새로 작성. `tempInit/tempRead` (0.01 ℃ 단위 `int16_t`) |

### 레퍼런스(nrf54l15-bd)와 다른 점

| 항목 | 내용 |
|---|---|
| lock | FreeRTOS(`xSemaphoreTake`) → Zephyr `k_mutex` |
| 전압 환산 | 0.9 V / gain 을 코드에 적던 것을 `adc_raw_to_millivolts_dt()` 로 (DTS 설정을 그대로 따름) |
| 분압 | 채널 테이블에 `scale`(×1000) 추가 → `adcReadMilliVolt()` 가 분압 전 전압을 준다 |
| 평균 | `adcReadAverage(ch, count)` 추가. 첫 변환은 버린다 |
| 정수 API | 부동소수점 출력을 피하려고 `adcReadMilliVolt/adcConvMilliVolt` 추가 (`adcReadVoltage` 는 그대로 둠) |

## 4. 배터리를 연결하지 않으면 값이 흔들린다

| 상태 | 읽은 값 |
|---|---|
| 배터리 없음 | 4.04 ~ 4.13 V, 읽을 때마다 ±50 mV 흔들림 (평균 횟수를 늘려도 그대로) |
| 배터리 연결 | **3.896 ~ 3.898 V, ±2 mV** |

배터리가 없으면 분압기가 보는 것은 배터리가 아니라 **충전기(BQ25186)의 BAT 노드**다.
부하도 배터리도 없는 상태라 전압이 뜨고 흔들린다. 값이 이상하면 먼저 배터리 연결을 확인한다.

배터리를 연결하면 분압기 임피던스(320 kΩ)에도 흔들림이 거의 없다 — 40 µs 획득 시간과 오버샘플링으로 충분하다.

## 5. 저전력

| 항목 | 내용 |
|---|---|
| SAADC | 읽을 때만 동작한다 (Zephyr 드라이버가 변환 뒤 끈다) |
| 분압기 | 470K/1M 은 상시 누설 — 4.1 V 에서 약 2.8 µA. 스위치가 없어 끌 수 없다 |
| 온도 | 읽을 때만 측정. 다이 온도라 동작 중에는 주변보다 높게 나온다 |
| 평균 | 16회 + 하드웨어 16회 오버샘플링 = 한 번 읽는 데 약 17회 변환. 자주 읽지 않는다 |

## 6. 검증 결과 (2026-09-20, NCS v3.4.1, macOS)

- [x] 빌드: FLASH 73 KB / RAM 24.8 KB
- [x] `adc info` : 배터리 연결 시 3.898 V (±2 mV), 배터리 없을 때 4.04~4.13 V (§4)
- [x] `temp info` : 33.75 ~ 34.25 ℃ (같은 시각 SHTC3 는 28.3 ℃)
- [x] i2c / shtc3 합친 뒤에도 전체 동작 (`help` 명령 13개, `i2c scan`, `shtc3 read`, `adc info`, `temp info`)
- [ ] 충전 상태와 교차 확인 (13 pmic : 충전 중/완료에 따른 전압 변화)
