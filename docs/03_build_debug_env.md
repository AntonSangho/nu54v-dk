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

### Linux 실측 (2026-09-25, Ubuntu, AntonSangho PC)

- **SDK 설치**: nRF Connect for Desktop(GUI) 대신 `nrfutil` CLI 로 받았다.
  기본 `~/ncs` 에 `toolchains/<hash>/nrfutil/bin/nrfutil` 이 이미 있으면 그걸 쓴다.
  ```sh
  NRFUTIL=~/ncs/toolchains/<hash>/nrfutil/bin/nrfutil
  $NRFUTIL install toolchain-manager     # 한 번만
  $NRFUTIL install sdk-manager           # 한 번만
  $NRFUTIL sdk-manager install v3.4.1 --install-dir ~/ncs   # 툴체인(컴파일러) + SDK 소스 둘 다 받음
  ```
  버전마다 SDK ~4.6 GB + 툴체인 ~4.3 GB. 여러 버전을 같이 두면 금방 커지므로 안 쓰는 버전은
  `$NRFUTIL sdk-manager uninstall <version>` 으로 지운다.
- **`fw` 스크립트의 python3 가 죽는 문제** (`error while loading shared libraries: libpython3.12.so.1.0`):
  툴체인 python3(`usr/local/bin/python3`)는 자체 `libpython*.so` 를 쓰는데, `fw` 가 이 python3 를
  띄우기 *전에* `LD_LIBRARY_PATH` 를 설정해주지 않아서 `fw.py` 실행 전에 즉시 죽는다.
  `firmware/scripts/fw` 가 python3 경로를 찾은 직후 해당 툴체인의 `lib`/`lib/x86_64-linux-gnu`/
  `usr/local/lib` 를 `LD_LIBRARY_PATH` 에 넣도록 고쳤다 (`f320ee3`). 이 저장소를 받으면 자동 적용된다.
- **udev 규칙 실측**: `/dev/bus/usb/.../...` 노드 권한이 규칙 적용 전엔 `root:root rw-rw-r--` 라
  일반 사용자가 못 열어 `pyocd list` 에 보드가 안 뜬다 (VCOM `/dev/ttyACM*` 는 이미 `666` 이라 무관).
  규칙 적용 + USB 재연결 후 `pyocd list` 에 `NU54DK_v2_Pre-release` 로 뜬다.
- **SEGGER J-Link 를 동시에 연결해둔 경우**: pyOCD 가 두 프로브를 다 보고한다. `fw flash` 는 첫 프로브를
  쓰므로, 온보드 프로브를 확실히 쓰려면 `pyocd list` 로 UID 를 확인한 뒤
  `fw flash --probe <UID>` (또는 `FW_PROBE=<UID>`) 로 지정한다. 동시 SWD 연결 자체는 위 §8 (내장 프로브 결함)
  경고대로 피한다.

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
- `serverArgs` 의 `--script scripts/pyocd_cortex_debug.py` 는 로그 문구 호환용이다 (아래 §8 타임아웃 항목).

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

- `Failed to launch PyOCD GDB Server: Timeout.` : 원인 두 가지.
  1. **로그 문구 불일치.** cortex-debug 1.12.1 은 서버 기동을 출력 문구로만 판단하는데
     (`dist/debugadapter.js` → `initMatch()` → `/GDB server started (at|on) port/`),
     pyOCD 0.36 부터 문구가 `GDB server listening on port N (core N)` 으로 바뀌어 영영 매칭되지 않는다.
     서버 자체는 정상이다. → `firmware/scripts/pyocd_cortex_debug.py` (pyOCD 사용자 스크립트) 가
     로그 필터로 문구만 예전 형식으로 되돌린다. launch.json 이 `--script` 로 넘긴다.
  2. **이전 세션의 pyocd 가 남아 있음.** 고아 프로세스가 프로브와 50000/50001 포트를 잡고 있으면
     새 서버가 `Unable to claim interface for probe …` 로 뜨지 못한다. → `pkill -f "pyocd gdbserver"`

- `Board ID 5415 is not recognized` / `NRF54L15 is not in a secure state` : pyOCD 정보성 경고, 동작에 영향 없음.
- `Error during board uninit` : 가끔 `fw flash` 끝에 나오지만 쓰기(`Erased … programmed …`)는 완료된 상태.
- `Deprecated symbol NRF_PLATFORM_LUMOS is enabled` : NCS v3.4.1 SDK 자체 경고 (nRF54L 에 기본 켜짐). 무시.
- `[fw] 경고: 툴체인 cmake 실행 불가 → 임시로 … 사용` : macOS 13 이하 + NCS v3.4.1. 시스템 cmake(`brew install cmake`)나
  다른 툴체인의 cmake 를 대신 쓴다. 빌드 결과는 같다.

