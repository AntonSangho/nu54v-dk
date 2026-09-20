/*
 * serial.js — SMP over 시리얼 전송 계층 (Web Serial)
 *
 * 보드의 cli 포트(VCOM1)에 그대로 붙는다. 펌웨어가 SMP 프레임만 가려내므로
 * cli 와 같은 포트를 쓴다 (docs/18_dfu.md §7).
 *
 * 프레이밍 (SMP Transport 스펙)
 *   첫 조각   : 0x06 0x09  + base64(...) + '\n'
 *   이어지는 조각 : 0x04 0x14 + base64(...) + '\n'
 *   base64 로 싸는 내용 : 2바이트 전체 길이(BE) + SMP 패킷 + CRC16
 *
 * base64 는 **줄마다 따로 인코딩하는 것이 아니라** 하나의 연속 문자열을 줄로 쪼갠 것이다
 * (마지막 줄에만 = 패딩이 붙는다). 받을 때도 문자열을 먼저 이어 붙인 뒤 디코딩해야 한다.
 *   CRC16 : 다항식 0x1021, 초기값 0, SMP 패킷에 대해서만 계산
 *
 * mcumgr / smpclient 와 같은 형식이라 서로 호환된다.
 */

const SERIAL_MARK_PKT = [0x06, 0x09];
const SERIAL_MARK_FRAG = [0x04, 0x14];

const PKT_MARK_STR = String.fromCharCode(...SERIAL_MARK_PKT);
const FRAG_MARK_STR = String.fromCharCode(...SERIAL_MARK_FRAG);

// 한 줄에 담는 base64 길이. 보드의 수신 버퍼(DFU_SERIAL_FRAG_MAX)보다 작아야 한다.
const SERIAL_LINE_MAX = 120;


function crc16(data, crc = 0) {
  for (const b of data) {
    crc ^= b << 8;
    for (let i = 0; i < 8; i++) {
      crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
    }
  }
  return crc;
}

function toBase64(bytes) {
  let s = "";
  for (const b of bytes) s += String.fromCharCode(b);
  return btoa(s);
}

/* 바이트열을 글자 하나당 한 바이트인 문자열로 (CR 은 버린다) */
function latin1(bytes) {
  let s = "";
  for (let i = 0; i < bytes.length; i += 4096) {
    const part = bytes.subarray(i, i + 4096);
    s += String.fromCharCode.apply(null, part);
  }
  return s.replace(/\r/g, "");
}

function fromBase64(str) {
  const bin = atob(str);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}


class SerialSmpTransport {
  constructor(log) {
    this.log = log || (() => {});
    this.port = null;
    this.reader = null;
    this.writer = null;
    this.onPacket = null;
    this.onText = null;       // SMP 가 아닌 줄 (보드 cli 출력)

    this.line = "";           // 받는 중인 한 줄
    this.b64 = "";            // 이어 붙이는 중인 base64 문자열
    this.bodyLen = -1;        // 아직 모름

    // 어디서 잃는지 가르기 위한 계수기.
    //
    // 응답이 안 왔을 때 "보드가 답을 안 했다" 와 "답이 왔는데 우리가 놓쳤다" 는
    // 원인이 전혀 다르다. rxBytes 가 그대로면 앞이고, 늘었는데 패킷이 안 되면 뒤다.
    this.cnt = {
      rxBytes: 0,        // 포트에서 읽은 전체 바이트
      rxLines: 0,        // 줄 끝(\n)을 본 횟수
      markPkt: 0,        // 0x06 0x09 로 시작한 줄
      markFrag: 0,       // 0x04 0x14 로 이어진 줄
      orphanFrag: 0,     // 시작을 못 본 채 온 이어짐 → 앞줄을 잃었다는 증거
      badB64: 0,         // base64 해독 실패
      crcErr: 0,         // CRC 불일치
      markInline: 0,     // 표식 앞에 cli 출력이 붙어 온 줄
      packets: 0,        // 온전히 모은 패킷
    };
  }

