# 18. DFU — MCUboot 펌웨어 업데이트 (`firmware/projects/dfu`)

`ble_nus`(16) 에 MCUboot 부트로더와 SMP 서버를 더한다. 예제는 누적이므로 앞 단계 모듈이 모두 들어 있다.

세 단계로 나눠 진행한다. **1단계 완료**, 2·3단계는 아래 계획.

| 단계 | 내용 | 상태 |
|---|---|---|
| 1 | MCUboot 만 올리기 (메모리맵 이동, 서명, 부팅 확인) | ✅ |
| 2 | SMP 서버 (BLE + 시리얼 VCOM0), `VERSION` | |
| 3 | `hw/driver/dfu.c` + `dfu info/test/confirm/revert` CLI | |

## 1. 결정 사항

| 항목 | 선택 | 이유 |
|---|---|---|
| 업데이트 방식 | **swap using move** (NCS 의 nRF54L 기본값) | 새 이미지가 부팅에 실패하거나 confirm 하지 않으면 이전 버전으로 자동 복귀. BLE 로 원격 업데이트하려면 이 복귀가 필요하다 |
| 서명 | ED25519 + SHA512 (nRF54L 기본), **우리 키를 저장소에 포함** | 다른 PC·세션에서도 바로 업데이트 이미지를 만들 수 있다. 공개 저장소이므로 **개발용 키**이고 양산 시 교체한다 |
| 키 저장 위치 | `firmware/keys/nu54v_dk_ed25519.pem` | |
| 전송 경로 | BLE + 시리얼(VCOM0) 둘 다 | BLE 는 무선 업데이트, 시리얼은 BLE 가 사용자에게 잡혀 있을 때의 자동 시험·복구용 |
| KMU | **쓰지 않는다** | 프로비저닝에 `nrfutil device` / `west ncs-provision` (SWD, J-Link 계열) 이 필요하고, "KMU 를 켠 부트로더를 첫 부팅 전에 프로비저닝하지 않으면 부팅이 안 될 수 있다" 는 경고가 있다. 우리 프로그래머는 CMSIS-DAP 다 |
| Partition Manager | 쓰지 않는다 (DTS 파티션) | NCS v3.4.1 에서 deprecated. 보드 `Kconfig.sysbuild` 가 이미 `PARTITION_MANAGER default n` |

참고한 것 : `nrf/samples/dfu/smp_svr` (v3.4.1, DTS 파티션 방식) 의 설정 골격 +
`nrf54l15-bd/firmware/nrf54l-fw-fota` (v3.1.0, Partition Manager 방식) 가 이 SoC 에서 검증한 한 줄 켜기 패턴.

## 2. 메모리맵이 바뀐다

| | 부트로더 없음 (16 까지) | MCUboot (18 부터) |
|---|---|---|
| `0x000000` | 앱 (RRAM 전체 1524 KB) | **MCUboot** 62 KB (`boot_partition`) |
| `0x010000` | | **앱** = slot0 712 KB (`slot0_partition`) |
| `0x0C2000` | | 업데이트 이미지 = slot1 712 KB (`slot1_partition`) |
| `0x174000` | storage 36 KB | storage 36 KB (그대로 — NVS/본드 유지) |

파티션은 보드 DTS 가 이미 갖고 있어서 새로 만들 것이 없다 ([01_memory_map](01_memory_map.md) §2).

실측 (NCS v3.4.1, `CONFIG_DEBUG_OPTIMIZATIONS=y`)

| 이미지 | 주소 | 크기 | 여유 |
|---|---|---|---|
| MCUboot | `0x0` | **56,152 B** | 62 KB 중 91% 사용 |
| 앱 (서명 후) | `0x10000` | 231,164 B | 712 KB 중 32% 사용 |

앱은 앞에 **0x800 짜리 MCUboot 헤더**가 붙는다 (`CONFIG_ROM_START_OFFSET=0x800`).

## 3. 파일 구성

```
firmware/keys/nu54v_dk_ed25519.pem          서명 키 (개발용)
firmware/projects/dfu/
  sysbuild.conf                             SB_CONFIG_BOOTLOADER_MCUBOOT=y
  sysbuild/mcuboot/prj.conf                 비워 둔다 (아래 §4 함정 1)
  sysbuild/mcuboot/boards/
    nu54v_dk_nrf54l15_cpuapp.conf           MCUboot 이미지 설정
    nu54v_dk_nrf54l15_cpuapp.overlay        code-partition = boot_partition (함정 2)
```

키는 imgtool 로 만든다.

```sh
PY=<NCS 툴체인>/bin/python3
MCUBOOT=<NCS>/bootloader/mcuboot/scripts
PYTHONPATH=$MCUBOOT $PY $MCUBOOT/imgtool.py keygen -k firmware/keys/nu54v_dk_ed25519.pem -t ed25519
```

빌드·다운로드는 평소와 같다. 서명은 빌드가 알아서 한다 (`zephyr.signed.hex`, `dfu_application.zip`).