## 내장 프로브(DAPLink)가 죽을 때 — 2026-09-20 실측

보드의 디버그 프로브는 **별도 MCU**(nRF52840, `HIC ID 6e052840`)에서 도는
`NU54DK_v2_Pre-release` DAPLink 펌웨어다. 이것이 죽으면 우리 펌웨어 문제처럼 보인다.

### 구분법 — 먼저 드라이브를 본다

```sh
cat /Volumes/NU54V2PRE/ASSERT.TXT    # 있으면 프로브 펌웨어가 HardFault 로 죽은 것
cat /Volumes/NU54V2PRE/DETAILS.TXT   # Target Detect / Target Voltage / Board ID
```

| 증상 | 원인 |
|---|---|
| `ASSERT.TXT` 존재 (`HardFault_Handler.c:67`) | **프로브 펌웨어가 죽었다.** USB 재연결로 복구 |
| pyOCD `SWD/JTAG communication failure (No ACK)` | 프로브가 죽었거나, SW1 `DISABLE_SWD` ON, 또는 **다른 프로브가 같은 SWD 선을 잡고 있음** |
| MSD 드래그앤드롭 후 `Last Flash Result: NONE` | 프로그래밍이 시작조차 안 됐다 (아래 참고) |
| 시리얼·SWD 가 **둘 다** 무응답 | SW1 `DISABLE_SWD`/`DISABLE_UART` 확인 (11_power §4) |

### 확인된 두 가지 문제 (프로브 펌웨어 쪽)

원인 분리까지 끝났다. 제조사 보고서: [reports/2026-09-20_daplink_vcom0_swd.md](reports/2026-09-20_daplink_vcom0_swd.md)

1. **MSD 드라이브에 `.hex` 를 복사하면 즉시 HardFault.** 두 번 재현. 이 경로는 쓰지 않는다.
2. **VCOM0(`uart30`)를 쓰면 같은 프로브의 CMSIS-DAP SWD 가 죽는다.** SMP 와는 무관하다.

| 조건 | SWD |
|---|---|
| uart30 미사용 (led, BLE 전용 dfu) | ✅ |
| uart30 활성, TX/RX 만, 통신 없음 | ✅ |
| 보드 → 호스트 512 B 송신 | ✅ |
| **호스트 → 보드 64 B 송신** | ❌ No ACK |
| **RTS(P0.02)를 Low 로 구동** (통신 없어도) | ❌ No ACK |

→ 프로브의 **VCOM0 송신 경로와 RTS 입력 처리**가 SWD 서비스와 충돌한다.
타깃 문제가 아니다 (외부 프로브는 같은 상태에서 정상). CDC 자체는 SWD 가 죽은 뒤에도 계속 동작한다.

**한 번 VCOM0 를 쓰는 펌웨어가 올라가면 내장 프로브로는 되돌릴 수 없다.** 외부 프로브가 필요하다.

### 프로브가 여러 대일 때

pyOCD 가 번호를 물어보다 스크립트에서 실패한다(`EOF when reading a line`). UID 로 지정한다.

```sh
pyocd list                                   # UID 확인
../../scripts/fw flash --probe <UID>
FW_PROBE=<UID> ../../scripts/fw flash        # 환경변수도 가능
```

**두 프로브의 SWD 를 동시에 물리면 버스가 충돌한다.** 한쪽만 연결한다.

### 프로브 펌웨어 업데이트 경로

1. **MAINTENANCE 모드** — 프로브 리셋을 누른 채 USB 연결 → `MAINTENANCE` 드라이브에 `*_if.hex` 복사 (부트로더가 처리하므로 인터페이스 펌웨어가 죽어 있어도 된다)
2. 평상시 드라이브에 `_if.hex` 복사 — **지금 펌웨어는 hex 복사에서 죽으므로 쓸 수 없다**
3. nRF52840 의 SWD 에 직접 (확실한 경로)

펌웨어 이미지는 nuworks.io 쪽에서 받는다 (드라이브의 `NU54DK.HTM` 이 그리로 간다).
