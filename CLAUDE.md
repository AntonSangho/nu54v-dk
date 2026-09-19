# NU54-DK (nRF54L15) 펌웨어 저장소

작업 전에 **`docs/00_handoff.md`** 를 읽는다 (진행 상황, 다음 할 일, 결정 사항, 작업 규칙).
문서는 `docs/NN_<기능>.md` 형식이며 번호는 구현 순서다. 예제 계획은 `docs/roadmap.md`.

## 핵심 규칙 (상세: docs/00_handoff.md)

- 펌웨어 구조·모듈화는 사용자 방식 유지 (`main → hwInit/apInit/apMain`, `ap / hw / hw/driver / bsp / common` 계층, `hw_def.h` 의 `_USE_HW_xxx`)
- 모듈 추가 시 레퍼런스의 같은 모듈을 먼저 참조: https://github.com/chcbaram/NU87-TinyDK (`firmware/nu87-fw`, 로컬 `../NU87-TinyDK`, 최신 구조) → https://github.com/chcbaram/nu54dk (`firmware/nu54l15-fw`, 로컬 `../nu54dk`)
- 모듈은 자기 CLI 명령을 갖고 cli 로 시험한다 (ap 에 임시 시험 코드 금지)
- 항상 저전력 고려
- 핀은 보드 DTS(`firmware/boards/nucode/nu54v_dk`)에서 가져온다
- 기능 추가 시 docs 문서 작성 + `00_handoff.md` 갱신
- 커밋 메시지에 Claude 서명을 넣지 않는다

## 명령 (프로젝트 폴더에서)

```sh
../../scripts/fw build [-p]   # Windows: ..\..\scripts\fw.cmd
../../scripts/fw flash
../../scripts/fw env
```

SDK 버전 / 보드 타깃: `firmware/ncs_config.json`

## 보드 CLI 시험 (baram-term)

사용자가 baram-term 으로 VCOM1(cli) 포트를 열어 두므로 **포트를 직접 열지 않고** baram-term 스킬(`baram-ctl`)로 보낸다.

```sh
baram-ctl list                                           # 창 확인
baram-ctl --match NU54DK_v2 send "help" --until 'cli# $' --timeout 5
```

- 대상: USB 이름 `NU54DK_v2` (CMSIS-DAP, VID:PID 0D28:0204). VCOM1 = cli, VCOM0 = 시험용 두 번째 채널
- `fw flash` / 디버깅은 SWD 라서 release 필요 없음. 부팅 메시지는 리셋 전에 mark 를 잡고 `wait --since`
