#!/usr/bin/env python3
"""빌드한 예제 이미지를 web/fw/ 로 옮기고 목록(manifest.json)을 만든다.

웹페이지에서 파일을 찾아 헤매지 않고 바로 고를 수 있게 한다.

    python3 web/pack_fw.py            # 기본 목록(dfu)만
    python3 web/pack_fw.py led shtc3  # 예제를 더 넣을 때

**저장소에 들어가는 파일이라 크기를 생각해서 고른다.** 이미지는 빌드할 때마다
내용이 통째로 바뀌므로 갱신할 때마다 git 히스토리가 그만큼 커진다.
기본은 dfu 하나다 (merged.hex 835 KB + signed.bin 254 KB).
"""

import json
import pathlib
import re
import shutil
import sys

WEB = pathlib.Path(__file__).parent
FW = WEB / "fw"
PROJECTS = WEB.parent / "firmware" / "projects"

DEFAULT = ["dfu"]


def version_of(proj: pathlib.Path) -> str:
    """VERSION 파일에서 1.0.10 형태로."""
    f = proj / "VERSION"
    if not f.exists():
        return ""
    v = dict(re.findall(r"(\w+)\s*=\s*(\d+)", f.read_text(encoding="utf-8")))
    return f"{v.get('VERSION_MAJOR', 0)}.{v.get('VERSION_MINOR', 0)}.{v.get('PATCHLEVEL', 0)}"


def find(proj: pathlib.Path, *patterns):
    for pat in patterns:
        hits = sorted(proj.glob(pat))
        if hits:
            return hits[0]
    return None


def main():
    names = sys.argv[1:] or DEFAULT
    FW.mkdir(exist_ok=True)

    # 이번에 넣지 않는 것은 지운다 (오래된 이미지가 남아 있으면 헷갈린다)
    keep = set()
    items = []

    for name in names:
        proj = PROJECTS / name
        if not proj.is_dir():
            print(f"  건너뜀 {name} — 프로젝트가 없다")
            continue

        ver = version_of(proj)
        item = {"name": name, "version": ver}

        # SWD 로 굽는 것 : MCUboot 를 쓰면 merged.hex, 아니면 앱 hex
        hex_src = find(proj, "build/merged.hex", "build/*/zephyr/zephyr.hex")
        # SMP(BLE·시리얼)로 올리는 것 : 서명된 앱만
        bin_src = find(proj, "build/*/zephyr/zephyr.signed.bin")

        for src, kind, ext in ((hex_src, "hex", "hex"), (bin_src, "bin", "bin")):
            if src is None:
                continue
            tag = "merged" if src.name == "merged.hex" else ("signed" if kind == "bin" else "app")
            dst_name = f"{name}-{ver}.{tag}.{ext}" if ver else f"{name}.{tag}.{ext}"
            shutil.copy2(src, FW / dst_name)
            keep.add(dst_name)
            item[kind] = f"fw/{dst_name}"
            item[kind + "_size"] = (FW / dst_name).stat().st_size
            print(f"  {name:8s} {kind}  {dst_name}  ({item[kind + '_size'] // 1024} KB)")

        if "hex" in item or "bin" in item:
            items.append(item)

    for f in FW.iterdir():
        if f.name != "manifest.json" and f.name not in keep:
            f.unlink()
            print(f"  지움 {f.name}")

    (FW / "manifest.json").write_text(
        json.dumps({"examples": items}, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    print(f"목록 {len(items)} 개 → fw/manifest.json")


if __name__ == "__main__":
    main()
