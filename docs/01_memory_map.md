# 01. 메모리맵 (nRF54L15)

NU54-DK 모듈(NCRB54N01VC)의 SoC는 **nRF54L15** 이다. 앱 코어(Cortex-M33, `cpuapp`)는 Secure 모드로 동작한다.

값의 출처: `<NCS>/zephyr/dts/vendor/nordic/nrf54l15.dtsi`, `nrf54l_05_10_15.dtsi`, `nrf54l15_cpuapp_partition.dtsi` (**NCS v3.4.1**)

## 1. 전체 주소 공간

| 영역 | 시작 | 끝(미포함) | 크기 | 비고 |
|---|---|---|---|---|
| RRAM (cpuapp) | `0x0000_0000` | `0x0017_D000` | 1524 KB | 앱 코어 코드/데이터 (`cpuapp_rram`) — RRAM 전체 |
| FICR | `0x00FF_C000` | `0x00FF_D000` | 4 KB | 공장 정보 (읽기 전용, MAC, part 등) |
| UICR | `0x00FF_D000` | `0x00FF_E000` | 4 KB | 사용자 설정 (보호, OTP, bl_storage `0xFFD500`) |
| SRAM (cpuapp) | `0x2000_0000` | `0x2003_F000` | 252 KB | 앱 코어 RAM (`cpuapp_sram`) |
| SRAM (보존) | `0x2003_F000` | `0x2004_0000` | 4 KB | 리셋·System OFF 에도 남는 영역 (`retainedmem0`, rtc 가 사용 — [15_rtc](15_rtc.md)) |
| 주변장치 (NS) | `0x4000_0000` | `0x5000_0000` | | Non-Secure 별칭 |
| 주변장치 (S) | `0x5000_0000` | `0x6000_0000` | | 현재 빌드(Secure)는 이 주소 사용 |
| Cortex-M33 PPB | `0xE000_0000` | | | SCS, ITM, DWT 등 |

- RRAM 은 섹터 삭제가 필요 없는 저항성 메모리. 쓰기 단위 16 byte, 삭제 블록 4 KB (드라이버 기준).
- **v3.3.0 과 다른 점**: v3.3.0 은 RISC-V 코프로세서(FLPR)용으로 RRAM 96 KB(`0x165000~`), SRAM 68 KB(`0x2002F000~`)를
  떼어 두어 cpuapp 가 1428 KB / 188 KB 였다. v3.4.1 은 기본으로 전부 cpuapp 에 준다.
  FLPR 를 쓸 때만 `WITH_FLPR_PARTITIONS` 로 나눈다 (로드맵 선택 예제 `flpr`).

## 2. RRAM 파티션 (DTS)

보드 DTS 가 `vendor/nordic/nrf54l15_cpuapp_partition.dtsi` 를 포함한다 (FLPR 없는 기본 배치).

| 라벨 | 노드 | 시작 | 크기 | 용도 |
|---|---|---|---|---|
| mcuboot | `boot_partition` | `0x0000_0000` | 62 KB | MCUboot 사용 시 부트로더 (FPROTECT 단위 때문에 62 KB) |
| image-0 | `slot0_partition` | `0x0001_0000` | 712 KB | MCUboot 사용 시 앱 (primary) |
| image-1 | `slot1_partition` | `0x000C_2000` | 712 KB | MCUboot 사용 시 업데이트 (secondary) |
| storage | `storage_partition` | `0x0017_4000` | 36 KB | NVS/Settings 등 (끝 `0x17D000`) |

## 3. 현재 빌드 배치 (부트로더 없음)

- 부트로더 없음 (`SB_CONFIG_BOOTLOADER_NONE=y`), `CONFIG_USE_DT_CODE_PARTITION=n`
- → 앱은 **`0x0000_0000` 에 링크**되고 `CONFIG_FLASH_LOAD_OFFSET=0`, 가용 크기는 cpuapp RRAM 전체 1524 KB.

| 예제 | FLASH | RAM |
|---|---|---|
| led | 42 KB | 7.4 KB |
| uart | 59 KB | 14.9 KB |
| i2c | 62 KB | 13.7 KB |
| shtc3 | 64 KB | 13.7 KB |

(NCS v3.4.1, `CONFIG_DEBUG_OPTIMIZATIONS=y`)

> **주의**: 부트로더 없이 링크하면 앱 이미지가 파티션 라벨과 무관하게 0x0 부터 채워진다.
> 앱이 `storage_partition`(NVS/Settings)을 쓰게 되면 이미지 크기가 `0x174000`(1488 KB)을 넘지 않는지 확인한다.
> MCUboot 를 도입하면(로드맵 `dfu`) `slot0_partition`(0x10000)에 링크되도록 바뀐다.

## 4. Partition Manager → DTS 파티션

NCS 3.3 부터 Partition Manager(PM, `pm_static.yml`)는 **deprecated** 이다.
보드 패키지의 `Kconfig.sysbuild` 에서 `PARTITION_MANAGER` 기본값을 `n` 으로 바꿔 DTS 파티션만 사용한다.

```kconfig
# firmware/boards/nucode/nu54v_dk/Kconfig.sysbuild
config PARTITION_MANAGER
	default n
```

파티션을 바꾸려면 보드 DTS 에서 `&cpuapp_rram { partitions { ... } }` 를 재정의하거나, 프로젝트별 `app.overlay` 로 덮어쓴다.

## 5. 확인 방법

```sh
# 빌드 후 링크 결과
cat build/<app>/zephyr/zephyr.map
# 최종 DTS (파티션/주소 확인)
cat build/<app>/zephyr/zephyr.dts
# 영역별 사용량 리포트
../../scripts/fw shell   # 이후: west build -d build -t rom_report / ram_report
```
