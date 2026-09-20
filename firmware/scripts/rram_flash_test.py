"""RRAM 플래시 알고리즘 호출 규약을 검증한다 (웹 도구와 같은 순서).

web/js/flash.js 가 하는 것을 pyOCD 로 그대로 재현한다.
알고리즘 적재 → init → erase → program_page → 읽어서 확인 → CTRL-AP 리셋 →
시리얼로 부팅 확인.

브라우저 쪽이 안 될 때 "순서가 틀렸나, 전송 계층이 문제인가" 를 가르는 데 쓴다.

  python3 rram_flash_test.py <프로브UID> <merged.hex> <시리얼포트>

2026-09-20 : 이 순서로 정상 부팅까지 확인했다. 즉 순서는 맞다.
"""
import sys, time, re, threading
import serial
from intelhex import IntelHex
from pyocd.core.helpers import ConnectHelper

ALGO = "/System/Volumes/Data/Users/hancheol/hdd/develop/zephyr-home/zephyr-4.4.0/.venv/lib/python3.12/site-packages/pyocd/target/builtin/target_nRF54L15.py"
src = open(ALGO).read()
INSTR = [int(w,16) for w in re.findall(r'0x[0-9a-fA-F]{8}', src.split("'instructions': [")[1].split("],")[0])]

LOAD, PC_INIT, PC_UNINIT, PC_PROG, PC_ERASE = 0x20000000, 0x20000015, 0x20000019, 0x20000065, 0x20000041
SB, STACK, BUF, PAGE = 0x200000a4, 0x20000300, 0x20002000, 0x1000
CTRL_AP, CTRL_AP_RESET = 2, 0x000

def call(t, pc, r0=0, r1=0, r2=0):
    t.write_core_registers_raw(['pc','r0','r1','r2','r9','sp','xpsr','lr'],
                               [pc, r0, r1, r2, SB, STACK, 0x01000000, LOAD+1])
    t.resume()
    end = time.time()+10
    while not t.is_halted():
        if time.time()>end: raise RuntimeError("알고리즘 멈추지 않음")
        time.sleep(0.002)
    return t.read_core_register('r0')

def to_pages(ih):
    pages = {}
    for a in ih.addresses():
        p = a - (a % PAGE)
        pages.setdefault(p, bytearray(b'\xff'*PAGE))[a-p] = ih[a]
    return sorted(pages.items())

def main():
    uid, hexfile, port = sys.argv[1], sys.argv[2], sys.argv[3]
    pages = to_pages(IntelHex(hexfile))
    print(f"{len(pages)} 페이지 ({len(pages)*PAGE//1024} KB), "
          f"{pages[0][0]:#x} ~ {pages[-1][0]+PAGE:#x}")

    ser = serial.Serial(port, 115200, timeout=0.2)
    buf = bytearray(); stop = False
    def rd():
        while not stop: buf.extend(ser.read(2048))
    th = threading.Thread(target=rd); th.start()

    with ConnectHelper.session_with_chosen_probe(
            unique_id=uid, target_override='nrf54l', frequency=1000000,
            blocking=False, auto_unlock=False, resume_on_disconnect=False) as session:
        t = session.target
        t.reset_and_halt()
        t.write_memory_block32(LOAD, INSTR)

        call(t, PC_INIT, 0, 0, 1)
        for addr, _ in pages: call(t, PC_ERASE, addr)
        call(t, PC_UNINIT, 1)

        call(t, PC_INIT, 0, 0, 2)
        t0 = time.time()
        for addr, data in pages:
            words = [int.from_bytes(data[i:i+4],'little') for i in range(0, PAGE, 4)]
            t.write_memory_block32(BUF, words)
            if call(t, PC_PROG, addr, PAGE, BUF) != 0:
                raise RuntimeError(f"쓰기 실패 {addr:#x}")
        call(t, PC_UNINIT, 2)
        print(f"굽기 완료 {time.time()-t0:.1f} 초")

        first = t.read_memory_block32(pages[0][0], 4)
        print("확인 :", " ".join(f"{w:#010x}" for w in first))

        print("CTRL-AP 리셋 (2 -> 0)")
        ap = session.target.dp.aps[CTRL_AP]
        ap.write_reg(CTRL_AP_RESET, 2)
        ap.write_reg(CTRL_AP_RESET, 0)

    time.sleep(4)
    stop = True; th.join()
    out = buf.decode(errors='replace')
    print("=== 시리얼 ===")
    print(out if out.strip() else "(아무것도 안 나옴 — 펌웨어가 돌지 않는다)")

main()
