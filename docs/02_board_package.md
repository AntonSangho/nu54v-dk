# 02. 보드 패키지 (nu54v_dk)

- 보드 타깃: **`nu54v_dk/nrf54l15/cpuapp`**
- 위치: `firmware/boards/nucode/nu54v_dk/` (모든 프로젝트가 공유)
- 근거 회로도: `hardware/NU54_DK_2026-07-13T15_16_09_UTC_SCH.pdf` (Variant NU-54DK-C)
- 기반: NCS 의 `nrf54l15dk` 보드 (이 보드의 LED/버튼/VCOM 배치가 DK 와 동일)

## 1. 파일 구성

| 파일 | 역할 |
|---|---|
| `board.yml` | 보드 이름/벤더/SoC 선언 (HWMv2) |
| `Kconfig.nu54v_dk` | 보드 → SoC(`SOC_NRF54L15_CPUAPP`) 선택 |
| `Kconfig.defconfig` | 보드 기본 Kconfig |
| `Kconfig.sysbuild` | sysbuild 기본값 (Partition Manager 끔 → [01_memory_map](01_memory_map.md)) |
| `nu54v_dk_nrf54l15_cpuapp_defconfig` | 콘솔(UART), GPIO, MPU 기본 활성 |
| `nu54v_dk_nrf54l15_cpuapp.dts` | 하드웨어 정의 (LED, 버튼, UART, I2C, 클럭, 파티션) |
| `nu54v_dk-pinctrl.dtsi` | 핀 배치 (UART20/30, I2C21) |
| `nu54v_dk_nrf54l15_cpuapp.yaml` | twister 메타데이터 |
| `board.cmake` | 플래시/디버그 러너 (pyOCD 기본, J-Link/nrfutil 대체) |
| `board.c`, `CMakeLists.txt` | 부팅 시 보드 초기화: NFC 패드 끄기 (P1.02/P1.03 을 I2C 로 사용) |

## 2. 핀맵 (회로도 기준)

`SBx` = 솔더 브리지. 기본 연결 상태는 실물로 확인할 것.

| 핀 | 신호 | 연결 | DTS |
|---|---|---|---|
| P0.00 | LPUART_TX | VCOM0 → DAP (SB5) | `uart30` TX |
| P0.01 | LPUART_RX | VCOM0 (SB6) | `uart30` RX |
| P0.02 | LPUART_RTS | VCOM0 (SB7) | `uart30` RTS |
| P0.03 | LPUART_CTS | VCOM0 (SB8) | `uart30` CTS |
| P0.04 | SW4 | 스위치 → GND | `button3` / `sw3` |
| P1.00 / P1.01 | XL1 / XL2 | 32.768 kHz Y1 (SB18/SB19), C1/C2 13 pF | `lfxo` external |
| P1.02 | SDA | Qwiic J5 (SB14), PMIC (SB16) — NFC1 겸용 | `i2c21` SDA |
| P1.03 | SCL | Qwiic J5 (SB15), PMIC (SB17) — NFC2 겸용 | `i2c21` SCL |
| P1.04 | UARTE_TX | VCOM1 → DAP (SB9) | `uart20` TX (콘솔) |
| P1.05 | UARTE_RX | VCOM1 (SB10) | `uart20` RX |
| P1.06 | UARTE_RTS | VCOM1 (SB11) | `uart20` RTS |
| P1.07 | UARTE_CTS | VCOM1 (SB12) | `uart20` CTS |
| P1.08 | SW3 | 스위치 → GND | `button2` / `sw2` |
| P1.09 | SW2 | 스위치 → GND | `button1` / `sw1` |
| P1.10 | LED2 | 버퍼 → NPN → LED | `led1` |
| P1.11 (AIN4) | PMIC_INT | BQ25186 /INT (SB1) | (미정의) |
| P1.12 (AIN5) | VBAT_MON | VBAT × 1M/(470k+1M) ≈ 0.68 (SB4) | (미정의) |
| P1.13 | SW1 | 스위치 → GND | `button0` / `sw0` |
| P1.14 | LED4 | 버퍼 → NPN → LED | `led3` |
| P2.07 | LED3 | 버퍼 → NPN → LED, **MOD_SWO 와 공유 (SB13)** | `led2` |
| P2.08 | PMIC_PG | BQ25186 /PG (SB2) | (미정의) |
| P2.09 | LED1 | 버퍼 → NPN → LED | `led0` |
| P2.10 | PMIC_CE | BQ25186 /CE (SB3) | (미정의) |
| P2.00 ~ P2.06 | - | 헤더 P2 로만 인출 | |
| SWDIO / SWDCLK / RESET | | DAP 레벨시프터(SB22~24), J4(10핀), P3 | |

