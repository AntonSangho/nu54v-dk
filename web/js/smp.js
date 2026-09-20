/*
 * smp.js — MCUmgr(SMP) 프로토콜
 *
 * 펌웨어 업데이트 명령을 주고받는다. 전송 계층(BLE / 시리얼)과 분리되어 있다.
 *
 *   헤더 8 바이트 + CBOR 본문
 *   op(1) flags(1) len(2, BE) group(2, BE) seq(1) id(1)
 *
 * 쓰는 명령
 *   image list    (읽기, group 1, id 0)  슬롯 목록·해시
 *   image upload  (쓰기, group 1, id 1)  이미지 올리기
 *   image state   (쓰기, group 1, id 0)  test / confirm 표시
 *   os reset      (쓰기, group 0, id 5)  리셋
 *
 * CBOR 는 여기서 쓰는 만큼만 직접 만든다 (정수, 문자열, 바이트열, 맵, 배열, 불리언).
 * 보드는 길이를 미리 주지 않는 형태(indefinite length)로 보내므로 그것도 읽는다.
 */

const SMP_OP = { READ: 0, READ_RSP: 1, WRITE: 2, WRITE_RSP: 3 };
const SMP_GROUP = { OS: 0, IMAGE: 1 };
const SMP_ID_IMAGE = { STATE: 0, UPLOAD: 1 };
const SMP_ID_OS = { RESET: 5 };

/*
 * 보드가 rc 로 거절한 것.
 *
 * 응답이 오지 않은 것(유실)과 반드시 구분한다. 유실은 다시 보내면 되지만
 * rc 는 보드가 상태를 보고 내린 판단이라 같은 것을 다시 보내도 결과가 같다.
 */
class SmpError extends Error {
  constructor(rc, why) {
    super(`SMP 오류 rc=${rc}${why ? " — " + why : ""}`);
    this.rc = rc;
  }
}

/* 보드가 돌려주는 오류 코드 (mgmt_err_t) */
const SMP_ERR = {
  1: "알 수 없는 오류",
  2: "메모리가 모자란다",
  3: "요청이 잘못됐다",
  4: "명령을 모른다",
  5: "잘못된 인자",
  6: "지금 상태에서는 할 수 없다",
  8: "지원하지 않는다",
};


//-- CBOR
//
function cborEncode(value) {
  const out = [];

  const head = (major, n) => {
    if (n < 24) out.push((major << 5) | n);
    else if (n < 0x100) out.push((major << 5) | 24, n);
    else if (n < 0x10000) out.push((major << 5) | 25, n >> 8, n & 0xff);
    else out.push((major << 5) | 26, (n >>> 24) & 0xff, (n >>> 16) & 0xff, (n >>> 8) & 0xff, n & 0xff);
  };

  const enc = (v) => {
    if (v === true) { out.push(0xf5); return; }
    if (v === false) { out.push(0xf4); return; }
    if (v === null) { out.push(0xf6); return; }

    if (typeof v === "number") {
      if (v >= 0) head(0, v);
      else head(1, -v - 1);
      return;
    }
    if (typeof v === "string") {
      const bytes = new TextEncoder().encode(v);
      head(3, bytes.length);
      out.push(...bytes);
      return;
    }
    if (v instanceof Uint8Array) {
      head(2, v.length);
      out.push(...v);
      return;
    }
    if (Array.isArray(v)) {
      head(4, v.length);
      v.forEach(enc);
      return;
    }
    // 맵
    const keys = Object.keys(v);
    head(5, keys.length);
    for (const k of keys) { enc(k); enc(v[k]); }
  };

  enc(value);
  return new Uint8Array(out);
}

