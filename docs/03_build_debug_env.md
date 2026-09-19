# 03. 빌드 / 다운로드 / 디버그 환경

macOS · Linux · Windows 에서 같은 방법으로 빌드·다운로드·디버깅한다.
각 프로젝트 폴더(`firmware/projects/<이름>`)를 **VS Code 로 따로 열어서** 쓴다.

## 1. 폴더 구조

```
nu54v-dk/
├── CLAUDE.md                 # Claude 세션용 작업 규칙 (docs 요약)
├── docs/                     # 설명 문서 (번호 = 구현 순서)
├── hardware/                 # 회로도
└── firmware/
    ├── ncs_config.json       # NCS SDK 버전 · 보드 타깃 (전 프로젝트 공통)
    ├── .clang-format         # 코드 스타일 (전 프로젝트 공통)
    ├── .tools/               # (자동 생성, git 제외) 디버거용 툴 링크
    ├── boards/nucode/nu54v_dk/   # 공용 보드 패키지
    ├── scripts/
    │   ├── fw                # macOS / Linux 진입점
    │   ├── fw.cmd            # Windows 진입점
    │   └── fw.py             # 실제 로직
    └── projects/             # 예제 프로젝트 (VS Code 로 각 폴더를 연다) — docs/roadmap.md 순서
        ├── led/  uart/  i2c/  shtc3/ …
        └── <예제>/
            ├── .vscode/      # tasks / launch / settings / c_cpp_properties
            ├── CMakeLists.txt
            ├── prj.conf
            └── src/
```

**projects/ 하위에 두는 이유**: `firmware/` 에는 프로젝트가 아닌 공용 자산(보드 패키지, 스크립트, 설정)이 같이 있다.
프로젝트를 `projects/` 로 모으면 공용 자산과 섞이지 않고, 모든 프로젝트가 `../../` 로 같은 위치를 참조할 수 있다.

## 2. PC 준비

1. nRF Connect for Desktop → Toolchain Manager (또는 VS Code nRF Connect 확장)로 NCS SDK + 툴체인 설치
   - 기본 설치 경로: macOS `/opt/nordic/ncs`, Windows `C:\ncs`, Linux `~/ncs`
   - 다른 경로라면 환경변수 `NCS_ROOT` 지정
2. `firmware/ncs_config.json` 의 `ncs_version` 과 같은 버전이 설치되어 있어야 한다.
   - **macOS 13 이하**: NCS v3.4.1 툴체인의 cmake 가 실행되지 않는다 → `brew install cmake` (fw 가 자동으로 사용)
3. VS Code 확장: **Cortex-Debug**(marus25.cortex-debug), **C/C++**(ms-vscode.cpptools). nRF Connect 확장팩은 선택.
4. Linux: 일반 사용자로 CMSIS-DAP 에 접근하도록 udev 규칙 추가.
   `lsusb` 로 VID:PID 를 확인한 뒤 `/etc/udev/rules.d/50-nu54dk.rules` 에
   `SUBSYSTEM=="usb", ATTR{idVendor}=="<VID>", ATTR{idProduct}=="<PID>", MODE="0666"` 를 넣고
   `sudo udevadm control --reload && sudo udevadm trigger`. (VCOM 은 `dialout` 그룹 필요)

전역 PATH 설정이나 nRF Connect 터미널은 필요 없다. `fw` 스크립트가 `ncs_config.json` 의 버전에 맞는 툴체인을 찾아
`environment.json` 으로 환경을 구성한 뒤 west / pyocd 를 실행한다.

## 3. 명령 (터미널)

프로젝트 폴더에서 실행한다.

| 동작 | macOS / Linux | Windows |
|---|---|---|
| 빌드 | `../../scripts/fw build` | `..\..\scripts\fw.cmd build` |
| 전체 재빌드 | `../../scripts/fw build -p` | `..\..\scripts\fw.cmd build -p` |
| 다운로드 | `../../scripts/fw flash` | `..\..\scripts\fw.cmd flash` |
| 칩 삭제 | `../../scripts/fw erase` | `..\..\scripts\fw.cmd erase` |
| 리셋 | `../../scripts/fw reset` | `..\..\scripts\fw.cmd reset` |
| build 삭제 | `../../scripts/fw clean` | `..\..\scripts\fw.cmd clean` |
| Kconfig | `../../scripts/fw menuconfig` | `..\..\scripts\fw.cmd menuconfig` |
| GDB 서버 | `../../scripts/fw debugserver` | `..\..\scripts\fw.cmd debugserver` |
| 경로 확인 | `../../scripts/fw env` | `..\..\scripts\fw.cmd env` |
| NCS 셸 | `../../scripts/fw shell` | `..\..\scripts\fw.cmd shell` |