```sh
cd firmware/projects/dfu
../../scripts/fw build -p
../../scripts/fw flash        # MCUboot → 앱 순서로 두 이미지를 굽는다
```

## 4. 실기에서 걸린 것 (중요)

### 함정 1 — `sysbuild/mcuboot/prj.conf` 는 "대체" 한다

이 폴더가 있으면 MCUboot 이미지의 **애플리케이션 설정 폴더가 통째로 여기로 바뀐다**.
즉 `prj.conf` 를 여기에 두면 MCUboot 자신의 `prj.conf` 가 **무시된다**. 처음에 설정을 여기 넣었더니
`CONFIG_FLASH` 가 사라져서 이렇게 죽었다.

```
warning: FLASH_MAP ... has direct dependencies FLASH_HAS_DRIVER_ENABLED with value n,
         but is currently being y-selected by MCUBOOT_DEVICE_SETTINGS
error: Aborting due to Kconfig warnings
```

`nrf/samples/dfu/smp_svr` 도 `sysbuild/mcuboot/prj.conf` 를 `#empty` 로 두고 내용은
`sysbuild/mcuboot/boards/<보드타깃>.conf` 에 넣는다. **같은 구조를 따른다.**

### 함정 2 — MCUboot 도 slot0 에 링크된다

보드 DTS 가 `zephyr,code-partition = &slot0_partition;` 이므로, 오버레이가 없으면
**MCUboot 까지 0x10000 에 링크되어** 부팅하지 못한다 (hex 첫 레코드가 `:020000021000` = 0x10000).

```dts
/* sysbuild/mcuboot/boards/nu54v_dk_nrf54l15_cpuapp.overlay */
/ {
	chosen {
		zephyr,code-partition = &boot_partition;
	};
};
```

### 함정 3 — 서명 키 경로는 절대 경로여야 한다

앱 이미지와 MCUboot 이미지가 상대 경로를 **서로 다르게** 푼다.

| 이미지 | 상대 경로 기준 |
|---|---|
| 앱 (`nrf/cmake/sysbuild/image_signing.cmake`) | **west topdir** (= NCS 설치 폴더) |
| MCUboot (`bootloader/mcuboot/boot/zephyr/CMakeLists.txt`) | 그 값을 담은 conf 파일의 폴더, 없으면 MCUboot 폴더 |

저장소 위치는 PC 마다 다르므로 `fw` 스크립트가 절대 경로로 만들어 넘긴다
(`signing_key_arg()` → `-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="<절대경로>"`).
Kconfig 문자열이라 **값에 따옴표가 들어가야 한다**. 없으면 이렇게 된다.

```
warning: malformed string literal in assignment to BOOT_SIGNATURE_KEY_FILE. Assignment ignored.
```

MCUboot 를 쓰지 않는 프로젝트(= `sysbuild.conf` 가 없는 프로젝트)에는 넘기지 않는다. 그 심볼 자체가 없기 때문이다.

### 함정 4 — pyOCD 러너의 `--dt-flash=y`

공통 `pyocd.board.cmake` 가 `--dt-flash=y` 를 기본으로 넣는다. 그러면 러너가
`CONFIG_FLASH_LOAD_OFFSET` 을 읽는데, **MCUboot 빌드에서는 이 심볼이 생성되지 않는다**
(`USE_DT_CODE_PARTITION=y` 면 주소가 DT 에서 바로 링커로 간다).

```
KeyError: 'CONFIG_FLASH_LOAD_OFFSET'
```

보드 `board.cmake` 에서 끈다. 우리는 항상 hex 를 굽고 hex 안에 주소가 들어 있으므로 `-a` 가 필요 없다.

```cmake
board_runner_args(pyocd "--dt-flash=n")
```

> 링크 자체는 정상이다. 확인하려면 `build/dfu/zephyr/zephyr.map` 의 `FLASH 0x00010000` 과
> `rom_start 0x00010000` 를 본다.

### 함정 5 — 디버깅할 때 서명 전 elf 를 굽지 않게 한다

cortex-debug 는 `executable` 로 준 `zephyr.elf` 를 타깃에 **그대로 로드**한다.
그런데 그 elf 는 **서명 전** 이미지다 (MCUboot 헤더 자리가 비어 있다).
그대로 F5 를 누르면 slot0 에 서명 없는 이미지가 들어가고, MCUboot 가 검증에 실패해 앱으로 점프하지 않는다.

`.vscode/launch.json` 에서 굽는 일을 cortex-debug 에 맡기지 않는다.

```jsonc
"loadFiles": [],              // cortex-debug 는 아무것도 굽지 않는다
"preLaunchTask": "Flash",     // fw flash 가 MCUboot + 서명된 앱을 굽는다 (빌드도 같이 한다)
```

`executable` 의 elf 는 **심볼용으로만** 쓴다. 주소가 slot0(0x10000)라 심볼은 그대로 맞는다.

### 함정 6 — SMP 시리얼을 켜면 cli 포트가 죽는다 (부팅 직후 USAGE FAULT)

