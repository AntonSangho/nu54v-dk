# 10. ap 모듈 구조 (`firmware/projects/module`)

log 예제(09) 위에 **ap 모듈 구조**를 올린다. 기능을 모듈 단위로 나누고, 모듈마다 자기 스레드에서 돈다.
첫 모듈은 cli 를 자기 스레드로 옮긴 `cli_mgr` 이다.

## 1. 구조

```
main()
 ├─ hwInit()          하드웨어 모듈 (led, uart, cli, log, button …)
 ├─ apInit()
 │    └─ moduleInit()     .module 섹션의 모듈을 우선순위 순서로 init()
 │         └─ cli_mgr     init() 에서 cli 스레드 생성 → moduleWaitReady() 후 시작
 │    └─ logBoot(false)   모듈 초기화 로그까지 부팅 로그에 남김
 └─ apMain()          main 스레드는 할 일이 없어 잠든다 (k_sleep(K_FOREVER))

src/ap/
├── ap.c / ap_def.h
└── modules/
    ├── module.c/h            MODULE_DEF, moduleInit, moduleWaitReady, module CLI
    └── common/cli/cli_mgr.c  cli 모듈 (스레드)
src/bsp/ldscript/ldscript.ld  .module 섹션 (_smodule ~ _emodule)
```

모듈 선언:

```c
MODULE_DEF(cli){
  .name     = "cli",
  .priority = MODULE_PRI_LOW,      // HIGH → NORMAL → LOW 순서로 init
  .init     = cliMgrInit,
};
```

## 2. CLI 명령

```
cli# module info
count     : 1
0 : cli              pri 3

cli# module thread          스레드별 우선순위 / 스택 사용량 (사용 / 전체 바이트)
cli          pri   5, stack   560 /  4096
sysworkq     pri  -1, stack   240 /  1024
idle         pri  15, stack    64 /   320
main         pri   0, stack   596 /  4096

cli# log boot               … 0008 [  ] moduleInit() / 000B cli OK 까지 남음
```

## 3. 레퍼런스와 다른 점

| 항목 | nu54dk | NU87-TinyDK | 여기 |
|---|---|---|---|
| module.h | `MODULE_PRI_LOW` 까지 | `MODULE_PRI_MAX`, `update`, `arg`, `event_cb` | NU87 구조. `event_cb` 는 event 모듈이 없어 뺌 |
| 섹션 | ldscript.ld (`_TEXT_SECTION_NAME` 에 넣음) | 플랫폼 링커 | `zephyr_linker_sources(SECTIONS …)` 로 `.module` 출력 섹션을 따로 둠 |
| 초기화 호출 | system 모듈 스레드가 `moduleInit()` | `apInit()` 에서 `moduleInit()` | NU87 처럼 `apInit()` |
| 모듈 스레드 시작 동기화 | `systemIsReady()` (mutex) | `threadBegin()` | `moduleWaitReady()` (`k_event`, 여러 스레드가 함께 기다림) |
| cli 스레드 | `cliMain(); delay(5);` | `cliMain(); delay(1);` | `cliMain()` 후 입력이 없으면 `uartWaitRx()` 로 잠듦 |
| main 루프 | system 루프 (LED, delay 5) | `moduleUpdate(); delay(1);` | `k_sleep(K_FOREVER)` |
| 이름 | `ap/modules/common/cli/cli.c` | `cli_mgr.c` | `cli_mgr.c` — hw 의 `cli.c/h` 와 이름이 겹치지 않게 |

- `update` / `moduleUpdate()` 는 NU87 모듈을 그대로 가져올 수 있게 남겨 두었다. Zephyr 에서는 모듈이 자기 스레드를 만든다.
- 스레드 우선순위/스택은 `hw_def.h` 의 `_HW_DEF_RTOS_THREAD_PRI_xxx`, `_HW_DEF_RTOS_THREAD_MEM_xxx` (nu54dk 방식).

## 4. 저전력

| 항목 | 내용 |
|---|---|
| 스레드 | 모든 스레드가 이벤트(`uartWaitRx`, 세마포어, `k_event`)로만 깨어난다. 기다리는 동안 idle 스레드가 WFI |
| main | 초기화 후 `k_sleep(K_FOREVER)` — 깨어나지 않는다 |
| 이전 예제와 비교 | 05~09 의 ap 루프는 `cliMain(); delay(1);` 로 1 ms 마다 깨어났다 → 이제 입력이 있을 때만 |
| 스택 | `CONFIG_INIT_STACKS` 는 스택 사용량 측정용 (부팅 시 스택을 채우는 시간이 조금 든다) |

스택 사용량을 보면 cli 560 B, main 596 B 이다. 이후 모듈이 늘어난 뒤 `module thread` 로 보고 크기를 줄인다.

## 5. 검증 결과 (2026-09-20, NCS v3.4.1, macOS)

- [x] 빌드: FLASH 61 KB / RAM 24.4 KB
- [x] `module info`, `module thread` (cli 스레드 동작), 부팅 로그에 모듈 초기화 기록 — baram-term