내부적으로 `west build -b nu54v_dk/nrf54l15/cpuapp -d build . -- -DBOARD_ROOT=<firmware>` 를 실행한다.

## 4. VS Code

| 동작 | 방법 |
|---|---|
| 빌드 | `Ctrl/Cmd+Shift+B` (Build 태스크) |
| 기타 | `Terminal → Run Task…` → Rebuild (pristine) / Flash / Erase chip / Reset / Clean / Menuconfig / Env info |
| 디버그 | `F5` → **Debug (pyOCD)** : 빌드 → 다운로드 → `main` 에서 정지 |
| 붙기 | **Attach (pyOCD)** : 실행 중인 타깃에 리셋 없이 연결 |

- 태스크는 모두 `fw` 스크립트를 호출하므로 터미널 명령과 결과가 같다.
- 디버거 경로(`gdbPath`, `serverpath`, `svdFile`)는 `firmware/.tools/{gdb,pyocd,svd}` 를 가리킨다.
  이 링크는 `fw build`(또는 `fw tools`) 가 SDK 버전에 맞게 만든다 (macOS/Linux 심볼릭 링크, Windows junction).
  → SDK 버전을 바꿔도 launch.json 은 수정하지 않는다.
- IntelliSense 는 `build/compile_commands.json` 을 쓴다 (빌드 후 자동 복사).
- SWD 는 4 MHz (`--frequency 4000000`). 기본 1 MHz 에서는 GDB `load` 중 probe 타임아웃이 날 수 있었다.

## 5. SDK 버전 변경

1. 새 SDK + 툴체인 설치
2. `firmware/ncs_config.json` 의 `ncs_version` 수정 (예: `"v3.4.1"`)
3. 각 프로젝트에서 `fw build -p` (pristine 필수)

한 번만 시험할 때는 파일 수정 없이 `NCS_VERSION=v3.4.1 ../../scripts/fw build -p`.

## 6. 새 프로젝트 추가

1. 바로 앞 단계 예제를 복사해 이름 변경 (cli 가 필요하므로 보통 `uart` 이후 예제. 예: `uart` → `button`)
2. `CMakeLists.txt` 의 `project(...)` 이름, `hw_def.h` 의 `_DEF_BOARD_NAME` 변경
3. `build/` 폴더는 복사하지 않는다
4. VS Code 로 새 폴더를 열어 빌드

launch.json 의 ELF 경로는 `build/${workspaceFolderBasename}/zephyr/zephyr.elf` 이다
(sysbuild 가 앱 이미지 폴더를 프로젝트 폴더 이름으로 만든다). 그래서 폴더 이름만 바꾸면 된다.

## 7. 시리얼 / CLI

- VCOM1(uart20) = cli·부팅 로그, VCOM0(uart30) = 두 번째 채널. 115200 8N1, 줄끝 CR
- 터미널은 baram-term 사용. 한 포트를 두 프로그램이 동시에 열지 않는다 (macOS 는 수신 데이터가 나뉘어 둘 다 깨진다)
- Claude 세션은 baram-term 스킬(`baram-ctl`)로 같은 창을 통해 명령을 보낸다 ([05_uart](05_uart.md))

## 8. 알려진 경고

- `Board ID 5415 is not recognized` / `NRF54L15 is not in a secure state` : pyOCD 정보성 경고, 동작에 영향 없음.
- `Error during board uninit` : 가끔 `fw flash` 끝에 나오지만 쓰기(`Erased … programmed …`)는 완료된 상태.
- `Deprecated symbol NRF_PLATFORM_LUMOS is enabled` : NCS v3.4.1 SDK 자체 경고 (nRF54L 에 기본 켜짐). 무시.
- `[fw] 경고: 툴체인 cmake 실행 불가 → 임시로 … 사용` : macOS 13 이하 + NCS v3.4.1. 시스템 cmake(`brew install cmake`)나
  다른 툴체인의 cmake 를 대신 쓴다. 빌드 결과는 같다.