`CONFIG_UART_MCUMGR` 은 `UART_INTERRUPT_DRIVEN` 을 **select** 한다. 그런데 nRF UARTE 의 인스턴스별 설정이

```kconfig
config UART_<n>_INTERRUPT_DRIVEN
	depends on UART_INTERRUPT_DRIVEN
	default y                                  # ← 켜지면 모든 포트가 인터럽트 방식이 된다
config UART_<n>_ASYNC
	depends on UART_ASYNC_API && !UART_<n>_INTERRUPT_DRIVEN
```

이라서, SMP 를 위해 uart30 하나만 인터럽트 방식이 필요한데 **cli 가 쓰는 uart20 까지 넘어간다**.
그러면 드라이버가 async API 를 만들지 않아 `uart_rx_enable()` 이 NULL 이 되고, `uartRxStart()` 에서 PC=0 으로 점프한다.

```
*** Booting My Application v1.0.0 ***
***** USAGE FAULT *****
  Illegal use of the EPSR
r14/lr:  0x000370b1              ← uartRxStart() (uart.c)
Faulting instruction address (r15/pc): 0x00000000
```

포트별로 명시한다 (`conf/dfu_serial.conf`).

```kconfig
CONFIG_UART_20_INTERRUPT_DRIVEN=n     # VCOM1 : cli/log — async(DMA)
CONFIG_UART_30_INTERRUPT_DRIVEN=y     # VCOM0 : SMP — uart_mcumgr
```

다시 밟지 않도록 `uart.c` 에 빌드 시 검사를 넣었다.

```c
BUILD_ASSERT(IS_ENABLED(CONFIG_UART_20_ASYNC),
             "uart20 은 async API 로 써야 한다. CONFIG_UART_20_INTERRUPT_DRIVEN=n 을 넣어라");
```

> 디버깅 요령 : `r14/lr` 을 `arm-zephyr-eabi-addr2line -f -e build/<app>/zephyr/zephyr.elf <주소>` 로 풀면
> 누가 NULL 을 불렀는지 바로 나온다.

### 함정 7 — BLE 가 붙으면 시리얼 SMP 가 33 배 느려진다

`cli_mgr` 은 BLE(NUS)가 붙으면 cli 를 그 채널로 넘긴다. 그리고 입력을 기다릴 때
**그 채널 하나의 세마포어만** 봤다.

```c
if (bleNusIsReady()) cli_ch = HW_UART_CH_BLE;
...
if (cliAvailable() == 0) uartWaitRx(cli_ch, CLI_MGR_IDLE_WAIT_MS);   // ← BLE 채널만 기다린다
```

시리얼로 온 SMP 요청은 BLE 채널을 깨우지 못한다. cli 스레드는 타임아웃이 날 때까지 자고,
그동안 요청은 UART 큐에 그대로 있다. **왕복마다 `CLI_MGR_IDLE_WAIT_MS` 가 통째로 붙는다.**

| | BLE 끊김 | BLE 연결 (고치기 전) | BLE 연결 (고친 뒤) |
|---|---|---|---|
| `image list` 왕복 | 30 ms | **1001 ms** | 29 ms |
| 업로드 | 4.9 KB/s | **0.20 KB/s** | 5.33 KB/s |

증상이 고약한 이유는 **BLE 를 한 번 연결한 뒤부터만** 나타나서, 시리얼 쪽 코드를 아무리 봐도
원인이 없다는 점이다. 보드 안에서 쓴 시간은 1 ms 였다 (`dfu info` 의 `serial.time`).

고친 방식은 계층을 나눴다.

| 계층 | 역할 |
|---|---|
| `uart.c` | 정책을 모른다. 수신 알림 훅만 준다 (`uartSetRxNotify`) |
| `cli.c` | `cliFilterPump(ch)` — **지정한 채널**을 필터로 비운다. 필터가 안 가져간 바이트는 남겨 `cliMain()` 이 처리 |
| `cli_mgr.c` | 중재를 여기서. 자기 세마포어로 어느 채널이든 깨어나고, cli 가 BLE 에 있어도 로컬 포트를 계속 비운다 |

핵심은 **SMP 는 특정 포트에 묶여 있고 cli 의 채널 선택과 무관해야 한다**는 것이다.
필터가 가져간 바이트는 cli 채널을 옮기지 않고, 필터가 거절한 바이트(= 사람이 친 입력)일 때만 옮긴다.

> 이런 종류는 코드만 봐서는 안 잡힌다. `firmware/scripts/dfu_serial_bench.py` 로
> 브라우저 없이 왕복을 재는 것이 결정적이었다 (§9).

## 5. 1단계 검증 결과 (2026-09-20, NCS v3.4.1)

