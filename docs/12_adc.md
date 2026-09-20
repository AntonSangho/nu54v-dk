# 12. ADC · 칩 온도 (`firmware/projects/adc`)

power 예제(11) 위에 **adc** 모듈(배터리 전압)과 **temp** 모듈(칩 내부 온도)을 올린다.

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
| 칩 온도 | `&temp` (nordic,nrf-temp), Zephyr sensor API `SENSOR_CHAN_DIE_TEMP` |

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

## 4. 측정값이 흔들리는 것

읽을 때마다 **4.04 ~ 4.13 V** 사이를 오간다. 평균 횟수를 늘리거나 첫 샘플을 버려도 줄지 않는다.
같은 보드의 Arduino 코어 기록도 `ADC 4.05~4.16 V` 로 같은 범위이고, 그때 충전기는 "CV, 목표 4.20 V" 였다.
→ ADC 잡음이 아니라 **충전기(BQ25186)가 CV 구간에서 동작하며 생기는 배터리 전압 변동**으로 본다.
정확한 배터리 잔량이 필요하면 충전 상태(13 pmic)와 함께 봐야 한다.

## 5. 저전력

| 항목 | 내용 |
|---|---|
| SAADC | 읽을 때만 동작한다 (Zephyr 드라이버가 변환 뒤 끈다) |
| 분압기 | 470K/1M 은 상시 누설 — 4.1 V 에서 약 2.8 µA. 스위치가 없어 끌 수 없다 |
| 온도 | 읽을 때만 측정. 다이 온도라 동작 중에는 주변보다 높게 나온다 |
| 평균 | 16회 + 하드웨어 16회 오버샘플링 = 한 번 읽는 데 약 17회 변환. 자주 읽지 않는다 |

## 6. 검증 결과 (2026-09-20, NCS v3.4.1, macOS)

- [x] 빌드: FLASH 73 KB / RAM 24.8 KB
- [x] `adc info` : VBAT 약 4.09 V (배터리 연결 상태)
- [x] `temp info` : 33.75 ℃
- [ ] 무부하/방전 상태에서 배터리 전압 교차 확인 (13 pmic 의 충전 상태와 함께)
