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

## 5. 1단계 검증 결과 (2026-09-20, NCS v3.4.1)

- [x] MCUboot 가 `boot_partition`(0x0)에, 앱이 `slot0`(0x10000)에 링크
- [x] MCUboot 56,152 B → **62 KB 파티션에 들어간다** (FPROTECT 켠 상태)
- [x] `fw flash` 가 두 이미지를 순서대로 굽는다 (mcuboot → dfu)
- [x] MCUboot → 앱 부팅, 모든 모듈 init OK (i2c/shtc3/nvs/rtc/ble/pmic/adc/temp/module)
- [x] **보존 RAM 살아남음** — MCUboot 를 거쳐도 rtc 시각이 유지된다 (MCUboot 도 같은 보드 DTS 를 쓴다)
- [x] `storage_partition` 그대로 → nvs 값과 BLE 본드 유지
- [ ] VS Code 디버깅 (`launch.json` 은 `${workspaceFolderBasename}` 을 쓰므로 경로는 맞는다. 실기 확인 필요)
- [ ] 부팅 시간 측정 (MCUboot 가 서명을 검증하는 시간)

## 6. 다음 (2·3단계 계획)

**2단계 — SMP 서버**

- `CONFIG_NCS_SAMPLE_MCUMGR_BT_OTA_DFU=y` 한 줄이면 BLE 쪽(mcumgr + BT 전송 + img/os 그룹 + 재조립)이 한 번에 켜진다
  (`nrf/samples/common/mcumgr_bt_ota_dfu`). 시리얼은 VCOM0(uart30)에 따로 얹어 CLI(VCOM1)와 충돌하지 않게 한다
- `VERSION` 파일을 두고 버전을 올린다. **버전이 같으면 타깃이 업데이트를 거부한다**
- `_DEF_FIRMWATRE_VERSION` 과 이미지 버전을 하나로 묶는다 (`info` 의 `version : 1.2.3+4` 한 줄로 성공 판정)
- 흐름 : 업로드 → test → 리셋 → 부팅 확인 → confirm (confirm 하지 않으면 되돌아간다)

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