function cborDecode(bytes) {
  let pos = 0;

  const readLen = (info) => {
    if (info < 24) return info;
    if (info === 24) return bytes[pos++];
    if (info === 25) { const v = (bytes[pos] << 8) | bytes[pos + 1]; pos += 2; return v; }
    if (info === 26) {
      const v = ((bytes[pos] << 24) | (bytes[pos + 1] << 16) | (bytes[pos + 2] << 8) | bytes[pos + 3]) >>> 0;
      pos += 4; return v;
    }
    throw new Error(`CBOR: 다루지 않는 길이 ${info}`);
  };

  const dec = () => {
    const b = bytes[pos++];
    const major = b >> 5;
    const info = b & 0x1f;

    switch (major) {
      case 0: return readLen(info);
      case 1: return -readLen(info) - 1;
      case 2: {                                   // 바이트열
        if (info === 31) {                        // 길이를 모르는 형태 : 조각을 이어 붙인다
          const parts = [];
          while (bytes[pos] !== 0xff) parts.push(dec());
          pos++;
          const total = parts.reduce((n, p) => n + p.length, 0);
          const out = new Uint8Array(total);
          let at = 0;
          for (const p of parts) { out.set(p, at); at += p.length; }
          return out;
        }
        const n = readLen(info); const v = bytes.slice(pos, pos + n); pos += n; return v;
      }
      case 3: {                                   // 문자열
        if (info === 31) {
          let str = "";
          while (bytes[pos] !== 0xff) str += dec();
          pos++;
          return str;
        }
        const n = readLen(info);
        const v = new TextDecoder().decode(bytes.slice(pos, pos + n)); pos += n; return v;
      }
      case 4: {                                   // 배열
        const a = [];
        if (info === 31) {
          while (bytes[pos] !== 0xff) a.push(dec());
          pos++;
          return a;
        }
        const n = readLen(info);
        for (let i = 0; i < n; i++) a.push(dec());
        return a;
      }
      case 5: {                                   // 맵
        const m = {};
        if (info === 31) {                        // 보드가 이 형태로 보낸다
          while (bytes[pos] !== 0xff) { const k = dec(); m[k] = dec(); }
          pos++;
          return m;
        }
        const n = readLen(info);
        for (let i = 0; i < n; i++) { const k = dec(); m[k] = dec(); }
        return m;
      }
      case 7:
        if (info === 20) return false;
        if (info === 21) return true;
        if (info === 22) return null;
        throw new Error(`CBOR: 다루지 않는 단순값 ${info}`);
      default:
        throw new Error(`CBOR: 다루지 않는 타입 ${major}`);
    }
  };

  return dec();
}


//-- SMP 패킷
//
function smpRequest(op, group, id, payload, seq) {
  const body = cborEncode(payload);
  const pkt = new Uint8Array(8 + body.length);
  const dv = new DataView(pkt.buffer);

  dv.setUint8(0, op);
  dv.setUint8(1, 0);
  dv.setUint16(2, body.length);
  dv.setUint16(4, group);
  dv.setUint8(6, seq & 0xff);
  dv.setUint8(7, id);
  pkt.set(body, 8);

  return pkt;
}

function smpParse(pkt) {
  const dv = new DataView(pkt.buffer, pkt.byteOffset, pkt.byteLength);
  const len = dv.getUint16(2);

  return {
    op: dv.getUint8(0),
    length: len,
    group: dv.getUint16(4),
    seq: dv.getUint8(6),
    id: dv.getUint8(7),
    payload: len > 0 ? cborDecode(pkt.subarray(8, 8 + len)) : {},
  };
}


/*
 * SMP 클라이언트.
 *
 * 전송 계층은 send(Uint8Array) 와 onPacket(콜백) 만 제공하면 된다.
 */
class SmpClient {
  constructor(transport, log) {
    this.transport = transport;
    this.log = log || (() => {});
    this.seq = 0;
    this.pending = null;
    this.verbose = false;         // 구간별 속도를 남길까 (평소에는 조용하게)

    transport.onPacket = (pkt) => {
      if (this.pending === null) return;
      const rsp = smpParse(pkt);
      const { resolve, reject, seq, timer } = this.pending;
      if (rsp.seq !== seq) return;                 // 늦게 온 응답은 버린다
      clearTimeout(timer);                         // 남겨 두면 seq 가 한 바퀴 돌 때 오발한다
      this.pending = null;
      if (rsp.payload.rc !== undefined && rsp.payload.rc !== 0) {
        const rc = rsp.payload.rc;
        let why = SMP_ERR[rc] || "";
        // 자주 만나는 두 가지는 이유를 짚어 준다
        if (rc === 6) why = "실행 중 이미지가 확정 전이다. '현재 이미지 확정' 을 먼저 누른다";
        if (rc === 1) why = "같은 이미지이거나 처리할 수 없다";
        reject(new SmpError(rc, why));
      } else {
        resolve(rsp.payload);
      }
    };
  }

