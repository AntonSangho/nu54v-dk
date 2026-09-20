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

    transport.onPacket = (pkt) => {
      if (this.pending === null) return;
      const rsp = smpParse(pkt);
      const { resolve, reject, seq } = this.pending;
      if (rsp.seq !== seq) return;                 // 늦게 온 응답은 버린다
      this.pending = null;
      if (rsp.payload.rc !== undefined && rsp.payload.rc !== 0) {
        const rc = rsp.payload.rc;
        let why = SMP_ERR[rc] || "";
        // 자주 만나는 두 가지는 이유를 짚어 준다
        if (rc === 6) why = "실행 중 이미지가 확정 전이다. 먼저 confirm 한다";
        if (rc === 1) why = "같은 이미지이거나 처리할 수 없다";
        reject(new Error(`SMP 오류 rc=${rc}${why ? " — " + why : ""}`));
      } else {
        resolve(rsp.payload);
      }
    };
  }

  async request(op, group, id, payload = {}, timeoutMs = 10000) {
    if (this.pending) throw new Error("앞선 요청이 끝나지 않았다");

    const seq = this.seq++ & 0xff;
    const pkt = smpRequest(op, group, id, payload, seq);

    const result = new Promise((resolve, reject) => {
      this.pending = { resolve, reject, seq };
      setTimeout(() => {
        if (this.pending && this.pending.seq === seq) {
          this.pending = null;
          reject(new Error("응답이 오지 않는다"));
        }
      }, timeoutMs);
    });

    await this.transport.send(pkt);
    return result;
  }

  imageList() {
    return this.request(SMP_OP.READ, SMP_GROUP.IMAGE, SMP_ID_IMAGE.STATE);
  }

  imageState(hash, confirm) {
    return this.request(SMP_OP.WRITE, SMP_GROUP.IMAGE, SMP_ID_IMAGE.STATE,
                        { hash, confirm });
  }

  reset() {
    return this.request(SMP_OP.WRITE, SMP_GROUP.OS, SMP_ID_OS.RESET);
  }

  /*
   * 이미지를 slot1 에 올린다.
   *
   * 첫 요청에만 len 과 sha 를 같이 보낸다 (보드가 전체 크기를 알아야 한다).
   * 보드는 응답으로 다음에 보낼 위치(off)를 알려 준다.
   */
  async upload(image, onProgress, chunkSize = 160) {
    const sha = new Uint8Array(await crypto.subtle.digest("SHA-256", image));
    let off = 0;

    while (off < image.length) {
      const end = Math.min(off + chunkSize, image.length);
      const payload = { image: 1, off, data: image.subarray(off, end) };
      if (off === 0) {
        payload.len = image.length;
        payload.sha = sha;
      }

      const rsp = await this.request(
        SMP_OP.WRITE, SMP_GROUP.IMAGE, SMP_ID_IMAGE.UPLOAD, payload,
        off === 0 ? 40000 : 10000);            // 첫 조각은 슬롯을 지우느라 오래 걸린다

      if (typeof rsp.off !== "number") throw new Error("응답에 off 가 없다");
      off = rsp.off;
      if (onProgress) onProgress(off, image.length);
    }
  }
}