- **LED**: 핀 High = 점등 (`GPIO_ACTIVE_HIGH`). LED 전류는 VDD_3V3_SYS 에서 흐르고 MCU 핀은 버퍼(NC7WZ17) 입력만 구동한다.
- **버튼**: 외부 풀업 없음 → 내부 풀업 사용, 눌림 = Low (`GPIO_PULL_UP | GPIO_ACTIVE_LOW`).
- **P2.07**: LED3 와 SWO 가 공유된다. SWO 트레이스를 쓰려면 LED3 를 쓰지 않거나 SB13 을 확인한다.

## 3. 온보드 디버거 / VCOM

- nRF52840 기반 **CMSIS-DAP** (USB 장치명 `NU54DK_v2`) → pyOCD 로 다운로드/디버깅 (`--target nrf54l`)
- VCOM 2개: VCOM1 = `uart20`(P1.04~07, 콘솔), VCOM0 = `uart30`(P0.00~03)
- macOS 에서는 `/dev/cu.usbmodem*` 2개로 보인다. 콘솔은 115200 8N1.
- DAP 레벨시프터 SW1(DISABLE_SWD/DISABLE_UART) 로 디버거 신호를 분리할 수 있다 (저전력 측정 시 사용).

## 4. 전원 / 저전력 관련 하드웨어

| 항목 | 내용 |
|---|---|
| VDD_3V3_SYS | TPS7A37 LDO 3.3 V (VSYS 입력) |
| VDD_MOD | 모듈 전원. **J1 점퍼**로 VDD_3V3_SYS 와 연결 → 전류 측정 지점 (PPK2 연결) |
| 충전 | BQ25186 (I2C 0x6A), 배터리 J2, VBAT_MON 분압 |
| D5 (파랑) | VDD_MOD 인가 표시 |
| 레벨시프터 | VDD_MOD_LS (DAP 전원이 있을 때만), SW1 로 분리 가능 |

DTS 결정 사항:
- `lfxo`: 외부 부하 커패시터(C1/C2) → `load-capacitors = "external"`
- `hfxo`: 모듈 내부 → nRF54L15 DK 기본값(internal, 15 pF)
- **DC/DC 미사용(LDO)**: 모듈 내부 DC/DC 인덕터 실장 여부 미확인. 확인되면 DTS 주석의 `vregmain` 설정으로 DC/DC 를 켠다 (소비전류 감소).
- `uart20/uart30/i2c21` 에 `zephyr,pm-device-runtime-auto` → `CONFIG_PM_DEVICE_RUNTIME=y` 인 앱에서 미사용 시 자동 suspend.
- `uart30`, `i2c21` 는 기본 disabled. 필요한 프로젝트에서 overlay 로 `status = "okay"`.

## 5. 미확인 / TODO

- [ ] 모듈 내부 DC/DC 인덕터 유무 → DC/DC 활성화
- [ ] HFXO 내부 부하 용량 값 (모듈 데이터시트)
- [ ] 솔더 브리지 기본 상태 (SB1~SB24) 실물 확인
- [ ] PMIC(BQ25186) / VBAT_MON / Qwiic 노드 정의 (I2C·ADC 예제에서)
- [x] P1.02/P1.03 NFC 핀 겸용 → `board.c` 에서 `NFCT.PADCONFIG` 끔 (리셋값이 NFC 활성). I2C 동작 확인 (05)