  /*
   * 응답이 안 왔을 때 그 사이 무엇이 들어왔는지 한 줄로 만든다.
   *
   * 수신 0 B  → 보드가 답을 안 했다 (보드가 요청을 놓쳤거나 처리 중 멈췄다)
   * 수신 있음 → 답은 왔는데 패킷이 못 됐다. 뒤의 숫자가 어디서 깨졌는지 말해 준다
   */
  static describeGap(before, after) {
    if (!before || !after) return "";

    const d = (k) => after[k] - before[k];
    const bits = [`수신 ${d("rxBytes")} B`];

    if (d("rxLines")) bits.push(`${d("rxLines")} 줄`);
    if (d("orphanFrag")) bits.push(`앞줄 잃음 ${d("orphanFrag")}`);
    if (d("badB64")) bits.push(`base64 오류 ${d("badB64")}`);
    if (d("crcErr")) bits.push(`CRC 오류 ${d("crcErr")}`);
    if (after.partialB64) bits.push(`모으는 중 ${after.partialB64} 글자`);
    if (after.partialLine) bits.push(`줄 끝 못 봄 ${after.partialLine} 글자`);

    return ` [${bits.join(", ")}]`;
  }

  /*
   * 기다리던 요청을 버린다.
   *
   * 업로드가 중간에 멈추면 pending 이 남아 다음 시도가 통째로 막힌다
   * ("앞선 요청이 끝나지 않았다"). 새로 시작할 때 여기를 먼저 지운다.
   */
  abort() {
    if (this.pending === null) return;

    const { reject, timer } = this.pending;
    clearTimeout(timer);
    this.pending = null;
    reject(new Error("중단했다"));
  }

