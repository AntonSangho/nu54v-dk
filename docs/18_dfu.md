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

### 남은 확인

업로드 → test → 리셋 → confirm 흐름을 시리얼(VCOM0)과 BLE 양쪽에서 확인한다.

**3단계 — `dfu` 모듈**

- `hw/driver/dfu.c` + `dfu info / test / confirm / revert / erase` CLI
- `dfu info` 는 `키 : 값` 한 줄 형식, 슬롯은 키에 붙인다 (baram-term 쪽 요청)

```
slot0.version : 1.2.3+4
slot0.hash    : <hex>
slot0.flags   : active,confirmed
slot1.version : 1.2.4+0
slot1.flags   : pending
```

## 7. 호스트 도구

| 도구 | 비고 |
|---|---|
| `mcumgr` (go) / python `smpclient` | 시리얼·BLE 둘 다. 자동 시험은 시리얼(VCOM0) 권장 |
| nRF Connect **Device Manager** (모바일) | Nordic 문서상 nRF Connect **for Desktop 은 FOTA 를 지원하지 않는다** |
| 웹 페이지 | 19 web_dfu — Web Bluetooth / Web Serial / WebUSB(SWD). [roadmap §8](roadmap.md) |
| `fw flash` (pyOCD, SWD) | 초기 설치와 벽돌 복구. 빈 보드는 이것 또는 WebUSB 만 가능하다 |

BLE 로 업데이트할 때는 **다른 프로그램이 BLE 로 붙어 있으면 안 된다** (중앙이 하나뿐이고 연결 중에는 광고를 멈춘다).
baram-term 이 붙어 있으면 `baram-ctl release` → 업데이트 → `resume`.
