/*
 * nrf54l.js — nRF54L15 타깃 정보 읽기 (WebUSB + CMSIS-DAP)
 *
 * 주소와 값은 pyOCD 의 타깃 정의에서 가져왔다.
 *   pyocd/target/builtin/target_nRF54L15.py, pyocd/target/family/target_nRF54L.py
 */

const NRF54L = {
  // 메모리 배치
  RRAM_START: 0x00000000,
  RRAM_SIZE: 0x0017D000,        // 1524 KB
  SRAM_START: 0x20000000,
  SRAM_SIZE: 0x00040000,        // 256 KB

  // FICR (읽기 전용 공장 정보). 주소는 pyOCD 의 target_nRF54L.check_part_info() 기준.
  FICR_PARTNO: 0x00FFC31C,
  FICR_VARIANT: 0x00FFC320,
  FICR_DEVICEADDR0: 0x00FFC3A4,
  FICR_DEVICEADDR1: 0x00FFC3A8,

  // DTS 파티션 (firmware/boards/nucode/nu54v_dk)
  BOOT_PARTITION: 0x00000000,
  SLOT0_PARTITION: 0x00010000,
  SLOT1_PARTITION: 0x000C2000,

  MCUBOOT_IMG_MAGIC: 0x96f3b83d,   // MCUboot 이미지 헤더 매직
};

/* 값 하나를 16 진수 문자열로 */
function hex32(v) {
  return "0x" + (v >>> 0).toString(16).padStart(8, "0");
}

/* 보드에 실제로 있는 것을 읽어 화면에 채울 값을 만든다 */
async function readTargetInfo(target) {
  const info = {};

  const partno = await target.readMem32(NRF54L.FICR_PARTNO);
  const variant = await target.readMem32(NRF54L.FICR_VARIANT);
  // variant 는 ASCII 4 글자 (예: "AAAA"). 하위 바이트부터 들어 있다.
  let variantStr = "";
  for (let i = 3; i >= 0; i--) {
    const c = (variant >>> (i * 8)) & 0xff;
    if (c >= 32 && c < 127) variantStr += String.fromCharCode(c);
  }
  info.part = hex32(partno);
  info.partName = `nRF${partno.toString(16).toUpperCase()} ${variantStr}`.trim();

  // BLE MAC 으로도 쓰이는 장치 주소
  const a0 = await target.readMem32(NRF54L.FICR_DEVICEADDR0);
  const a1 = await target.readMem32(NRF54L.FICR_DEVICEADDR1);
  info.deviceId = hex32(a1).slice(2) + hex32(a0).slice(2);

  info.mem = `${NRF54L.RRAM_SIZE / 1024} KB / ${NRF54L.SRAM_SIZE / 1024} KB`;

  // 0x0 의 벡터 테이블 : 초기 SP 와 리셋 벡터
  const sp = await target.readMem32(0x00000000);
  const rv = await target.readMem32(0x00000004);
  info.vector = `SP ${hex32(sp)}, Reset ${hex32(rv)}`;
  info.isBlank = (sp === 0xffffffff && rv === 0xffffffff);

  // slot0 에 MCUboot 이미지 헤더가 있으면 부트로더가 깔린 보드다
  const slot0Magic = await target.readMem32(NRF54L.SLOT0_PARTITION);
  if (info.isBlank) {
    info.boot = "빈 보드 (지워져 있다)";
  } else if (slot0Magic === NRF54L.MCUBOOT_IMG_MAGIC) {
    info.boot = "MCUboot + 앱 (slot0 에 서명된 이미지)";
  } else {
    info.boot = "부트로더 없음 (앱이 0x0 에 직접)";
  }

  return info;
}
