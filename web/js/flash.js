/*
 * flash.js — RRAM 굽기 (Intel HEX 읽기 + 플래시 알고리즘 실행)
 *
 * pyOCD 와 같은 방식이다.
 *   1. 알고리즘을 타깃 RAM(0x20000000)에 올린다
 *   2. Init(주소, 클럭, 동작) 을 부른다
 *   3. 섹터(4 KB)마다 EraseSector → 데이터를 RAM 버퍼에 쓰고 ProgramPage
 *   4. UnInit
 *
 * dapjs 의 CortexM.execute() 는 쓰지 않는다. 두 가지가 걸린다.
 *   - 호출마다 알고리즘 전체를 다시 올린다
 *   - waitDelay(fn, timeout, interval) 이 interval 대신 timeout 만큼 잠든다 (dapjs 2.3.0 버그)
 *     → 호출 한 번에 최대 10 초가 걸린다
 * 그래서 레지스터를 직접 채우고 짧은 간격으로 halt 를 확인한다. 알고리즘은 한 번만 올린다.
 */

const FLASH_OP = { ERASE: 1, PROGRAM: 2, VERIFY: 3 };

// Cortex-M 디버그 레지스터
const DHCSR = 0xe000edf0;       // 상태/제어
const DEMCR = 0xe000edfc;       // 벡터 캐치
const AIRCR = 0xe000ed0c;       // 리셋 요청
const DBGKEY = 0xa05f0000;
const C_DEBUGEN = 1 << 0;
const C_HALT = 1 << 1;
const S_HALT = 1 << 17;
const VC_CORERESET = 1 << 0;
const SYSRESETREQ = 0x05fa0004;

/*
 * nRF54L 전용 CTRL-AP (AP #2).
 *
 * AIRCR.SYSRESETREQ 나 핀 리셋이 아니라 이쪽으로 리셋해야 한다.
 * pyOCD 도 같은 방식이다 (Nordic nWP-027). 코어를 거치지 않으므로
 * 펌웨어가 멈춰 있거나 디버그 상태가 꼬여도 통한다.
 *
 * AP 주소는 비트 31:24 에 APSEL 이 들어간다.
 */
const CTRL_AP = 0x02000000;
const CTRL_AP_RESET = 0x000;
const CTRL_AP_ERASEALL = 0x004;
const CTRL_AP_ERASEALLSTATUS = 0x008;


/* Intel HEX 를 {주소: Uint8Array} 구간 목록으로 바꾼다 */
function parseIntelHex(text) {
  const chunks = [];
  let base = 0;
  let cur = null;

  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (line.length === 0 || line[0] !== ":") continue;

    const bytes = [];
    for (let i = 1; i + 1 < line.length; i += 2) {
      bytes.push(parseInt(line.substr(i, 2), 16));
    }
    const len = bytes[0];
    const offset = (bytes[1] << 8) | bytes[2];
    const type = bytes[3];
    const data = bytes.slice(4, 4 + len);

    if (type === 0x00) {                       // 데이터
      const addr = base + offset;
      if (cur && cur.address + cur.data.length === addr) {
        cur.data.push(...data);                // 이어지는 구간이면 붙인다
      } else {
        cur = { address: addr, data: [...data] };
        chunks.push(cur);
      }
    } else if (type === 0x04) {                // 확장 선형 주소
      base = ((data[0] << 8) | data[1]) << 16;
      cur = null;
    } else if (type === 0x02) {                // 확장 세그먼트 주소
      base = ((data[0] << 8) | data[1]) << 4;
      cur = null;
    } else if (type === 0x01) {                // 끝
      break;
    }
  }

  return chunks.map((c) => ({ address: c.address, data: new Uint8Array(c.data) }));
}


/* 구간들을 4 KB 페이지 단위로 자른다. 빈 곳은 0xFF 로 채운다. */
function toPages(chunks, pageSize) {
  const pages = new Map();

  for (const chunk of chunks) {
    for (let i = 0; i < chunk.data.length; i++) {
      const addr = chunk.address + i;
      const pageAddr = addr - (addr % pageSize);
      if (!pages.has(pageAddr)) {
        pages.set(pageAddr, new Uint8Array(pageSize).fill(0xff));
      }
      pages.get(pageAddr)[addr - pageAddr] = chunk.data[i];
    }
  }

  return [...pages.entries()]
    .sort((a, b) => a[0] - b[0])
    .map(([address, data]) => ({ address, data }));
}


/*
 * dapjs 가 캐시해 둔 SELECT / CSW 를 버린다.
 *
 * dapjs 는 같은 값을 다시 쓰지 않으려고 이 둘을 기억해 둔다 (writeDPCommand, writeAPCommand).
 * 그런데 타깃을 리셋하면 하드웨어의 DP/AP 레지스터는 초기화되는데 캐시는 남는다.
 * 그러면 이후 전송이 엉뚱한 AP·뱅크로 가서 FAULT 가 난다. connect() 도 이것을 지우지 않는다.
 */