  /* 지금까지의 계수기와 아직 모으는 중인 것 (스냅샷) */
  stats() {
    return {
      ...this.cnt,
      partialLine: this.line.length,
      partialB64: this.b64.length,
      bodyLen: this.bodyLen,
    };
  }

  get isConnected() {
    return this.port !== null;
  }

  async connect() {
    this.port = await navigator.serial.requestPort();

    // 읽기 버퍼를 키운다.
    //
    // Chrome 의 기본값은 255 바이트고, 넘치면 **받은 데이터를 버린다**.
    // 업로드 중에는 응답이 쉴 새 없이 오는데 그 사이 화면 갱신 등으로 잠깐만 늦어도
    // 응답이 통째로 사라진다 (브라우저에서만 "응답이 오지 않는다" 가 났던 이유).
    await this.port.open({ baudRate: 115200, bufferSize: 16384 });
    this.writer = this.port.writable.getWriter();
    this.reader = this.port.readable.getReader();
    this.readLoop();
  }

  async disconnect() {
    try {
      if (this.reader) { await this.reader.cancel(); this.reader.releaseLock(); }
      if (this.writer) { this.writer.releaseLock(); }
      if (this.port) await this.port.close();
    } catch (e) { /* 이미 닫혔으면 무시 */ }
    this.reader = null;
    this.writer = null;
    this.port = null;
  }

  async readLoop() {
    try {
      while (this.reader) {
        const { value, done } = await this.reader.read();
        if (done) break;
        if (value) this.receive(value);
      }
    } catch (e) {
      // 포트가 닫히면 여기로 온다
    }
  }

  /*
   * 들어온 바이트에서 SMP 프레임만 골라낸다.
   * cli 출력이 섞여 오므로 표식으로 시작하는 줄만 본다.
   */
  receive(chunk) {
    this.cnt.rxBytes += chunk.length;

    // 바이트마다 문자열을 붙이면 느리다. 줄 단위로 잘라서 한 번에 붙인다.
    let start = 0;
    for (let i = 0; i < chunk.length; i++) {
      if (chunk[i] !== 0x0a) continue;        // 줄 끝을 찾는다

      this.cnt.rxLines++;
      this.line += latin1(chunk.subarray(start, i));
      this.handleLine(this.line);
      this.line = "";
      start = i + 1;
    }
    if (start < chunk.length) {
      this.line += latin1(chunk.subarray(start));
    }
  }

  handleLine(line) {
    // 표식이 줄 첫머리에 있다고 가정하면 안 된다.
    //
    // cli 는 프롬프트("cli# ")를 줄바꿈 없이 찍는다. 그 뒤에 SMP 응답이 이어지면
    // 한 줄 안에 "cli# " + 0x06 0x09 + base64 가 같이 온다. 첫 두 글자만 보면
    // 그 프레임을 통째로 버리게 되고, 뒤따르는 이어짐 줄이 전부 고아가 된다
    // (부팅 직후 첫 명령이 "응답이 오지 않는다" 로 끝나던 이유).
    //
    // base64 도 cli 출력도 모두 출력 가능한 글자라, 0x06 0x09 / 0x04 0x14 가
    // 중간에 나오면 그것은 틀림없이 표식이다.
    const iPkt = line.indexOf(PKT_MARK_STR);
    const iFrag = line.indexOf(FRAG_MARK_STR);

    let at = -1;
    let isPkt = false;
    if (iPkt >= 0 && (iFrag < 0 || iPkt < iFrag)) { at = iPkt; isPkt = true; }
    else if (iFrag >= 0) { at = iFrag; }

    if (at < 0) {
      // cli 출력이다. 듣는 쪽이 있으면 넘긴다 (보드 상태를 물어볼 때 쓴다).
      if (this.onText) this.onText(line);
      return;
    }
    if (at > 0) {
      this.cnt.markInline++;                  // 앞에 cli 출력이 붙어 있었다
      if (this.onText) this.onText(line.slice(0, at));
    }

    const rest = line.slice(at + 2);

    if (isPkt) {
      this.cnt.markPkt++;
      this.b64 = rest;                        // 새 패킷의 시작
      this.bodyLen = -1;
    } else {
      this.cnt.markFrag++;
      if (this.b64 === "") {                  // 시작을 못 본 이어짐은 버린다
        this.cnt.orphanFrag++;
        return;
      }
      this.b64 += rest;
    }

    // 4 글자 = 3 바이트. 지금까지 온 만큼만 디코딩해 길이를 본다.
    const aligned = this.b64.slice(0, Math.floor(this.b64.length / 4) * 4);
    if (aligned.length === 0) return;

    let bytes;
    try {
      bytes = fromBase64(aligned);
    } catch (e) {
      this.cnt.badB64++;
      this.b64 = "";
      return;
    }

    if (this.bodyLen < 0) {
      if (bytes.length < 2) return;
      this.bodyLen = (bytes[0] << 8) | bytes[1];
    }
    if (bytes.length < 2 + this.bodyLen) return;      // 아직 덜 왔다

    const body = bytes.subarray(2, 2 + this.bodyLen);
    const packet = body.slice(0, this.bodyLen - 2);
    const crc = (body[this.bodyLen - 2] << 8) | body[this.bodyLen - 1];

    this.b64 = "";
    this.bodyLen = -1;

    if (crc16(packet) !== crc) {
      this.cnt.crcErr++;
      this.log("시리얼 : CRC 가 맞지 않는다");
      return;
    }
    this.cnt.packets++;
    if (this.onPacket) this.onPacket(packet);
  }