- [x] MCUboot 가 `boot_partition`(0x0)에, 앱이 `slot0`(0x10000)에 링크
- [x] MCUboot 56,152 B → **62 KB 파티션에 들어간다** (FPROTECT 켠 상태)
- [x] `fw flash` 가 두 이미지를 순서대로 굽는다 (mcuboot → dfu)
- [x] MCUboot → 앱 부팅, 모든 모듈 init OK (i2c/shtc3/nvs/rtc/ble/pmic/adc/temp/module)
- [x] **보존 RAM 살아남음** — MCUboot 를 거쳐도 rtc 시각이 유지된다 (MCUboot 도 같은 보드 DTS 를 쓴다)
- [x] `storage_partition` 그대로 → nvs 값과 BLE 본드 유지
- [x] 디버깅 : 리셋 → MCUboot → 앱, `main` 브레이크포인트 적중 (pc `0x3491a`, 소스·스레드 모두 정상)
      단 `loadFiles: []` + `preLaunchTask: Flash` 로 바꿔야 한다 (§4 함정 5)
- [ ] 부팅 시간 측정 (MCUboot 가 서명을 검증하는 시간)

## 6. 2단계 — SMP 서버 (빌드 완료, 실기 확인 남음)

### Kconfig 조각

NCS 샘플의 `CONFIG_NCS_SAMPLE_MCUMGR_BT_OTA_DFU` 한 줄은 그 심볼이 `nrf/samples/common` 아래에만
정의되어 있어 일반 앱에서 쓰기 어렵다. 표준 심볼을 직접 켜고 우리 conf 조각 방식에 맞춘다.

| 파일 | 내용 | hw_def.h |
|---|---|---|
| `conf/dfu.conf` | MCUmgr 공통 + img/os 그룹 + MCUboot 이미지 관리 | `_USE_HW_DFU` |
| `conf/dfu_ble.conf` | SMP over BLE (재조립, 연결 파라미터 제어) | `_USE_HW_DFU_BLE` |
| `conf/dfu_serial.conf` | SMP over 시리얼 (VCOM0), 포트별 API 지정 | `_USE_HW_DFU_SERIAL` |

- SMP Service UUID : `8D53DC1D-1DB7-4CD3-868B-8A527460AA84` (NUS 와 같은 연결 위에 함께 올라간다)
- `MCUMGR_TRANSPORT_NETBUF_SIZE=1230` — MTU 247 기준 권장값 (MTU 498 을 쓰면 2475)
- 시리얼 포트는 `app.overlay` 의 `chosen { zephyr,uart-mcumgr = &uart30; }` 로 정한다

### 버전은 VERSION 파일 하나에서

```
firmware/projects/dfu/VERSION    VERSION_MAJOR/MINOR/PATCHLEVEL/VERSION_TWEAK
        │
        ├→ CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION  (기본값이 $(APP_VERSION_TWEAK_STRING))
        └→ app_version.h 의 APP_VERSION_TWEAK_STRING
                └→ hw_def.h 의 _DEF_FIRMWATRE_VERSION → cli `info` 의 version 줄
```

확인됨 : `CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION="1.0.0+0"`, `APP_VERSION_TWEAK_STRING "1.0.0+0"`.

**업데이트하려면 버전을 올려야 한다.** 같은 버전이면 타깃이 거부한다.

### 크기 (SMP 를 켠 값)

| | 1단계 | 2단계 | 차이 |
|---|---|---|---|
| 앱 FLASH | 231,164 B | 249,336 B | +18 KB |
| 앱 RAM | 53,368 B | 65,888 B | +12 KB |

### 검증 결과 (2026-09-20)

| 항목 | 결과 |
|---|---|
| 부팅, `version : 1.0.1+0` | ✅ VERSION 파일 단일 출처 확인 |
| 시리얼 SMP (VCOM0) `image list` | ✅ |
| BLE SMP `image list` | ✅ (MTU 247) |
| **BLE 업로드 → test → 리셋 → swap → confirm** | ✅ 249 KB / **15.8 초 / 15.4 KB/s** |
| 되돌리기 안전장치 | ✅ swap 직후 `confirmed=False`, 이전 버전이 slot1 에 남음 |

```
업로드 후 : slot0 v1.0.1 active,confirmed   slot1 v1.0.2
test 표시 : slot0 v1.0.1 active             slot1 v1.0.2 pending
리셋 swap : slot0 v1.0.2 active             slot1 v1.0.1 confirmed   ← 아직 확정 아님
confirm   : slot0 v1.0.2 active,confirmed   slot1 v1.0.1
```

시리얼 업로드는 9 KB 근처에서 끊긴다. 내장 프로브(DAPLink)의 CDC 를 통과하는데 그 펌웨어가
불안정하다 ([03_build_debug_env](03_build_debug_env.md) 참고). 외부 USB-UART 로 VCOM0 에
직접 붙여 다시 시험한다.

호스트 도구는 python `smpclient` 를 썼다.

```sh
pip install "smpclient[ble,serial]"
```

## 7. 3단계 — `dfu` 모듈과 시리얼 SMP 재설계 (완료)

### 시리얼 SMP 를 cli 포트(VCOM1) 위로 옮겼다

2단계에서는 Zephyr 의 `MCUMGR_TRANSPORT_UART` 로 VCOM0(uart30)를 썼는데 두 가지가 걸렸다.

