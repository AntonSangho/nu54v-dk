#!/usr/bin/env python3
"""SMP over 시리얼 왕복 시간 측정 도구.

웹페이지(web/js/serial.js, smp.js)와 **같은 프레이밍**을 쓴다. 브라우저를 거치지
않고 같은 것을 재보기 위한 것이다. 웹에서 "조각 왕복 평균 1001 ms" 가 나왔을 때
그게 브라우저 탓인지 보드 탓인지 가르려면 이쪽 숫자가 필요하다.

  ping   : image list 를 N 번 (플래시를 건드리지 않는 순수 왕복)
  upload : 이미지를 N 조각만 올려 보고 왕복 분포를 낸다 (전부 올릴 필요 없다)
  cli    : 보드 cli 명령을 그대로 보내고 출력을 받는다

프레이밍
  첫 줄   0x06 0x09 + base64(...)  이어지는 줄  0x04 0x14 + base64(...)
  base64 로 싸는 것 : 2바이트 전체 길이(BE) + SMP 패킷 + CRC16(0x1021, 초기 0)
  base64 는 하나의 연속 문자열을 줄로 쪼갠 것이다 (줄마다 인코딩하지 않는다)
"""

import argparse
import base64
import hashlib
import statistics
import struct
import sys
import time

import serial
import serial.tools.list_ports

MARK_PKT = b"\x06\x09"
MARK_FRAG = b"\x04\x14"
LINE_MAX = 120

SMP_OP_READ, SMP_OP_WRITE = 0, 2
GRP_OS, GRP_IMAGE = 0, 1
ID_IMAGE_STATE, ID_IMAGE_UPLOAD = 0, 1


def crc16(data: bytes, crc: int = 0) -> int:
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


# -- CBOR (여기서 쓰는 만큼만)
#
def cbor_enc(v) -> bytes:
    def head(major, n):
        if n < 24:
            return bytes([(major << 5) | n])
        if n < 0x100:
            return bytes([(major << 5) | 24, n])
        if n < 0x10000:
            return bytes([(major << 5) | 25]) + struct.pack(">H", n)
        return bytes([(major << 5) | 26]) + struct.pack(">I", n)

    if v is True:
        return b"\xf5"
    if v is False:
        return b"\xf4"
    if isinstance(v, int):
        return head(0, v) if v >= 0 else head(1, -v - 1)
    if isinstance(v, str):
        b = v.encode()
        return head(3, len(b)) + b
    if isinstance(v, (bytes, bytearray)):
        return head(2, len(v)) + bytes(v)
    if isinstance(v, dict):
        out = head(5, len(v))
        for k, val in v.items():
            out += cbor_enc(k) + cbor_enc(val)
        return out
    raise TypeError(v)


def cbor_dec(data: bytes):
    pos = 0

    def read_len(info):
        nonlocal pos
        if info < 24:
            return info
        if info == 24:
            pos += 1
            return data[pos - 1]
        if info == 25:
            pos += 2
            return struct.unpack(">H", data[pos - 2:pos])[0]
        if info == 26:
            pos += 4
            return struct.unpack(">I", data[pos - 4:pos])[0]
        raise ValueError(f"길이 {info}")

    def dec():
        nonlocal pos
        b = data[pos]
        pos += 1
        major, info = b >> 5, b & 0x1F

        if major == 0:
            return read_len(info)
        if major == 1:
            return -read_len(info) - 1
        if major in (2, 3):
            if info == 31:                      # 보드는 길이를 미리 주지 않는다
                parts = []
                while data[pos] != 0xFF:
                    parts.append(dec())
                pos += 1
                return b"".join(parts) if major == 2 else "".join(parts)
            n = read_len(info)
            pos += n
            v = data[pos - n:pos]
            return v if major == 2 else v.decode()
        if major == 4:
            a = []
            if info == 31:
                while data[pos] != 0xFF:
                    a.append(dec())
                pos += 1
                return a
            return [dec() for _ in range(read_len(info))]
        if major == 5:
            m = {}
            if info == 31:
                while data[pos] != 0xFF:
                    k = dec()
                    m[k] = dec()
                pos += 1
                return m
            for _ in range(read_len(info)):
                k = dec()
                m[k] = dec()
            return m
        if major == 7:
            if info == 20:
                return False
            if info == 21:
                return True
            if info == 22:
                return None
        raise ValueError(f"타입 {major}")

    return dec()