function invalidateDapCache(target) {
  target.selectedAddress = undefined;
  target.cswValue = undefined;
}


class Flasher {
  constructor(target, log) {
    this.target = target;
    this.log = log || (() => {});
  }

  /*
   * 리셋한 뒤 첫 명령에서 멈춘다.
   *
   * 실행 중인 앱 위에서 알고리즘을 돌리면 인터럽트(BLE, 타이머)가 끼어들어
   * BKPT 까지 오지 못한다. pyOCD 도 리셋 후 halt 상태에서 돌린다.
   */
  async resetHalt() {
    invalidateDapCache(this.target);
    try {
      await this.target.clearAbort();
    } catch (e) { /* 무시 */ }

    await this.target.writeMem32(DHCSR, DBGKEY | C_DEBUGEN | C_HALT);

    const demcr = await this.target.readMem32(DEMCR);
    await this.target.writeMem32(DEMCR, demcr | VC_CORERESET);   // 리셋 벡터 캐치
    await this.target.writeMem32(AIRCR, SYSRESETREQ);

    for (let i = 0; i < 100; i++) {
      await new Promise((r) => setTimeout(r, 20));
      try {
        const status = await this.target.readMem32(DHCSR);
        if (status & S_HALT) {
          await this.target.writeMem32(DEMCR, demcr);             // 원래대로
          return;
        }
      } catch (e) {
        invalidateDapCache(this.target);   // 리셋 도중에는 읽기가 실패할 수 있다
      }
    }
    throw new Error("리셋 후 halt 되지 않았다");
  }

  /*
   * 리셋하고 코어를 풀어 준다 (디버그 정지 해제).
   *
   * 굽기가 끝나면 이것을 해야 펌웨어가 실제로 돈다. 그냥 두면 코어가 halt 인 채라
   * 시리얼도 안 나오고 프로브 쪽도 이상해 보인다.
   */
  async resetRun() {
    const demcr = await this.target.readMem32(DEMCR);
    await this.target.writeMem32(DEMCR, demcr & ~VC_CORERESET);   // 벡터 캐치 해제

    // 먼저 코어를 풀어 준다 (C_HALT = 0, C_DEBUGEN 은 그대로).
    //
    // halt 상태에서 C_DEBUGEN 을 0 으로 쓰면 ARM 사양상 동작이 정의되지 않는다.
    // 코어가 그대로 굳어 펌웨어가 돌지 않는다.
    await this.target.writeMem32(DHCSR, DBGKEY | C_DEBUGEN);

    // CTRL-AP 리셋이 DHCSR 까지 기본값으로 되돌린다 (디버그 정지 해제).
    await this.ctrlApReset();
  }

  /* CTRL-AP 로 리셋한다 (pyOCD 와 같은 값) */
  async ctrlApReset() {
    invalidateDapCache(this.target);
    await this.target.writeAP(CTRL_AP | CTRL_AP_RESET, 2);
    await this.target.writeAP(CTRL_AP | CTRL_AP_RESET, 0);
    await new Promise((r) => setTimeout(r, 300));
    invalidateDapCache(this.target);              // 리셋으로 하드웨어가 초기화됐다
  }

  /*
   * 칩 전체를 지운다 (CTRL-AP ERASEALL).
   *
   * 코어를 거치지 않아서, 디버그 접근이 막힌 상태("No cores were discovered")에서
   * 빠져나오는 확실한 방법이다. UICR 까지 지워진다.
   */
  async massErase() {
    invalidateDapCache(this.target);
    await this.target.writeAP(CTRL_AP | CTRL_AP_ERASEALL, 1);

    const deadline = Date.now() + 30000;
    for (;;) {
      const status = await this.target.readAP(CTRL_AP | CTRL_AP_ERASEALLSTATUS);
      if (status === 0 || status === 1) break;       // READY / READYTORESET
      if (Date.now() > deadline) throw new Error("전체 삭제가 끝나지 않는다");
      await new Promise((r) => setTimeout(r, 100));
    }
    await this.ctrlApReset();
  }

  /* 알고리즘을 타깃 RAM 에 올린다 (한 번만) */
  async loadAlgo() {
    await this.target.writeBlock(FLASH_ALGO.loadAddress, FLASH_ALGO.instructions);
  }