1. 이 보드의 내장 프로브는 **VCOM0 를 쓰면 SWD 가 죽는다**
   ([reports/2026-09-20_daplink_vcom0_swd.md](reports/2026-09-20_daplink_vcom0_swd.md))
2. `UART_MCUMGR` 이 `UART_INTERRUPT_DRIVEN` 을 select 해서 cli 포트까지 async 가 아니게 된다 (§4 함정 6)

그래서 **전송 계층만 직접 만들어 cli 가 쓰는 포트에 얹었다** (`hw/driver/dfu/dfu_serial.c`).
BLE 에서 NUS 와 SMP 가 한 연결 위에 공존하는 것과 같은 방식이다.

```
uart(VCOM1) → cliMain() → dfuSerialRxByte()
                             │ 0x06 0x09 (또는 0x04 0x14) 로 시작하면 SMP 가 가져간다
                             │ 아니면 false → cli 가 처리
                             ↓
                   mcumgr_serial_process_frag()      base64 + CRC16 해독
                             ↓
                       smp_rx_req() → mcumgr
                             ↓
                   mcumgr_serial_tx_pkt() → uartWrite()
```

- 프레이밍은 표준이라 `mcumgr` / `smpclient` 가 그대로 붙는다. **포트 하나로 cli 와 DFU 가 공존한다.**
- 프레임 표식(`0x06`/`0x04`)이 아닌 바이트는 cli 로 넘어간다. 표식 뒤가 어긋나면 그 바이트는 버린다
  (cli 입력으로 쓰는 문자가 아니다).
- **cli 는 dfu 를 알지 못한다.** cli 가 수신 필터 자리만 내주고, dfu 가 자기를 등록한다
  (`uartSetDriver()` 와 같은 방식).

  ```c
  // cli.h — 필터 자리만 있다. 주석에도 dfu 는 나오지 않는다
  typedef bool (*cli_rx_filter_t)(uint8_t ch, uint8_t rx_data);
  bool cliSetRxFilter(cli_rx_filter_t filter);

  // dfu_serial.c — 자기를 등록한다
  cliSetRxFilter(dfuSerialRxByte);
  ```
- `dfu serial off` 로 가로채기를 끌 수 있다.
- 프레이밍 헬퍼(`serial_util.c`)는 숨은 심볼로 빌드되므로 프로젝트 `Kconfig` 에서 select 한다
  (`CONFIG_NU54_DFU_SMP_UART`).

**VCOM0 는 더 이상 쓰지 않는다.** `app.overlay` 의 `zephyr,uart-mcumgr` 설정도 뺐다.

### 버스트를 받아내기 위한 두 가지

| 항목 | 값 | 이유 |
|---|---|---|
| `cliMain()` 에서 프레임 수신 중 드레인 | `dfu_serial.c` | cliMain 은 한 번에 한 바이트만 처리한다. 그대로 두면 115200 bps 연속 수신을 못 따라가 업로드가 3.5 KB 에서 멈췄다 |
| `UART_RX_BUF_LEN` | 1024 → **4096** | 한 패킷이 여러 줄로 연속해서 들어온다 (1218 B 패킷 → base64 약 1640 B) |

그래도 호스트가 기본 프레임 크기로 밀어넣으면 26 KB 부근에서 멈춘다.
**호스트에서 프레임을 512 로 줄이면 끝까지 올라간다.**

```python
SMPSerialTransport(max_smp_encoded_frame_size=512)
```

### `dfu` CLI

출력은 `키 : 값` 한 줄 형식이다 (baram-term 쪽 요청 — 자동 시험에서 파싱한다).

```
dfu info      슬롯별 버전·플래그, 시리얼 SMP 상태
dfu test      slot1 을 다음 부팅에 시도 (확정하지 않는다)
dfu confirm   실행 중인 이미지를 확정 (되돌리기 취소)
dfu revert    이전 이미지로 되돌리기
dfu erase     slot1 지우기
dfu serial on:off
```

### 3단계 검증 결과 (2026-09-20)

시리얼(VCOM1) 업로드 → test → 리셋 → swap → `dfu confirm` 까지 실기 확인했다.

```
업로드      : 249 KB / 37.3 초 / 6.6 KB/s  (프레임 512)
리셋 후     : version : 1.0.2+0
dfu info    : slot0.flags : active,test      ← 확정 전
              slot1.version : 1.0.1+0        ← 이전 버전 백업
dfu confirm : OK
dfu info    : slot0.flags : active,confirmed
```

- [x] cli 와 SMP 가 같은 포트에서 공존 (`dfu info` 의 `serial.pkt` 로 확인)
- [x] **내장 프로브 SWD 가 살아 있다** — VCOM0 를 쓰지 않으므로
- [ ] `slot1.flags` 의 pending 표시 (MCUboot 의 swap state 는 공개 API 가 없다. mcumgr `image list` 로 본다)
- [ ] 해시 표시 (같은 이유. 필요하면 mcumgr 쪽을 쓴다)

## 8. 업데이트하기 — `fw dfu` / VS Code 태스크

