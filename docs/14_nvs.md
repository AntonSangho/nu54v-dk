# 14. 설정 저장 (`firmware/projects/nvs`)

pmic 예제(13) 위에 **nvs** 모듈을 올린다. 이름으로 값을 저장하고 읽는다. 전원을 꺼도 남는다.
포함 모듈: 지금까지 만든 것 전부 + `nvs`

## 1. 구성

| 항목 | 내용 |
|---|---|
| API | NU87 과 같음 — `nvsInit / nvsIsExist / nvsSet / nvsGet` (+ `nvsDel` 추가) |
| 저장소 | Zephyr **Settings** 서브시스템 |
| 백엔드 | **ZMS** (Zephyr Memory Storage) — 지우기 없이 쓰는 RRAM/MRAM 용 |
| 위치 | 보드 DTS 의 `storage_partition` (RRAM `0x174000`, 36 KB — [01_memory_map](01_memory_map.md)) |
| 키 | `nu54/<이름>` |

```c
// hw_def.h
#define _USE_HW_NVS

// prj.conf
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_ZMS=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_ZMS=y
```

## 2. CLI 명령

```
cli# nvs set name NU54V-DK      이름으로 저장 (문자열)
cli# nvs get name
nvs get name = "NU54V-DK"
cli# nvs info                   저장된 키 목록
  name               9 B
  count              6 B
count  : 2
cli# nvs del count              지우기
```

## 3. 왜 ZMS 인가

| 백엔드 | 쓰임 |
|---|---|
| NVS | NOR 플래시용. 섹터를 지우고 다시 쓴다 (qmk-zephyr 의 nRF52 가 이것을 쓴다) |
| **ZMS** | RRAM·MRAM 처럼 **지우기 없이 덮어쓰는** 메모리용. nRF54L15 의 RRAM 에 맞다 |
| FCB / FILE | 각각 플래시 서큘러 버퍼, 파일시스템 위 |

NU87 은 플래시 섹터 두 개를 직접 핑퐁으로 관리했다. nRF54L 에서는 Zephyr 가 이미 RRAM 에 맞는 백엔드를 주므로 그것을 쓰고,
API 만 같은 모양으로 감쌌다.

## 4. 쓰기 횟수와 저전력 (qmk-zephyr 의 교훈)

[qmk-zephyr](https://github.com/chcbaram/qmk-zephyr) 은 QMK 키맵 저장에서 겪은 것을 남겨 두었다 (`docs/PORTING-NOTES.md` §2.7).

- **RAM 미러 + settle-flush**: 쓰기는 미러만 갱신하고, 마지막 쓰기 후 100 ms 조용하면 한 번에 flush.
  편집 버스트 수천 바이트가 **플래시 쓰기 1회**로 합쳐진다. program/erase 가 전력을 가장 많이 먹는다.
- **함정**: 지연 기록은 "나중에 쓰는" 것이라, 그 사이 저전력 루프가 오래 자면 flush 가 같이 멈춰 **값이 유실된다**.
  그쪽에서는 dirty 인 동안 idle 대기를 20 ms 로 줄여서 해결했다.

여기서는 설정을 자주 쓰지 않으므로 `nvsSet()` 이 바로 기록한다. 주기적으로 쓰는 값(예: 사용 시간, 센서 누적)이 생기면
같은 방식으로 모아서 쓰고, System OFF(11 power) 전에 반드시 flush 한다.

## 5. 저전력

| 항목 | 내용 |
|---|---|
| 읽기 | 부팅 때 필요한 값만 읽는다. 자주 읽는 값은 RAM 에 둔다 |
| 쓰기 | RRAM 쓰기는 전류를 먹는다. 값이 바뀔 때만 쓴다 (`nvsGet` 으로 비교 후 저장) |
| ZMS | 덮어쓸 때 섹터 지우기가 없어 NOR 플래시보다 쓰기 비용이 낮다 |

## 6. 검증 결과 (2026-09-20, NCS v3.4.1, macOS)

- [x] 빌드: FLASH 91 KB / RAM 25 KB
- [x] `nvs set` / `nvs get` / `nvs info` 목록 / `nvs del`
- [x] **리셋 후에도 값 유지** (`reset run` 뒤 `nvs get name` 그대로)
- [ ] 전원 완전히 끊은 뒤 유지 확인
- [ ] 설정 구조체(예: 보드 이름, 시간대) 저장 형태 정하기 — 15 rtc 에서 시간대부터