  /*
   * 알고리즘 함수를 부르고 r0(반환값)를 읽는다.
   *
   * 레지스터 규약은 pyOCD 와 같다.
   *   pc = 함수, r0~r3 = 인자, r9 = static base, sp = 스택,
   *   lr = 알고리즘 첫 워드(BKPT) + 1, xpsr = Thumb
   */
  async call(pc, r0 = 0, r1 = 0, r2 = 0, r3 = 0) {
    const t = this.target;

    await t.halt();
    await t.transferSequence([
      t.writeCoreRegisterCommand(0, r0),
      t.writeCoreRegisterCommand(1, r1),
      t.writeCoreRegisterCommand(2, r2),
      t.writeCoreRegisterCommand(3, r3),
      t.writeCoreRegisterCommand(9, FLASH_ALGO.staticBase),
      t.writeCoreRegisterCommand(13, FLASH_ALGO.beginStack),
      t.writeCoreRegisterCommand(14, FLASH_ALGO.loadAddress + 1),
      t.writeCoreRegisterCommand(15, pc),
      t.writeCoreRegisterCommand(16, 0x01000000),          // xpsr : Thumb
    ]);
    await t.resume(false);

    // 짧은 간격으로 멈췄는지 본다 (dapjs 의 waitDelay 는 쓰지 않는다)
    const deadline = Date.now() + 10000;
    for (;;) {
      if (await t.isHalted()) break;
      if (Date.now() > deadline) throw new Error(`알고리즘이 멈추지 않았다 (pc=${hex32(pc)})`);
      await new Promise((r) => setTimeout(r, 2));
    }

    return await t.readCoreRegister(0);
  }

  async init(op) {
    const ret = await this.call(FLASH_ALGO.pcInit, 0, 0, op);
    if (ret !== 0) throw new Error(`플래시 Init 실패 (${ret})`);
  }

  async unInit(op) {
    await this.call(FLASH_ALGO.pcUnInit, op);
  }

  async eraseSector(address) {
    const ret = await this.call(FLASH_ALGO.pcEraseSector, address);
    if (ret !== 0) throw new Error(`섹터 지우기 실패 @${hex32(address)} (${ret})`);
  }

  async programPage(address, data) {
    // 데이터를 먼저 RAM 버퍼에 올린다 (워드 단위)
    const words = new Uint32Array(data.buffer, data.byteOffset, data.length / 4);
    await this.target.writeBlock(FLASH_ALGO.dataBuffer, words);

    const ret = await this.call(
      FLASH_ALGO.pcProgramPage, address, data.length, FLASH_ALGO.dataBuffer);
    if (ret !== 0) throw new Error(`쓰기 실패 @${hex32(address)} (${ret})`);
  }

  /*
   * 쓴 내용을 다시 읽어 확인한다.
   *
   * 페이지마다 앞 16 바이트만 본다. 알고리즘이 우리가 넘긴 길이만큼 제대로 쓰는지,
   * 주소가 맞는지 확인하는 것이 목적이다 (전체 비교는 느리다).
   */
  async verify(pages) {
    for (const page of pages) {
      const expect = new Uint32Array(page.data.buffer, page.data.byteOffset, 4);
      const actual = await this.target.readBlock(page.address, 4);

      for (let i = 0; i < 4; i++) {
        if ((actual[i] >>> 0) !== (expect[i] >>> 0)) {
          throw new Error(
            `확인 실패 @${hex32(page.address + i * 4)} : ` +
            `쓴 값 ${hex32(expect[i])}, 읽은 값 ${hex32(actual[i])}`);
        }
      }
    }
  }

  /*
   * Intel HEX 한 개를 굽는다.
   * onProgress(완료바이트, 전체바이트) 로 진행률을 알린다.
   */
  async program(hexText, onProgress) {
    const chunks = parseIntelHex(hexText);
    if (chunks.length === 0) throw new Error("HEX 에서 데이터를 찾지 못했다");

    const pages = toPages(chunks, FLASH_ALGO.pageSize);
    const total = pages.length * FLASH_ALGO.pageSize;

    const lo = pages[0].address;
    const hi = pages[pages.length - 1].address + FLASH_ALGO.pageSize;
    this.log(`구간 ${hex32(lo)} ~ ${hex32(hi)}, ${pages.length} 페이지 (${(total / 1024).toFixed(0)} KB)`);

    this.log("리셋 후 halt");
    await this.resetHalt();

    this.log("알고리즘 적재");
    await this.loadAlgo();

    await this.init(FLASH_OP.ERASE);
    for (const page of pages) {
      await this.eraseSector(page.address);
    }
    await this.unInit(FLASH_OP.ERASE);

    await this.init(FLASH_OP.PROGRAM);
    let done = 0;
    for (const page of pages) {
      await this.programPage(page.address, page.data);
      done += page.data.length;
      if (onProgress) onProgress(done, total);
    }
    await this.unInit(FLASH_OP.PROGRAM);

    this.log("쓴 내용 확인");
    await this.verify(pages);

    return { pages: pages.length, bytes: total };
  }
}
