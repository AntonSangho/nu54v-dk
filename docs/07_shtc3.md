# 07. SHTC3 온습도 센서 (`firmware/projects/shtc3`)

i2c 예제(06) 위에 Sensirion **SHTC3** 드라이버를 추가한다. Qwiic 커넥터(J5)에 연결.
드라이버 모듈에 CLI 명령을 넣어, ap 코드 없이 cli 에서 먼저 시험한다.

## 1. 하드웨어

| 항목 | 내용 |
|---|---|
| 버스 | `_DEF_I2C1` (i2c21, Qwiic J5) |
| 주소 / ID | `0x70` / `0x0887` (하위 비트 `0x0807` 확인) |
| 설정 | `hw_def.h` : `_USE_HW_SHTC3`, `HW_SHTC3_I2C_CH`, `HW_SHTC3_I2C_ADDR` |

## 2. CLI 명령

```
cli# shtc3 info
is_init   : True
i2c ch    : 1, addr 0x70
low power : Off
id        : 0x0887

cli# shtc3 read
T : 27.85 C, RH : 58.54 %
16 ms                             ← Normal 측정 (wakeup + 12.1 ms 변환 + 읽기)

cli# shtc3 read 500               500 ms 주기 반복, 아무 키로 종료 (측정 사이 sleep)
cli# shtc3 lowpower on            Low power 측정 (측정 5 ms)
cli# shtc3 init                   다시 초기화
```

`i2c scan 1` 에 SHTC3(0x70)이 보이지 않는 것은 정상이다. SHTC3 는 sleep 상태에서 주소에 응답하지 않는다 (먼저 Wakeup 명령이 필요).

## 3. 드라이버 (`hw/driver/shtc3.c`, 새로 작성)

API : `shtc3Init / shtc3IsInit / shtc3GetID / shtc3SetLowPower / shtc3Read(shtc3_info_t *)`

```
Wakeup(0x3517) → 240 us 대기 → 측정 명령(0x7866 / LP 0x609C) → 13 ms(LP 1 ms) sleep
→ 6바이트 읽기(CRC 확인) → Sleep(0xB098)
```

- 온도 `T = -45 + 175 × raw / 65536`, 습도 `RH = 100 × raw / 65536`
- 부동소수점 없이 0.01 단위 정수(`int16_t`)로 계산 (`shtc3_info_t.temp/humi`)
- CRC-8: poly 0x31, init 0xFF
- 클럭 스트레칭 없는 명령 사용 → 변환 중에 버스를 붙잡지 않는다
- CLI 는 `#if CLI_USE(HW_SHTC3)` 로 넣고 `shtc3Init()` 에서 `cliAdd` (다른 모듈과 같은 방식)

## 4. 저전력

| 항목 | 내용 |
|---|---|
| 센서 | 측정할 때만 깨우고 곧바로 Sleep 명령 → 대기 전류 0.3 µA (typ) |
| 측정 대기 | 클럭 스트레칭 대신 명령 후 `delay()` → 변환하는 동안 CPU 도 sleep |
| Low power 측정 | 변환 0.8 ms (정확도/반복성은 약간 낮아짐) |
| 반복 측정 | 주기 사이 `uartWaitRx()` 로 sleep (키 입력 시 바로 종료) |

## 5. 검증 결과 (2026-09-20, macOS)

- [x] 빌드: FLASH 64 KB / RAM 13.7 KB (NCS v3.4.1)
- [x] NCS v3.4.1 (Zephyr 4.4.2) 에서 `uart info` / `i2c scan` / `shtc3 read` / GDB 브레이크포인트 확인 (baram-term)
- [x] `shtc3 info` ID 0x0887, `shtc3 read` 약 27.9 ℃ / 58.5 %RH
- [x] 반복 측정 / Low power 모드 (측정 16 ms → 5 ms)
- [ ] 소비전류 측정 (power 예제 이후)