```sh
cd firmware/projects/dfu
../../scripts/fw dfu                      # 시리얼(cli 포트). 포트가 여럿이면 --port 로 지정
../../scripts/fw dfu --transport ble      # BLE
../../scripts/fw dfu --no-confirm         # 확정하지 않는다 (리셋하면 이전 버전으로 복귀)
```

한 번에 **업로드 → test 표시 → 리셋 → 다시 연결 → confirm** 까지 한다.

### 무엇이 올라가나 — `fw flash` 와 `fw dfu` 는 다르다

| | 굽는 것 | 경로 | 쓰는 때 |
|---|---|---|---|
| `fw flash` | **MCUboot + 앱** (두 이미지) | SWD (프로브) | 빈 보드, 부트로더 변경, 복구 |
| `fw dfu` | **앱만** (`zephyr.signed.bin` → slot1) | SMP (시리얼 / BLE) | 평소 업데이트 |

**DFU 로는 MCUboot 자신을 바꿀 수 없다.** 부트로더가 자기를 덮어쓰는 셈이기 때문이다.
서명 키나 파티션을 바꿨다면 `fw flash` 로 다시 구워야 한다.

빌드하면 **`build/merged.hex`** (MCUboot + 앱) 도 함께 만든다. 파일 하나만 받는 곳
(웹 도구, 드래그앤드롭)에서 쓴다.

> **MCUboot 만 따로 굽지 않는다.** 부팅할 앱이 없으면 MCUboot 가 이미지를 못 찾고 멈추는데,
> 이 보드는 타깃 펌웨어가 그런 상태(패닉·폴트)에 빠지면 **SWD 접근까지 막힌다**.
> 빠져나오려면 CTRL-AP 전체 삭제가 필요하다 (`pyocd erase --mass`, 또는 웹 도구의 [전체 삭제]).

VS Code 태스크 (모두 빌드를 먼저 한다)

| 태스크 | 굽는 것 | 포트 |
|---|---|---|
| **Flash (SWD, MCUboot + 앱)** | 둘 다 | 프로브 |
| **DFU (serial)** | 앱만 | 자동 탐색 |
| **DFU (serial, 포트 선택)** | 앱만 | 목록에서 고른다 |
| **DFU (BLE)** | 앱만 | — |

- 호스트 도구는 `firmware/.tools/venv` 에 처음 한 번만 설치한다 (`smpclient`). SDK 툴체인은 건드리지 않는다
- 버전은 **올리지 않아도 된다**. `MCUBOOT_DOWNGRADE_PREVENTION` 이 꺼져 있어 같은 버전도 올라간다.
  막히는 경우는 둘이다 (실측) — `rc=6` 실행 중 이미지가 확정 전(먼저 `dfu confirm`),
  `rc=1` slot1 이 slot0 과 완전히 같은 이미지
- 시리얼 프레임 크기는 512 로 고정해서 보낸다 (기본값이면 26 KB 부근에서 멈춘다)

> **주의 — 포트를 쓰는 프로그램을 먼저 닫는다.**
> SMP 가 cli 포트에 얹혀 있으므로, baram-term 등이 그 포트를 열고 있으면 두 프로세스가 같은 포트를 읽게 되어
> SMP 응답을 나눠 가진다. baram-term 이면 `baram-ctl release` → 업데이트 → `baram-ctl resume`.

실측 : 시리얼 249 KB / 44 초, BLE 249 KB / 15.8 초.

### 조각 크기는 전송 계층마다 다르다 (BLE 160, 시리얼 512)

한 값으로 묶으면 한쪽이 깨진다. 실제로 시리얼을 맞추다 BLE 를 망가뜨렸다.

| | 조각 | 패킷 상한 | 왜 |
|---|---|---|---|
| **BLE** | 160 | **241** (MTU 244 − 3) | 패킷 하나가 **한 번의 write** 에 통째로 들어가야 한다 |
| **시리얼** | 512 | 1222 (net_buf 1230 − 8) | 줄로 쪼개 보내고 보드가 다시 잇는다 |

**첫 요청만 `len` 과 `sha`(32 B)를 같이 실어 약 75 바이트 커진다.** 이것을 잊으면

```
조각 160 → 첫 패킷 230 B  ✓        조각 200 → 첫 패킷 275 B  ✗ (241 초과)
         평소   183 B                       평소   232 B  ✓
```

평소 조각은 멀쩡한데 **첫 조각에서만** 멈춘다. 넘치면 브라우저가 잘라 보내고
보드는 불완전한 패킷을 기다리기만 하므로 **오류 없이 "응답이 오지 않는다"** 로만 보인다.
`upload()` 는 첫 조각을 `chunkSize - 96` 으로 줄여 이 여유를 만든다.

> Web Bluetooth 는 MTU 를 알려주지 않는다. 그래서 BLE 상한은 보드에서 협상되는
> 값(`ble info` → `mtu`)을 코드에 적어 둔 것이다. 보드 BLE 설정을 바꾸면 여기도 바꾼다.

