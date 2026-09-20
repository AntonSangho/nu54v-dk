#!/usr/bin/env python3
"""MCUboot 이미지를 SMP 로 올린다 (시리얼 / BLE).

fw 스크립트가 firmware/.tools/venv 의 파이썬으로 실행한다. 직접 부를 일은 없다.

  fw dfu                     시리얼(자동 탐색) 로 올리고 test → 리셋 → confirm
  fw dfu --transport ble     BLE 로
  fw dfu --no-confirm        확정하지 않는다 (리셋하면 이전 버전으로 돌아간다)

흐름
  업로드 → test 표시 → 리셋 → (다시 연결) → confirm
confirm 을 하지 않으면 MCUboot 가 다음 부팅에 이전 이미지로 되돌린다 (swap using move).
"""

import argparse
import asyncio
import glob
import sys
import time

from smpclient import SMPClient
from smpclient.generics import success
from smpclient.requests.image_management import ImageStatesRead, ImageStatesWrite
from smpclient.requests.os_management import ResetWrite


def log(msg):
    print(f"[dfu] {msg}", flush=True)


def find_serial_port():
    """보드의 cli 포트를 찾는다. 여러 개면 사용자가 --port 로 지정한다."""
    if sys.platform == "win32":
        import serial.tools.list_ports

        ports = [p.device for p in serial.tools.list_ports.comports()]
    else:
        ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
    return ports


def make_transport(args):
    if args.transport == "ble":
        from smpclient.transport.ble import SMPBLETransport

        return SMPBLETransport(), args.name

    from smpclient.transport.serial import SMPSerialTransport

    port = args.port
    if port is None:
        ports = find_serial_port()
        if len(ports) == 0:
            sys.exit("[dfu] 시리얼 포트를 찾지 못했다. --port 로 지정해라")
        if len(ports) > 1:
            print("[dfu] 포트가 여럿이다. --port 로 지정해라:")
            for p in ports:
                print(f"       {p}")
            sys.exit(1)
        port = ports[0]
    # 기본 프레임 크기로는 큰 이미지가 중간에 멈춘다 (docs/18_dfu.md §7)
    return SMPSerialTransport(max_smp_encoded_frame_size=args.frame), port


async def show_states(client, label):
    r = await client.request(ImageStatesRead())
    if not success(r):
        log(f"{label} : 이미지 목록 실패 {r}")
        return []
    for img in r.images:
        flags = []
        if img.active:
            flags.append("active")
        if img.confirmed:
            flags.append("confirmed")
        if img.pending:
            flags.append("pending")
        log(f"  {label} slot{img.slot} v{img.version} [{','.join(flags) or '-'}]")
    return r.images


async def run(args):
    with open(args.image, "rb") as f:
        image = f.read()
    log(f"이미지 : {args.image} ({len(image)} 바이트)")

    transport, address = make_transport(args)
    log(f"연결 : {args.transport} {address}")

    async with SMPClient(transport, address) as client:
        await show_states(client, "전")

        t0 = time.time()
        last = -1
        async for offset in client.upload(image, slot=1, subsequent_timeout_s=args.timeout):
            pct = offset * 100 // len(image)
            if pct // 10 != last // 10:
                log(f"  업로드 {pct}%")
                last = pct
        dur = time.time() - t0
        log(f"업로드 완료 : {dur:.1f} 초, {len(image) / dur / 1024:.1f} KB/s")

        images = await show_states(client, "후")
        new = [i for i in images if i.slot == 1]
        if not new:
            sys.exit("[dfu] slot1 에 이미지가 없다")

        r = await client.request(ImageStatesWrite(hash=new[0].hash, confirm=False))
        if not success(r):
            sys.exit(f"[dfu] test 표시 실패 : {r}")
        log("test 표시 완료")

        log("리셋")
        await client.request(ResetWrite())

    if not args.confirm:
        log("확정하지 않았다. 새 이미지가 뜬 뒤 `dfu confirm` 을 해야 유지된다")
        return

    log(f"재부팅 대기 {args.reboot_wait} 초")
    await asyncio.sleep(args.reboot_wait)

    transport, address = make_transport(args)
    async with SMPClient(transport, address) as client:
        images = await show_states(client, "재부팅")
        active = [i for i in images if i.active]
        if not active:
            sys.exit("[dfu] 실행 중인 이미지를 읽지 못했다")
        if active[0].confirmed:
            log("이미 확정되어 있다")
            return
        r = await client.request(ImageStatesWrite(hash=active[0].hash, confirm=True))
        if not success(r):
            sys.exit(f"[dfu] confirm 실패 : {r}")
        log("confirm 완료")
        await show_states(client, "확정")


def main():
    parser = argparse.ArgumentParser(description="SMP 로 펌웨어 업데이트")
    parser.add_argument("--image", required=True, help="서명된 이미지 (zephyr.signed.bin)")
    parser.add_argument("--transport", choices=["serial", "ble"], default="serial")
    parser.add_argument("--port", help="시리얼 포트 (없으면 자동 탐색)")
    parser.add_argument("--name", default="NU54V-DK", help="BLE 장치 이름")
    parser.add_argument("--frame", type=int, default=512, help="시리얼 프레임 크기")
    parser.add_argument("--timeout", type=float, default=20.0, help="응답 대기 (초)")
    parser.add_argument("--reboot-wait", type=float, default=8.0, help="리셋 후 대기 (초)")
    parser.add_argument("--no-confirm", dest="confirm", action="store_false",
                        help="확정하지 않는다")
    args = parser.parse_args()

    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        sys.exit(130)


if __name__ == "__main__":
    main()
