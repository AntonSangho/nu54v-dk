# 08. 버튼 (`firmware/projects/button`)

uart 예제(05) 위에 button 모듈을 올린다. 보드 스위치 SW1~SW4 의 눌림/뗌, **클릭**, **길게 누름**을 인식한다.

## 1. 하드웨어

| 채널 | 이름 | 스위치 | 핀 | DTS |
|---|---|---|---|---|
| 0 | BTN1 | SW1 | P1.13 | `sw0` |
| 1 | BTN2 | SW2 | P1.09 | `sw1` |
| 2 | BTN3 | SW3 | P1.08 | `sw2` |
| 3 | BTN4 | SW4 | P0.04 | `sw3` |

- 스위치 → GND, 외부 풀업 없음 → DTS 에서 내부 풀업 + `GPIO_ACTIVE_LOW` (읽은 값 1 = 눌림)
- 보드 DTS `sense-edge-mask` 로 네 핀 모두 GPIO SENSE 엣지 검출 (GPIOTE IN 채널 사용 안 함)

## 2. CLI 명령

```
cli# button info        이름, 핀, 현재 상태
BTN1   : P1.13, released
...
cli# button show        4개 상태를 0/1 로 계속 표시 (아무 키로 종료)
cli# button event       이벤트가 생길 때마다 출력 (아무 키로 종료)
BTN1   pressed
BTN1   click
BTN1   released, 180 ms
BTN2   pressed
BTN2   long                ← 1초가 지나면 떼기 전에 바로
BTN2   released, 2350 ms
```

## 3. 동작 방식 (저전력 우선)

레퍼런스(stm32h7-lvgl 의 최신 button.c)는 swtimer 로 **항상 주기 스캔**하며 반복/이벤트 테이블까지 관리한다.
여기서는 저전력을 우선해 필요한 기능만 남기고 **주기 스캔을 없앴다**.

```
핀 변화 ─(GPIO SENSE 인터럽트)─► 디바운스 타이머 20 ms (흔들리면 다시 미룸)
                                     │
                                     ▼ 상태 확정
                   눌림 : 누른 시각 저장, PRESSED, 길게 누름 1회 타이머 시작 (1 s)
                   뗌   : 뗀 시각 저장, 길게 누름 타이머 정지, RELEASED (+ 1 s 전이면 CLICK)
길게 누름 타이머 만료 ─► LONG (눌린 상태일 때만)
```

- 버튼을 누르고 있는 동안에도 CPU 는 깨어나지 않는다. 누른 시간은 조회할 때 `millis() - 누른 시각` 으로 계산한다.
- 깨어나는 시점: 누를 때, 뗄 때 (디바운스 포함 각 1~2회), 길게 누름 시간에 1회.

| API | 내용 |
|---|---|
| `buttonGetPressed(ch)` / `buttonGetData()` / `buttonGetPressedCount()` | 현재 상태 (레퍼런스와 같음) |
| `buttonGetPressedTime(ch)` | 누르는 중이면 지금까지, 뗐으면 마지막으로 누른 시간 |
| `buttonGetName(ch)` | `NAME_DEF` 로 만든 이름 (`BTN1` …) |
| `buttonGetEvent(ch)` | 쌓인 이벤트 비트를 꺼내고 지움: `BUTTON_EVT_PRESSED / RELEASED / CLICK / LONG` |
| `buttonSetLongTime(ch, ms)` | 길게 누름 시간 (기본 1000 ms) |
| `buttonSetEventISR(func)` | 이벤트가 생기면 부르는 콜백 (타이머 ISR 문맥) → 앱이 폴링 없이 기다릴 때 사용 |

레퍼런스 헤더에 있던 반복(repeat), 이벤트 테이블(`buttonEvent*`), 뗀 시간 조회 등은 넣지 않았다. 필요해지면 같은 방식(1회 타이머)으로 추가한다.

## 4. 저전력

| 항목 | 내용 |
|---|---|
| 엣지 검출 | GPIO SENSE (`sense-edge-mask`) → GPIOTE IN 채널보다 대기 전류가 작다. System OFF 에서도 같은 핀으로 깨울 수 있다 (11 power) |
| 스캔 | 없음. 인터럽트 + 1회 타이머만 사용 |
| 풀업 | 내부 풀업 (약 13 kΩ). 버튼을 누르고 있는 동안만 전류가 흐른다 (3.3 V / 13 kΩ ≈ 250 µA) |

## 5. 검증 결과 (2026-09-20, NCS v3.4.1, macOS)

- [x] 빌드: FLASH 59 KB / RAM 13.9 KB
- [x] `buttonInit()`, `button info` (4개 핀 released) — baram-term
- [x] 실제 누름: 눌림/뗌/클릭/길게 누름 동작 확인 (사용자, baram-term)