### 보드 없이 도는 회귀 시험

```sh
bun run web/selftest.js      # node 도 된다
```

- 두 전송 계층의 **첫 패킷·평소 패킷**이 각자의 한계 안인지
- 시리얼 프레이밍 왕복 (한 덩어리 / 프롬프트가 앞에 붙은 경우 / 1 바이트씩)
- **이 시험이 실제로 회귀를 잡는지** — 조각 512 와 "첫 요청을 안 줄인 200" 이
  한계를 넘는 것을 함께 확인한다. 통과만 하는 시험은 의미가 없다

한쪽 전송 계층을 만질 때 이것부터 돌린다. 조각 크기는 둘이 공유하는 `upload()` 를
지나므로 한쪽만 보고 고치면 다른 쪽이 조용히 깨진다.

### 조각 크기는 512 다 — 더 키우면 오히려 느려진다

`web/js/smp.js` 의 `chunkSize`, `dfu_serial_bench.py` 의 `--chunk` 기본값이 512 다.
실측으로 고른 값이다 (115200 baud, 조각 왕복 = 요청 전선 + 보드 처리 + 응답 전선).

| 조각 | 처리율 | 왕복 분해 |
|---|---|---|
| 200 | 5.01 KB/s | 36.9 ms = rx 24 + proc 1.1 + 고정비 12 |
| **512** | **5.96 KB/s** | 78.5 ms = rx 61 + proc 5.0 + 고정비 12 |
| 1024 | — | **한 번에 쓰면 잃는다** (아래) |

왕복마다 붙는 **고정비 약 12 ms** 가 조각 크기와 무관해서, 조각을 키우면 그만큼 이득이다.
그런데 1024 부터는 프로브가 못 버틴다.

> **프로브의 버스트 한계.** 호스트는 USB 로 1500 바이트를 한순간에 넘기는데,
> 프로브는 그것을 115200 으로 127 ms 동안 흘려보낸다. **흐름제어가 없어**
> 내부 버퍼가 넘치면 조용히 버리거나 덮어쓴다.
>
> ```
> 조각 1024  한 번에         : 성공  3/25
> 조각 1024  줄마다 10 ms 쉼  : 성공  0/25    (10×12=120 ms < 전선 127 ms)
> 조각 1024  줄마다 12 ms 쉼  : 성공 25/25    (12×12=144 ms > 전선 127 ms)
> 조각  768  한 번에         : 성공 25/25
> ```
>
> 크기가 아니라 **타이밍**이다 (992 는 6/15, 1024 가 15/15 로 나온 적도 있다).
> 보드는 `rx drop 0, rx stop 0` 으로 멀쩡하다 — **보드에 닿기 전에** 사라진 것이다.

1024 를 안전하게 쓰려면 전선 속도에 맞춰 나눠 써야 하는데, 그러면 전선을 못 채워
**4.9 KB/s 로 더 느려진다.** 512 는 한 번에 쓰는 양이 약 790 바이트라 프로브가 견딘다.

### 페이싱 — 전선이 비워내는 속도에 맞춰 쓴다

512 로 줄여도 **약 7,000 프레임에 1회** 손상이 남았다. 두 가지 모습으로 나온다.

| `dfu info` 의 `serial.drop2` | 뜻 |
|---|---|
| `nb_len 538, pkt_len 547` — 길이는 딱 맞는데 버렸다 | **바이트가 바뀌었다** (CRC 불일치) |
| `nb_len -1` — 모으던 것이 없다 | 한 줄이 **통째로 사라졌다** |

둘 다 프로브가 버스트를 감당 못 할 때의 모습이고, 보드는 두 경우 모두
`rx drop 0, rx stop 0` 이다. 그래서 **쓰는 속도를 전선에 맞춘다**.

```
페이싱 없음:  [790 B 한 번에] ........... 69 ms 동안 프로브가 들고 있어야 한다
페이싱 있음:  [256] 22ms [256] 22ms [256]  프로브에 항상 256 B 이하만 쌓인다
```

`drainAt`(지금까지 쓴 것이 다 나갈 시각)을 따라가며 `SERIAL_PACE_AHEAD` 바이트 이상
앞서지 않게 한다 (`web/js/serial.js`, `dfu_serial_bench.py --ahead`).

| | 프레임 | 손상 |
|---|---|---|
| 페이싱 없음 (`dfu_serial_bench.py`) | 13,235 | **2** |
| 페이싱 있음 (`dfu_serial_bench.py`) | 17,543 | **0** |

**전선이 어차피 병목이라 속도 손해는 없다** (5.51 → 5.55 KB/s).