class SmpSerial:
    def __init__(self, port, baud=115200, verbose=False, ahead=512):
        self.ser = serial.Serial(port, baud, timeout=0)
        self.baud = baud
        self.seq = 0
        self.verbose = verbose
        self.buf = b""
        self.b64 = ""
        self.text = []

        # 프로브(DAPLink)는 USB 로 받은 것을 UART 로 흘려보낸다. 흐름제어가 없어
        # 전선보다 빨리 쓰면 내부 버퍼가 넘치고 **조용히 버린다**.
        # 그래서 전선 속도에 맞춰 쓴다. 전선이 어차피 병목이라 손해가 없다.
        self.ahead = ahead              # 앞질러 써도 되는 바이트 (프로브 버퍼 여유)
        self.drain_at = time.perf_counter()

    def close(self):
        self.ser.close()

    # 한 패킷을 줄로 쪼개 보낸다
    def send(self, pkt: bytes):
        body = pkt + struct.pack(">H", crc16(pkt))
        raw = struct.pack(">H", len(body)) + body
        b64 = base64.b64encode(raw).decode()

        out = b""
        for i in range(0, len(b64), LINE_MAX):
            mark = MARK_PKT if i == 0 else MARK_FRAG
            out += mark + b64[i:i + LINE_MAX].encode() + b"\n"
        self.write_paced(out)

    # 전선이 비울 시각을 따라가며 쓴다 (프로브 버퍼 넘침 방지)
    #
    # 한 번에 ahead 바이트씩만 쓰고, 그만큼이 전선으로 빠져나갈 시간을 기다린다.
    # 쪼개지 않으면 한 번의 write 가 이미 프로브 버퍼보다 커서 페이싱이 소용없다.
    def write_paced(self, data):
        if self.ahead <= 0:                       # 페이싱 끄기 (비교용)
            self.ser.write(data)
            return

        budget = self.ahead * 10 / self.baud

        for i in range(0, len(data), self.ahead):
            part = data[i:i + self.ahead]
            now = time.perf_counter()
            if self.drain_at < now:
                self.drain_at = now
            if self.drain_at - now > budget:
                time.sleep(self.drain_at - now - budget)

            self.ser.write(part)
            self.drain_at = max(self.drain_at, time.perf_counter()) + len(part) * 10 / self.baud

    # 줄을 모아 프레임을 뽑는다. 표식이 줄 중간에 있어도 찾는다
    # (cli 프롬프트가 줄바꿈 없이 앞에 붙어 온다).
    def _feed(self, chunk: bytes):
        self.buf += chunk
        while b"\n" in self.buf:
            line, self.buf = self.buf.split(b"\n", 1)
            line = line.replace(b"\r", b"")

            i_pkt, i_frag = line.find(MARK_PKT), line.find(MARK_FRAG)
            if i_pkt >= 0 and (i_frag < 0 or i_pkt < i_frag):
                at, is_pkt = i_pkt, True
            elif i_frag >= 0:
                at, is_pkt = i_frag, False
            else:
                if line:
                    self.text.append(line.decode("latin1"))
                continue

            if at > 0:
                self.text.append(line[:at].decode("latin1"))

            rest = line[at + 2:].decode("latin1")
            if is_pkt:
                self.b64 = rest
            elif self.b64:
                self.b64 += rest
            else:
                continue

            aligned = self.b64[:len(self.b64) // 4 * 4]
            if not aligned:
                continue
            try:
                raw = base64.b64decode(aligned)
            except Exception:
                self.b64 = ""
                continue
            if len(raw) < 2:
                continue
            body_len = struct.unpack(">H", raw[:2])[0]
            if len(raw) < 2 + body_len:
                continue

            body = raw[2:2 + body_len]
            packet, crc = body[:-2], struct.unpack(">H", body[-2:])[0]
            self.b64 = ""
            if crc16(packet) == crc:
                yield packet

    def request(self, op, group, cmd_id, payload=None, timeout=10.0):
        seq = self.seq & 0xFF
        self.seq += 1
        body = cbor_enc(payload or {})
        pkt = struct.pack(">BBHHBB", op, 0, len(body), group, seq, cmd_id) + body

        self.ser.reset_input_buffer()
        self.buf = b""
        self.b64 = ""
        t0 = time.perf_counter()
        self.send(pkt)

        while time.perf_counter() - t0 < timeout:
            data = self.ser.read(4096)
            if not data:
                time.sleep(0.001)
                continue
            for rsp in self._feed(data):
                if rsp[6] != seq:
                    continue
                rtt = (time.perf_counter() - t0) * 1000
                out = cbor_dec(rsp[8:]) if struct.unpack(">H", rsp[2:4])[0] else {}
                if out.get("rc"):
                    raise RuntimeError(f"rc={out['rc']}")
                return out, rtt
        raise TimeoutError("응답이 오지 않는다")

    def image_list(self):
        return self.request(SMP_OP_READ, GRP_IMAGE, ID_IMAGE_STATE)

    def cli(self, line, wait=0.8):
        """보드 cli 에 한 줄. 엔터는 CR(0x0D) 이다 (cli.c 의 CLI_KEY_ENTER)."""
        self.text = []
        self.ser.reset_input_buffer()
        self.ser.write(line.encode() + b"\r")
        t0 = time.perf_counter()
        while time.perf_counter() - t0 < wait:
            data = self.ser.read(4096)
            if data:
                list(self._feed(data))
            else:
                time.sleep(0.005)
        return [t for t in self.text if t.strip() and not t.strip().endswith(line)]


def find_port(explicit=None):
    if explicit:
        return explicit
    # CMSIS-DAP (0d28:0204) 의 두 번째 CDC 가 VCOM1 (cli)
    cand = sorted(p.device for p in serial.tools.list_ports.comports()
                  if (p.vid, p.pid) == (0x0D28, 0x0204))
    if not cand:
        sys.exit("NU54DK 포트를 못 찾았다. --port 로 지정한다")
    return cand[1] if len(cand) > 1 else cand[0]


def show(name, vals):
    if not vals:
        return
    vals = sorted(vals)
    print(f"  {name:8s} n={len(vals):4d}  최소 {vals[0]:7.1f}  중앙 {statistics.median(vals):7.1f}  "
          f"평균 {statistics.fmean(vals):7.1f}  최대 {vals[-1]:7.1f} ms")
    # 1000 ms 근처에 뭉치는지가 핵심이다 (타임아웃인지 지연인지 가른다)
    buckets = {}
    for v in vals:
        buckets[int(v // 100) * 100] = buckets.get(int(v // 100) * 100, 0) + 1
    print("  분포:", "  ".join(f"{k}~{k+99}ms:{n}" for k, n in sorted(buckets.items())))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["ping", "upload", "cli"])
    ap.add_argument("arg", nargs="?", help="upload: .bin 경로 / cli: 명령")
    ap.add_argument("--port")
    ap.add_argument("-n", type=int, default=30, help="ping 횟수 / upload 조각 수")
    # 512 : 실측으로 가장 빠르고 안전한 값.
    # 더 키우면 한 번에 쓰는 양이 프로브 버퍼를 넘어 조용히 잃고(흐름제어 없음),
    # 안전하게 조이면 전선을 못 채워 오히려 느려진다 (1024 는 4.9, 512 는 6.0 KB/s).
    ap.add_argument("--chunk", type=int, default=512)
    # 조각 512 에서는 한 번에 쓰는 양(약 790 B)을 프로브가 견디므로 페이싱이 필요 없다
    # (실측 5.55 vs 5.51 KB/s — 켜면 오히려 미세하게 손해). 조각을 더 키우거나
    # 보率을 올려 프로브가 못 따라올 때 쓰는 안전장치로 남겨 둔다.
    ap.add_argument("--ahead", type=int, default=0,
                    help="전선보다 앞질러 쓸 바이트 (프로브 버퍼 여유). 0 이면 페이싱 안 함")
    args = ap.parse_args()

    port = find_port(args.port)
    print(f"포트 : {port}")
    s = SmpSerial(port, ahead=args.ahead)
    try:
        if args.mode == "cli":
            for line in s.cli(args.arg or "help"):
                print(" ", line)
            return

        if args.mode == "ping":
            rtts = []
            for i in range(args.n):
                _, rtt = s.image_list()
                rtts.append(rtt)
            print(f"\nimage list {args.n} 회 (플래시를 건드리지 않는 순수 왕복)")
            show("ping", rtts)
            return

        img = open(args.arg, "rb").read()
        sha = hashlib.sha256(img).digest()
        print(f"이미지 : {len(img)} 바이트, 조각 {args.chunk} 바이트, 최대 {args.n} 조각")

        off, rtts, first = 0, [], None
        t0 = time.perf_counter()
        while off < len(img) and len(rtts) < args.n:
            end = min(off + args.chunk, len(img))
            pl = {"image": 1, "off": off, "data": img[off:end]}
            if off == 0:
                pl["len"] = len(img)
                pl["sha"] = sha
            rsp, rtt = s.request(SMP_OP_WRITE, GRP_IMAGE, ID_IMAGE_UPLOAD, pl,
                                 timeout=60 if off == 0 else 5)
            if off == 0:
                first = rtt
                if rsp["off"] > end:
                    print(f"보드에 이미 {rsp['off']} 바이트가 있다. 이어서 보낸다")
            else:
                rtts.append(rtt)
            off = rsp["off"]

        sec = time.perf_counter() - t0
        sent = len(rtts) * args.chunk
        print(f"\n첫 조각(슬롯 지우기 포함) : {first:.0f} ms")
        print(f"{len(rtts)} 조각 / {sec:.1f} 초 → {sent / 1024 / sec:.2f} KB/s")
        show("upload", rtts)

        print("\n보드 계수기")
        for cmd in ("dfu info", "uart info"):
            for line in s.cli(cmd):
                print(" ", line)
    finally:
        s.close()


if __name__ == "__main__":
    main()
