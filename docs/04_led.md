# 04. LED 예제 (`firmware/projects/led`)

보드 브링업 첫 단계. 4개 LED 배선과 빌드/다운로드/디버그/콘솔 경로를 확인한다.

## 1. 동작

- 부팅 시 VCOM1(uart20, 115200) 로 부팅 메시지 출력
- LED1 : 500 ms 주기 점멸 (250 ms 마다 토글)
- LED2 → LED3 → LED4 : 250 ms 간격 순차 점등

```
*** Booting nRF Connect SDK v3.3.0-... ***
[ Firmware Begin... ]
Booting..Name 		: NU54-DK-LED
Booting..Ver  		: V260920R1
...
Board         		: nu54v_dk/nrf54l15/cpuapp
```

## 2. 소스 구조

레퍼런스 프로젝트(`nu54dk/firmware/nu54l15-fw`)의 계층 구조를 그대로 따른다.

```
src/
├── main.c / main.h        hwInit() → apInit() → apMain()
├── ap/                    애플리케이션 (ap.c, ap_def.h)
├── hw/                    하드웨어 초기화 (hw.c, hw_def.h: _USE_HW_xxx 기능 선택)
│   └── driver/led.c       LED 드라이버
├── bsp/                   delay(), millis(), logPrintf()
└── common/                def.h, err_code.h, hw/include/led.h
```

| 파일 | 내용 |
|---|---|
| `hw/hw_def.h` | `_USE_HW_LED`, `HW_LED_MAX_CH 4`, 보드 이름/버전 |
| `hw/driver/led.c` | `led_tbl_t` 테이블(type / dt spec / pin / on·off 상태) 기반. `ledInit/On/Off/Toggle/ToSleep` |
| `bsp/bsp.c` | `logPrintf()` 는 `__weak` 기본 구현(printk). 나중에 log 모듈이 추가되면 그쪽이 우선 |
| `ap/ap.c` | millis() 기반 주기 처리 |

LED 핀은 코드에 직접 쓰지 않고 보드 DTS 의 `led0~led3` 노드에서 가져온다 (`GPIO_DT_SPEC_GET`).

## 3. 저전력 고려

| 항목 | 내용 |
|---|---|
| 메인 루프 | 1 ms 폴링 대신 다음 주기까지 `delay(남은 시간)` → 그동안 idle 스레드가 WFI (System ON idle) |
| UART | `CONFIG_PM_DEVICE_RUNTIME=y` + DTS `pm-device-runtime-auto` → 출력하지 않을 때 UARTE suspend |
| LED 슬립 | `ledToSleep()` : LED 끄고 핀을 `GPIO_DISCONNECTED` 로 (입력버퍼 off, 누설 제거) |
| 디버그 옵션 | `CONFIG_DEBUG_OPTIMIZATIONS`, `CONFIG_DEBUG_THREAD_INFO` 는 개발용. 전류 측정/양산 시 끈다 |

소비전류 측정 시 주의:
- 디버거가 붙은 상태(디버그 세션 이후)는 전류가 높다 → 전원을 한 번 껐다 켠 뒤 측정
- J1 점퍼 위치에서 VDD_MOD 전류 측정, DAP 레벨시프터는 SW1 로 분리
- DC/DC 미사용(LDO) 상태임 ([02_board_package](02_board_package.md) TODO)

## 4. 검증 결과 (2026-09-20, NCS v3.3.0, macOS)

- [x] 빌드: FLASH 41 KB / RAM 8 KB
- [x] 다운로드: `fw flash` (pyOCD, CMSIS-DAP "NU54DK_v2")
- [x] 콘솔: VCOM 부팅 메시지 확인
- [x] 디버그: pyOCD gdbserver + GDB, `ledToggle` 브레이크포인트/백트레이스 확인
- [ ] LED 4개 육안 확인 (사용자)
- [ ] VS Code F5 디버깅 (사용자)
- [ ] Windows / Linux 에서 빌드·다운로드