  async request(op, group, id, payload = {}, timeoutMs = 10000) {
    if (this.pending) throw new Error("앞선 요청이 끝나지 않았다");

    const seq = this.seq++ & 0xff;
    const pkt = smpRequest(op, group, id, payload, seq);
    const stats = this.transport.stats ? this.transport.stats.bind(this.transport) : null;
    const before = stats ? stats() : null;

    const result = new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        if (this.pending && this.pending.seq === seq) {
          this.pending = null;
          reject(new Error("응답이 오지 않는다" + SmpClient.describeGap(before, stats && stats())));
        }
      }, timeoutMs);
      this.pending = { resolve, reject, seq, timer };
    });

    await this.transport.send(pkt);
    return result;
  }

  imageList() {
    return this.request(SMP_OP.READ, SMP_GROUP.IMAGE, SMP_ID_IMAGE.STATE);
  }

  /* hash 를 주면 그 이미지를, 주지 않으면 실행 중인 이미지를 가리킨다 */
  imageState(hash, confirm) {
    const payload = hash ? { hash, confirm } : { confirm };
    return this.request(SMP_OP.WRITE, SMP_GROUP.IMAGE, SMP_ID_IMAGE.STATE, payload);
  }

  /* 실행 중인 이미지를 확정한다 (되돌아갈 자리를 비운다) */
  confirmActive() {
    return this.imageState(null, true);
  }

  reset() {
    return this.request(SMP_OP.WRITE, SMP_GROUP.OS, SMP_ID_OS.RESET);
  }

  /*
   * 이미지를 slot1 에 올린다.
   *
   * 첫 요청에만 len 과 sha 를 같이 보낸다 (보드가 전체 크기를 알아야 한다).
   * 보드는 응답으로 다음에 보낼 위치(off)를 알려 준다.
   *
   * 앞서 같은 이미지를 올리다 끊겼으면 보드가 **이어받을 위치를 돌려준다**.
   * 그러면 처음부터 보내지 않으므로, 실제로 보낸 바이트를 따로 세어 돌려준다
   * (이미지 크기로 속도를 내면 이어받은 만큼 빨라 보인다).
   */
  /*
   * 조각 크기는 **전송 계층이 정한다** (transport.chunkSize).
   *
   * 시리얼과 BLE 의 한계가 전혀 다르다. 시리얼은 패킷을 여러 줄로 쪼개 보내
   * 512 까지 되지만, BLE 는 한 번의 write 에 패킷이 통째로 들어가야 해서
   * MTU 에 묶인다. 여기서 하나로 정하면 한쪽이 깨진다.
   */
  async upload(image, onProgress, chunkSize = null, retries = 5) {
    if (chunkSize === null) chunkSize = this.transport.chunkSize || 200;

    this.abort();                          // 앞서 멈춘 것이 남아 있으면 버린다

    const sha = new Uint8Array(await crypto.subtle.digest("SHA-256", image));
    let off = 0;
    let sent = 0;
    let resumedAt = 0;

    // 10 % 마다 실제 속도를 남긴다 (느려지면 어디서부터인지 보인다)
    let rttSum = 0, rttCnt = 0, rttMax = 0;
    let markPct = 0;
    const t0 = performance.now();

    // 첫 요청만 len 과 sha(32 바이트)를 같이 실어 패킷이 약 75 바이트 커진다.
    //
    // BLE 는 패킷 하나가 한 번의 write 에 들어가야 하고 MTU(244)에 묶인다.
    // 조각 200 이면 평소 패킷은 223 바이트로 들어가지만 첫 패킷은 275 가 되어
    // 넘친다. 브라우저가 잘라 보내면 보드는 불완전한 패킷을 기다리기만 하고
    // 응답이 오지 않는다 (첫 조각에서만 멈추는 증상).
    // 그래서 첫 조각만 그만큼 줄인다. 한 번뿐이라 속도에 영향이 없다.
    const firstChunk = Math.max(64, chunkSize - 96);

    while (off < image.length) {
      const size = (off === 0) ? firstChunk : chunkSize;
      const end = Math.min(off + size, image.length);
      const payload = { image: 1, off, data: image.subarray(off, end) };
      if (off === 0) {
        payload.len = image.length;
        payload.sha = sha;
      }

      const tChunk = performance.now();

      // 조각 하나가 실패하면 같은 위치를 다시 보낸다.
      // 보드는 다음에 받을 위치(off)를 돌려주므로 같은 자리를 다시 보내도 안전하다.
      let rsp = null;
      for (let attempt = 0; attempt <= retries; attempt++) {
        try {
          // 첫 조각은 슬롯을 지우느라 오래 걸린다 (실측 3.3 초).
          // 나머지는 실측 왕복이 78 ms, 최대 89 ms 다. 600 ms 면 7 배 여유이고,
          // 드물게 나는 재전송의 비용이 3 초에서 0.6 초로 준다.
          rsp = await this.request(
            SMP_OP.WRITE, SMP_GROUP.IMAGE, SMP_ID_IMAGE.UPLOAD, payload,
            off === 0 ? 40000 : 600);
          break;
        } catch (e) {
          // 보드가 판단해서 거절한 것은 다시 보내도 같다. 바로 알린다.
          if (e instanceof SmpError) throw e;
          if (attempt === retries) throw e;
          this.log(`조각 재전송 (${off} 바이트 지점, ${attempt + 1}/${retries}) — ${e.message}`);
          // 늦게 온 응답은 버린다. 타임아웃이 아니라 보내다 실패한 경우에는
          // pending 과 그 타이머가 살아 있으므로 abort() 로 같이 치운다.
          this.abort();
          await new Promise((r) => setTimeout(r, 200));
        }
      }

      if (typeof rsp.off !== "number") throw new Error("응답에 off 가 없다");

      // 왕복 시간을 모아 둔다. 어디서 시간이 새는지는 이것 말고는 알 길이 없다.
      const rtt = performance.now() - tChunk;
      rttSum += rtt;
      rttCnt++;
      if (rtt > rttMax) rttMax = rtt;

      sent += end - off;
      if (off === 0 && rsp.off > end) {
        resumedAt = rsp.off;                 // 보드가 이어받을 위치를 알려 줬다
        this.log(`보드에 이미 ${resumedAt} 바이트가 있다. 이어서 보낸다`);
      }
      off = rsp.off;
      if (onProgress) onProgress(off, image.length);

      const pct = Math.floor((off * 100) / image.length);
      if (this.verbose && pct >= markPct + 10) {
        markPct = pct - (pct % 10);
        const sec = (performance.now() - t0) / 1000;
        this.log(`${markPct}% — ${sec.toFixed(1)} 초, ${(sent / 1024 / sec).toFixed(2)} KB/s, `
          + `조각 ${rttCnt} 개 왕복 평균 ${(rttSum / rttCnt).toFixed(0)} ms, 최대 ${rttMax.toFixed(0)} ms`);
        rttSum = 0; rttCnt = 0; rttMax = 0;
      }
    }

    return { sent, resumedAt };
  }
}
