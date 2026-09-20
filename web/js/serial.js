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

    this.line = "";           // 받는 중인 한 줄
    this.b64 = "";            // 이어 붙이는 중인 base64 문자열
    this.bodyLen = -1;        // 아직 모름
  }

  get isConnected() {
    return this.port !== null;
  }

  async connect() {
    this.port = await navigator.serial.requestPort();
    await this.port.open({ baudRate: 115200 });
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
    for (const b of chunk) {
      if (b === 0x0a) {                       // 줄 끝
        this.handleLine(this.line);
        this.line = "";
        continue;
      }
      if (b === 0x0d) continue;               // CR 은 버린다
      this.line += String.fromCharCode(b);
    }
  }

  handleLine(line) {
    const c0 = line.charCodeAt(0);
    const c1 = line.charCodeAt(1);

    if (c0 === SERIAL_MARK_PKT[0] && c1 === SERIAL_MARK_PKT[1]) {
      this.b64 = line.slice(2);               // 새 패킷의 시작
      this.bodyLen = -1;
    } else if (c0 === SERIAL_MARK_FRAG[0] && c1 === SERIAL_MARK_FRAG[1]) {
      if (this.b64 === "") return;            // 시작을 못 본 이어짐은 버린다
      this.b64 += line.slice(2);
    } else {
      return;                                 // cli 출력이다. 무시한다
    }

    // 4 글자 = 3 바이트. 지금까지 온 만큼만 디코딩해 길이를 본다.
    const aligned = this.b64.slice(0, Math.floor(this.b64.length / 4) * 4);
    if (aligned.length === 0) return;

    let bytes;
    try {
      bytes = fromBase64(aligned);
    } catch (e) {
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
      this.log("시리얼 : CRC 가 맞지 않는다");
      return;
    }
    if (this.onPacket) this.onPacket(packet);
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