  /*
   * 보드 cli 에 한 줄 보내고 잠시 동안의 출력을 모은다.
   *
   * SMP 와 같은 포트를 쓰므로 업로드 결과를 그 자리에서 물어볼 수 있다
   * (리셋하면 보드의 계수기가 지워져 나중에는 볼 수 없다).
   */
  async command(line, waitMs = 700) {
    if (!this.writer) throw new Error("시리얼이 열려 있지 않다");

    const out = [];
    const prev = this.onText;
    this.onText = (t) => out.push(t);

    try {
      // cli 의 엔터는 CR(0x0D) 이다 (cli.c 의 CLI_KEY_ENTER).
      // SMP 프레임이 쓰는 LF 와 다르다. LF 로 보내면 명령이 실행되지 않는다.
      const bytes = new Uint8Array(line.length + 1);
      for (let i = 0; i < line.length; i++) bytes[i] = line.charCodeAt(i);
      bytes[line.length] = 0x0d;
      await this.writer.write(bytes);
      await new Promise((r) => setTimeout(r, waitMs));
    } finally {
      this.onText = prev;
    }

    // 에코된 명령 줄과 빈 줄은 뺀다
    return out.map((t) => t.replace(/\x1b\[[0-9;]*m/g, "").trim())
              .filter((t) => t !== "" && !t.endsWith(line));
  }

  async send(packet) {
    if (!this.writer) throw new Error("시리얼이 열려 있지 않다");

    const crc = crc16(packet);
    const body = new Uint8Array(packet.length + 2);
    body.set(packet);
    body[packet.length] = (crc >> 8) & 0xff;
    body[packet.length + 1] = crc & 0xff;

    const raw = new Uint8Array(2 + body.length);
    raw[0] = (body.length >> 8) & 0xff;
    raw[1] = body.length & 0xff;
    raw.set(body, 2);

    const b64 = toBase64(raw);

    for (let i = 0; i < b64.length; i += SERIAL_LINE_MAX) {
      const part = b64.slice(i, i + SERIAL_LINE_MAX);
      const mark = (i === 0) ? SERIAL_MARK_PKT : SERIAL_MARK_FRAG;
      const line = new Uint8Array(2 + part.length + 1);
      line[0] = mark[0];
      line[1] = mark[1];
      for (let k = 0; k < part.length; k++) line[2 + k] = part.charCodeAt(k);
      line[line.length - 1] = 0x0a;
      await this.writer.write(line);
    }
  }
}
