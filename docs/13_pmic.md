# 13. PMIC — 배터리 충전기 (`firmware/projects/pmic`)

adc 예제(12) 위에 **pmic** 모듈을 올린다. 보드의 TI **BQ25186** (1셀 리니어 충전기 + 파워패스) 상태를 읽고,
충전 전류를 배터리에 맞게 설정한다.
포함 모듈: `led uart cli i2c shtc3 button log reset power adc temp pmic` + ap 모듈 구조

## 1. 하드웨어

| 신호 | 연결 | 비고 |
|---|---|---|
| I2C | `i2c21` 0x6A (Qwiic 과 같은 버스) | 장치 ID 0x41 |
| `/INT` | P1.11 (SB1) | open drain + 10K 풀업. 상태가 바뀌면 Low |
| `/PG` | P2.08 (SB2) | 입력 전원 있음 |
| `/CE` | P2.10 (SB3) | R10 10K 풀다운 → **띄워 두면 충전 허용** |
| VBAT_MON | P1.12 (SB4) | 전압은 adc 모듈에서 (12_adc) |

핀은 보드 DTS 의 `zephyr,user` 에 `pmic-int-gpios` / `pmic-pg-gpios` / `pmic-ce-gpios` 로 정의했다.

## 2. CLI 명령

```
cli# pmic info
id      : 0x41 (i2c ch1, addr 0x6A)
state   : charging - CC
input   : VIN good, /PG pin active
setting : target 4.20 V, charge 150 mA, input limit 500 mA
health  : ok
raw     : STAT0 21 STAT1 00  int 0회

cli# pmic show          1초마다 계속 (아무 키로 종료)
cli# pmic reg           레지스터 0x00~0x0C 덤프
cli# pmic ichg 150      충전 전류 설정 (최대 HW_PMIC_ICHG_MAX_MA)
cli# pmic ce off        /CE 핀으로 충전 정지 → state "done 또는 충전 금지"
cli# pmic ce on         다시 허용
```

## 3. 쓰는 것은 충전 전류 하나뿐

목표 전압·안전 타이머·VSYS·입력 전류 제한은 **건드리지 않는다**. 잘못 쓰면 충전이 멎거나 배터리에 무리가 간다.
바꾸는 것은 `ICHG_CTRL` 의 충전 전류뿐이고, 충전 금지 비트(7)는 그대로 둔다.

```c
// hw_def.h — 배터리 : 리튬 1셀 4.2 V, 300 mAh
#define      HW_PMIC_ICHG_MA        150    // 0.5C, 부팅할 때 적용
#define      HW_PMIC_ICHG_MAX_MA    300    // 1C. 이 값을 넘는 요청은 잘라낸다
```

배터리를 바꾸면 이 두 값을 바꾼다. 충전을 끄고 켜는 것은 레지스터 대신 **/CE 핀**으로 한다 (되돌리기 쉽다).

> 보드 기본 설정은 충전 전류 **10 mA** 였다 (baram-nrf54-arduino 실측 기록과 같음). 300 mAh 배터리에 0.5C 인 150 mA 로 올렸다.

## 4. Zephyr charger 드라이버를 쓰지 않은 이유

Zephyr 에 `ti,bq25186` 바인딩과 `charger_bq2518x.c` (`CONFIG_CHARGER_BQ2518X`) 가 있다.
표준 charger API(`CHARGER_PROP_ONLINE/STATUS`, 전압·전류 get/set, `charge_enable`)를 제공한다. 그런데,

- **초기화할 때 레지스터를 여러 개 쓴다** — IC_CTRL(워치독/안전 타이머/TS), SYS_REG, CHARGE_CTRL1, 목표 전압, 충전 전류.
  즉 보드의 충전 조건이 DTS 값으로 덮어써진다.
- STAT1/FLAG0 의 **상세 이상·래치 정보가 API 에 없다** (TS 상태, VIN 과전압, 안전 타이머 등).

그래서 지금은 직접 읽는 모듈로 두었다. 충전 조건을 전부 DTS 로 관리하고 싶어지면 그때 바꾼다 (전환 시 배터리 사양 필요).

## 5. 저전력

| 항목 | 내용 |
|---|---|
| 폴링 | 하지 않는다. 상태 변화는 `/INT` 인터럽트(`pmicSetEventISR`)로 알고 그때 레지스터를 읽는다 |
| I2C | 전송할 때만 TWIM 동작 (PM runtime) |
| /CE | 기본은 입력(띄움). 풀다운이 충전 허용을 유지하므로 핀 전류가 없다 |
| 충전 중 | 충전 전류는 배터리로 가는 전류라 J1 측정값(모듈 소비전류)과는 별개다 |

## 6. 검증 결과 (2026-09-20, NCS v3.4.1, macOS)

- [x] 빌드: FLASH 82 KB / RAM 25 KB
- [x] `pmic info` : ID 0x41, CC 충전, 목표 4.20 V, 입력 제한 500 mA, /PG active
- [x] `pmic ichg 150` 적용 (레지스터 0x04 = 0x2A), `pmic ichg 500` → 300 mA 로 잘림
- [x] `pmic ce off` → `done 또는 충전 금지`, `pmic ce on` → 복구
- [x] 충전 확인 : 배터리 전압 3.898 V → 3.999 V (150 mA 로 올린 뒤)
- [ ] `/INT` 이벤트 실제 발생 확인 (충전 완료·입력 제거 시)
