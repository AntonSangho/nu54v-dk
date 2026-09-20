/*
 * flash_algo.js — nRF54L15 RRAM 플래시 알고리즘
 *
 * pyOCD 의 타깃 정의에서 그대로 가져왔다 (Apache-2.0).
 *   pyocd/target/builtin/target_nRF54L15.py
 * 타깃 RAM 에 올려서 실행한다. RRAM 컨트롤러(0x5004B000)를 다뤄 워드 단위로 쓴다.
 */

const FLASH_ALGO = {
  loadAddress: 0x20000000,
  pcInit: 0x20000015,
  pcUnInit: 0x20000019,
  pcProgramPage: 0x20000065,
  pcEraseSector: 0x20000041,
  pcEraseAll: 0x2000001d,
  staticBase: 0x20000000 + 0x00000004 + 0x000000a0,
  beginStack: 0x20000300,
  // 알고리즘이 쓰는 데이터 버퍼. 알고리즘(240 B)과 스택(0x300)을 피해 잡는다.
  dataBuffer: 0x20002000,
  pageSize: 0x1000,          // RRAM 섹터 = 4 KB (pyOCD 의 FlashRegion blocksize)

  instructions: new Uint32Array([
    0xE00ABE00, 0xf8d24a02, 0x2b013400, 0x4770d1fb, 0x5004b000, 0x47702000, 0x47702000, 0x49072001,
    0xf8c1b508, 0xf7ff0500, 0xf8c1ffed, 0x20000540, 0xffe8f7ff, 0x0500f8c1, 0xbf00bd08, 0x5004b000,
    0x2301b508, 0xf8c14906, 0xf7ff3500, 0xf04fffdb, 0x600333ff, 0xf7ff2000, 0xf8c1ffd5, 0xbd080500,
    0x5004b000, 0x2301b538, 0x4d0c4614, 0x0103f021, 0x3500f8c5, 0xffc6f7ff, 0x44214622, 0x42911b00,
    0x2000d105, 0xffbef7ff, 0x0500f8c5, 0x4613bd38, 0x4b04f853, 0x461a5014, 0xbf00e7f1, 0x5004b000,
    0x00000000,
  ]),
};