> **브라우저에서는 같은 효과가 나오지 않는다 (미해결).**
> 위 수치는 파이썬(pyserial) 기준이다. 웹페이지에 같은 페이싱을 넣었는데도
> 재전송 빈도는 그대로다 (약 7,000 프레임에 2 회, 손상 모습도 `nb_len 538,
> pkt_len 547` 로 동일).
>
> 확인된 사실은 여기까지다. `writer.write()` 는 Chrome 내부로 넘기는 것까지만
> 보장하고 장치에 언제 밀어 넣을지는 브라우저가 정하므로, JS 에서 나눠 써도
> 실제 USB 전송은 뭉쳐 나갈 수 있다 — 다만 **이것은 아직 확인하지 않은 추정**이다.
>
> 실제 영향은 작다. 재전송 대기가 0.6 초라 43.8 초 업로드에서 2 회면 **1.2 초(2.7 %)** 다.

> 원리는 1024 시험이 그대로 보여 준다. 줄마다 10 ms 쉬면(합 120 ms) 전선(127 ms)보다
> 여전히 빨라서 0/25 이고, 12 ms 쉬면(합 144 ms) 전선보다 느려서 25/25 다.

**전제 — 보率을 올릴 때 이것이 깨질 수 있다.** 지금 페이싱은 *보率로 계산한 전선 속도*를
기준으로 삼는다. 115200 에서 이게 안전한 이유는 **전선이 사슬에서 가장 느린 고리**이기
때문이다 (호스트 USB 12 Mbps → 프로브 → 전선 115200). 전선 속도에 맞추면 프로브는
저절로 여유가 생긴다.

1 Mbaud 로 올리면 전선이 100 kB/s 가 되어 **프로브의 USB→UART 처리가 가장 느린 고리로
바뀔 수 있다.** 그러면 보率로 계산한 기준은 더 이상 보수적이지 않고, 페이싱이 켜져
있는데도 손상이 나기 시작한다. 그때의 순서는 이렇다.

1. 페이싱을 **끄고** 손상률부터 본다. 프로브가 견디면 그대로 끝이다
2. 견디지 못하면 기준 속도를 보率이 아니라 **손상 0 이 나오는 실측 바이트/초**로 바꾸고
   이분 탐색한다 (`--ahead` 와 기준 속도만 갈아 끼우면 된다)

조각 1024 시험의 "144 ms 쉼 > 전선 127 ms" 가 이미 그 성격이었다 — 그때는 **전선보다도
느리게** 보내야 했고, 그것이 프로브가 전선보다 느릴 수 있다는 첫 증거다.

### 시리얼이 느릴 때 — 먼저 재고 고친다

`firmware/scripts/dfu_serial_bench.py` 는 웹페이지(`web/js/serial.js`)와 **같은 프레이밍**으로
브라우저 없이 왕복을 잰다. "브라우저 탓인가 보드 탓인가" 를 한 번에 가른다.

```sh
python3 scripts/dfu_serial_bench.py ping -n 40             # 플래시를 안 건드리는 순수 왕복
python3 scripts/dfu_serial_bench.py upload <파일> -n 150   # 조각 150 개만 올려 분포를 본다
python3 scripts/dfu_serial_bench.py cli "dfu info"         # 보드 계수기
```

읽는 법 (실측 기준값, 115200 baud · 조각 200 바이트)

| 조각 왕복 | 뜻 |
|---|---|
| **30~45 ms** | 정상. 요청 325 B + 응답 35 B 를 선에 싣는 물리 시간이다 |
| **1000 ms 배수** | 타임아웃이다. 지연이 아니라 누가 자고 있다 (§4 함정 7) |
| **평균은 작고 최대만 3200 ms** | 호스트 재전송이다. 조각이 유실되고 있다 |

보드 쪽은 `dfu info` 가 구간을 나눠 준다.

```
serial.pkt    : rx 1666, tx 1666, err 0
serial.frag   : 4820, drop 0        ← drop 이 오르면 줄은 왔는데 패킷이 안 됐다
serial.time   : rx 22/28 ms, proc 4/3057 ms   (평균/최대)
```

`rx` 와 `proc` 이 둘 다 작은데 호스트 왕복이 크면 **지연은 보드가 첫 바이트를 꺼내기 전**에 있다.
`uart info` 의 `rx drop` / `rx stop` 이 0 이면 바이트를 잃은 것도 아니다 — 그러면 남는 것은
cli 스레드가 깨어나지 못한 경우뿐이다.

## 9. 호스트 도구

| 도구 | 비고 |
|---|---|
| `mcumgr` (go) / python `smpclient` | 시리얼·BLE 둘 다. 자동 시험은 시리얼(VCOM0) 권장 |
| nRF Connect **Device Manager** (모바일) | Nordic 문서상 nRF Connect **for Desktop 은 FOTA 를 지원하지 않는다** |
| 웹 페이지 | 19 web_dfu — Web Bluetooth / Web Serial / WebUSB(SWD). [roadmap §8](roadmap.md) |
| `fw flash` (pyOCD, SWD) | 초기 설치와 벽돌 복구. 빈 보드는 이것 또는 WebUSB 만 가능하다 |

BLE 로 업데이트할 때는 **다른 프로그램이 BLE 로 붙어 있으면 안 된다** (중앙이 하나뿐이고 연결 중에는 광고를 멈춘다).
baram-term 이 붙어 있으면 `baram-ctl release` → 업데이트 → `resume`.
