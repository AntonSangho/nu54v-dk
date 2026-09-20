#!/usr/bin/env python3
"""NU54-DK 펌웨어 빌드/다운로드/디버그 도우미 (macOS / Linux / Windows 공용).

nRF Connect SDK 툴체인 환경을 직접 구성한 뒤 west / pyocd 를 실행한다.
nRF Connect 터미널이나 전역 PATH 설정 없이 어느 셸에서든 동작한다.

  fw build [-p]      빌드 (-p : pristine 전체 재빌드)
  fw flash           다운로드 (빌드가 없으면 먼저 빌드)
  fw dfu             SMP 로 업데이트 (시리얼/BLE). MCUboot 를 쓰는 프로젝트만
  fw erase           칩 전체 삭제
  fw reset           타깃 리셋
  fw clean           build 폴더 삭제
  fw menuconfig      Kconfig 설정 (터미널 UI)
  fw debugserver     pyOCD GDB 서버 실행 (외부 GDB 연결용)
  fw tools           VS Code 디버거용 .tools 링크만 갱신
  fw env             SDK/툴체인 경로 확인
  fw shell           툴체인 환경이 적용된 셸 실행

프로젝트 폴더는 현재 폴더(기본) 또는 --project 로 지정한다.
SDK 버전과 보드는 firmware/ncs_config.json 에서 관리한다.
설치 경로가 기본값과 다르면 환경변수 NCS_ROOT 로 지정한다.
"""

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

FW_DIR = Path(__file__).resolve().parent.parent
CONFIG_FILE = FW_DIR / "ncs_config.json"
TOOLS_DIR = FW_DIR / ".tools"

IS_WINDOWS = platform.system() == "Windows"


def log(msg):
    print(f"[fw] {msg}", flush=True)


def die(msg):
    print(f"[fw] ERROR: {msg}", file=sys.stderr, flush=True)
    sys.exit(1)


def load_config():
    with open(CONFIG_FILE, encoding="utf-8") as f:
        cfg = json.load(f)
    # 환경변수로 일시적으로 버전을 바꿔 볼 수 있다. (예: NCS_VERSION=v3.4.1 fw build -p)
    cfg["ncs_version"] = os.environ.get("NCS_VERSION", cfg["ncs_version"])
    return cfg


def ncs_root():
    if os.environ.get("NCS_ROOT"):
        return Path(os.environ["NCS_ROOT"])
    system = platform.system()
    if system == "Darwin":
        return Path("/opt/nordic/ncs")
    if system == "Windows":
        return Path("C:/ncs")
    return Path.home() / "ncs"


def find_toolchain(root, version):
    index_file = root / "toolchains" / "toolchains.json"
    if not index_file.exists():
        die(f"toolchains.json 없음: {index_file} (NCS_ROOT 확인)")
    with open(index_file, encoding="utf-8") as f:
        index = json.load(f)
    # 최상위가 [ { "toolchains": [...] } ] 형태 (버전에 따라 dict 일 수도 있음)
    entries = index if isinstance(index, list) else [index]
    toolchains = [tc for e in entries for tc in e.get("toolchains", [])]
    for tc in toolchains:
        if version in tc.get("ncs_versions", []):
            path = root / "toolchains" / tc["identifier"]["bundle_id"]
            if path.exists():
                return path
    die(f"NCS {version} 용 툴체인이 설치되어 있지 않음 ({index_file})")


def make_env(tc_dir, sdk_dir):
    """툴체인의 environment.json 을 적용한 환경변수를 만든다."""
    env = dict(os.environ)
    with open(tc_dir / "environment.json", encoding="utf-8") as f:
        spec = json.load(f)

    for var in spec.get("env_vars", []):
        key = var["key"]
        kind = var["type"]
        if kind == "relative_paths":
            value = os.pathsep.join(str(tc_dir / p) for p in var["values"])
        elif kind == "string":
            value = var["value"]
        else:
            log(f"알 수 없는 환경변수 타입 무시: {kind} ({key})")
            continue

        treatment = var.get("existing_value_treatment", "overwrite")
        old = env.get(key)
        if old and treatment == "prepend_to":
            value = value + os.pathsep + old
        elif old and treatment == "append_to":
            value = old + os.pathsep + value
        env[key] = value

    env["ZEPHYR_BASE"] = str(sdk_dir / "zephyr")
    # 다른 파이썬 환경이 섞이지 않도록 한다.
    env.pop("PYTHONHOME", None)
    env.pop("PYTHONPATH", None)
    env.pop("VIRTUAL_ENV", None)
    return env


def tool_runs(path):
    try:
        return subprocess.run([str(path), "--version"], capture_output=True, timeout=20).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def fix_cmake(root, tc_dir, env):
    """[임시 우회] 툴체인의 cmake 가 이 PC 에서 실행되지 않으면 시스템/다른 툴체인의 cmake 를 쓴다.

    NCS v3.4.1 macOS 툴체인의 cmake 는 macOS 14 이상 전용이라 macOS 12 에서는 바로 죽는다.
    cmake 하나만 firmware/.tools/cmake 링크로 PATH 앞에 넣는다 (다른 도구는 원래 툴체인 그대로).
    정상 PC(macOS 14+, Windows, Linux)에서는 아무 것도 하지 않는다.
    """
    cmake = shutil.which("cmake", path=env["PATH"])
    if cmake and tool_runs(cmake):
        return env

    exe = "cmake.exe" if IS_WINDOWS else "cmake"
    candidates = []
    # 1) 시스템 PATH 의 cmake (예: Homebrew)  2) 설치된 다른 툴체인의 cmake
    system_cmake = shutil.which(exe, path=os.environ.get("PATH", ""))
    if system_cmake:
        candidates.append(Path(system_cmake))
    for other in sorted((root / "toolchains").iterdir()):
        if other.resolve() == tc_dir.resolve() or not other.is_dir():
            continue
        for rel in ("bin", "usr/local/bin", "opt/bin"):
            if (other / rel / exe).exists():
                candidates.append(other / rel / exe)

    for cand in candidates:
        if tool_runs(cand):
            TOOLS_DIR.mkdir(exist_ok=True)
            link = TOOLS_DIR / "cmake"
            target = Path(os.path.realpath(cand)).parent
            if not (link.exists() and link.resolve() == target.resolve()):
                link_dir(link, target)
            env = dict(env)
            env["PATH"] = str(link) + os.pathsep + env["PATH"]
            log(f"경고: 툴체인 cmake 실행 불가 → 임시로 {cand} 사용 (docs/00_handoff.md 참고)")
            return env

    die(f"실행 가능한 cmake 가 없음 ({cmake}). cmake 3.20 이상을 설치하세요 (macOS: brew install cmake)")


def resolve():
    cfg = load_config()
    root = ncs_root()
    version = cfg["ncs_version"]
    sdk_dir = root / version
    if not (sdk_dir / "zephyr").exists():
        die(f"NCS SDK {version} 가 없음: {sdk_dir}")
    tc_dir = find_toolchain(root, version)
    env = make_env(tc_dir, sdk_dir)
    env = fix_cmake(root, tc_dir, env)
    cfg["toolchain_dir"] = tc_dir
    return cfg, sdk_dir, env


def which(name, env):
    path = shutil.which(name, path=env["PATH"])
    if not path:
        die(f"'{name}' 를 툴체인에서 찾을 수 없음")
    return Path(path)


def link_dir(link, target):
    """link → target 디렉터리 링크 (Windows 는 관리자 권한이 필요 없는 junction)."""
    if os.path.lexists(link):
        if IS_WINDOWS or link.is_symlink():
            os.unlink(link) if link.is_symlink() else os.rmdir(link)
        else:
            shutil.rmtree(link)
    if IS_WINDOWS:
        import _winapi

        _winapi.CreateJunction(str(target), str(link))
    else:
        os.symlink(target, link, target_is_directory=True)


def update_tools(sdk_dir, env):
    """VS Code(cortex-debug)가 SDK 버전과 무관한 고정 경로를 쓰도록 링크를 만든다.

    firmware/.tools/gdb    → arm-zephyr-eabi-gdb 가 있는 폴더
    firmware/.tools/pyocd  → pyocd 가 있는 폴더
    firmware/.tools/svd    → nrf54l15_application.svd 가 있는 폴더
    """
    TOOLS_DIR.mkdir(exist_ok=True)
    targets = {
        "gdb": which("arm-zephyr-eabi-gdb", env).parent,
        "pyocd": which("pyocd", env).parent,
    }
    svd = next((sdk_dir / "modules" / "hal" / "nordic").rglob("nrf54l15_application.svd"), None)
    if svd:
        targets["svd"] = svd.parent

    for name, target in targets.items():
        link = TOOLS_DIR / name
        if link.exists() and link.resolve() == target.resolve():
            continue
        link_dir(link, target)
        log(f".tools/{name} → {target}")


def run(cmd, env, cwd):
    cmd = [str(c) for c in cmd]
    log(" ".join(cmd))
    # Windows 는 자식 env 가 아닌 현재 프로세스 PATH 로 실행 파일을 찾으므로 직접 경로를 푼다.
    cmd[0] = shutil.which(cmd[0], path=env["PATH"]) or cmd[0]
    try:
        return subprocess.call(cmd, env=env, cwd=cwd)
    except KeyboardInterrupt:
        return 130


def default_image(build_dir):
    """sysbuild 의 기본 이미지(애플리케이션) 빌드 폴더를 찾는다."""
    domains = build_dir / "domains.yaml"
    if domains.exists():
        for line in domains.read_text(encoding="utf-8").splitlines():
            if line.startswith("default:"):
                return build_dir / line.split(":", 1)[1].strip()
    return build_dir


# MCUboot 서명 키의 경로를 넘긴다.
#
# 앱 이미지와 MCUboot 이미지가 상대 경로를 서로 다르게 푼다 (앱은 west topdir 기준,
# MCUboot 는 conf 파일 폴더 기준). 저장소 위치는 PC 마다 다르므로 여기서 절대 경로로 만든다.
# MCUboot 를 쓰지 않는 프로젝트에는 넘기지 않는다 (그 심볼 자체가 없다).
#
def signing_key_arg(project):
    sysbuild_conf = project / "sysbuild.conf"

    if not sysbuild_conf.exists():
        return []
    if "SB_CONFIG_BOOTLOADER_MCUBOOT=y" not in sysbuild_conf.read_text():
        return []

    key = FW_DIR / "keys" / "nu54v_dk_ed25519.pem"
    if not key.exists():
        print(f"[경고] 서명 키가 없다: {key}")
        print("       imgtool keygen -k <경로> -t ed25519 로 만든다 (docs/18_dfu.md)")
        return []

    # Kconfig 문자열이므로 값에 따옴표가 들어가야 한다 (없으면 malformed string literal).
    return [f'-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="{key.as_posix()}"']


def cmd_build(args, cfg, sdk_dir, env):
    update_tools(sdk_dir, env)
    project = args.project
    build_dir = project / "build"
    cmd = ["west", "build", "-b", cfg["board"], "-d", build_dir, project]
    if args.pristine:
        cmd += ["-p", "always"]
    cmd += ["--", f"-DBOARD_ROOT={FW_DIR.as_posix()}"]
    cmd += signing_key_arg(project)
    ret = run(cmd, env, project)
    if ret == 0:
        # IntelliSense 가 이미지 이름과 무관하게 build/compile_commands.json 을 보도록 복사
        src = default_image(build_dir) / "compile_commands.json"
        if src.exists():
            shutil.copyfile(src, build_dir / "compile_commands.json")
    return ret


# 프로브가 여러 대일 때 어느 것을 쓸지 고른다.
#
#   fw flash --probe 5400360300052840a4efca674d33faa2
#   FW_PROBE=<UID> fw flash
#
# UID 는 `pyocd list` 가 보여 준다. 지정하지 않으면 pyOCD 가 번호를 물어보고,
# 스크립트에서는 입력을 받을 수 없어 실패한다.
def probe_uid(args):
    return getattr(args, "probe", None) or os.environ.get("FW_PROBE")


def probe_args(args):
    uid = probe_uid(args)
    return ["--dev-id", uid] if uid else []


def cmd_flash(args, cfg, sdk_dir, env):
    build_dir = args.project / "build"
    if not (build_dir / "build.ninja").exists():
        args.pristine = False
        ret = cmd_build(args, cfg, sdk_dir, env)
        if ret:
            return ret
    cmd = ["west", "flash", "-d", build_dir]
    cmd += probe_args(args)
    return run(cmd, env, args.project)


# SMP 업데이트용 파이썬 환경.
#
# smpclient 를 SDK 툴체인에 설치하면 공용 환경이 더러워진다. firmware/.tools 아래에
# 따로 venv 를 만든다 (.tools 는 .gitignore 에 있다). 처음 한 번만 설치한다.
def dfu_venv_python():
    venv_dir = TOOLS_DIR / "venv"
    python = venv_dir / ("Scripts/python.exe" if IS_WINDOWS else "bin/python")

    if python.exists():
        return python

    log(f"SMP 도구 환경을 만든다 : {venv_dir}")
    TOOLS_DIR.mkdir(exist_ok=True)
    base = shutil.which("python3") or shutil.which("python") or sys.executable
    if subprocess.call([base, "-m", "venv", str(venv_dir)]) != 0:
        die("venv 생성 실패")
    log("smpclient 설치 중 (처음 한 번)")
    if subprocess.call([str(python), "-m", "pip", "install", "--quiet",
                        "smpclient[ble,serial]"]) != 0:
        die("smpclient 설치 실패")
    return python


def cmd_dfu(args, cfg, sdk_dir, env):
    build_dir = args.project / "build"
    if not (build_dir / "build.ninja").exists():
        args.pristine = False
        ret = cmd_build(args, cfg, sdk_dir, env)
        if ret:
            return ret

    image = default_image(build_dir) / "zephyr" / "zephyr.signed.bin"
    if not image.exists():
        die(f"서명된 이미지가 없다: {image}\n       MCUboot 를 쓰는 프로젝트인지 확인해라 (sysbuild.conf)")

    cmd = [str(dfu_venv_python()), str(Path(__file__).resolve().parent / "dfu_update.py"),
           "--image", str(image), "--transport", args.transport]
    if args.port:
        cmd += ["--port", args.port]
    if args.ble_name:
        cmd += ["--name", args.ble_name]
    if args.no_confirm:
        cmd += ["--no-confirm"]

    return run(cmd, env, args.project)


def cmd_erase(args, cfg, sdk_dir, env):
    cmd = ["pyocd", "erase", "--chip", "-t", cfg["pyocd_target"]]
    if probe_uid(args):
        cmd += ["-u", probe_uid(args)]
    return run(cmd, env, args.project)


def cmd_reset(args, cfg, sdk_dir, env):
    return run(["pyocd", "reset", "-t", cfg["pyocd_target"]], env, args.project)


def cmd_clean(args, cfg, sdk_dir, env):
    build_dir = args.project / "build"
    if build_dir.exists():
        shutil.rmtree(build_dir)
        log(f"삭제: {build_dir}")
    return 0


def cmd_menuconfig(args, cfg, sdk_dir, env):
    build_dir = args.project / "build"
    return run(["west", "build", "-d", build_dir, "-t", "menuconfig"], env, args.project)


def cmd_debugserver(args, cfg, sdk_dir, env):
    return run(["west", "debugserver", "-d", args.project / "build"], env, args.project)


def cmd_tools(args, cfg, sdk_dir, env):
    update_tools(sdk_dir, env)
    return 0


def cmd_env(args, cfg, sdk_dir, env):
    print(f"NCS version : {cfg['ncs_version']}")
    print(f"SDK         : {sdk_dir}")
    print(f"Toolchain   : {cfg['toolchain_dir']}")
    print(f"Board       : {cfg['board']}")
    print(f"BOARD_ROOT  : {FW_DIR}")
    print(f"Project     : {args.project}")
    for tool in ("west", "cmake", "ninja", "arm-zephyr-eabi-gcc", "arm-zephyr-eabi-gdb", "pyocd"):
        print(f"{tool:<20}: {shutil.which(tool, path=env['PATH'])}")
    return 0


def cmd_shell(args, cfg, sdk_dir, env):
    shell = os.environ.get("COMSPEC", "cmd.exe") if IS_WINDOWS else os.environ.get("SHELL", "/bin/sh")
    log(f"NCS {cfg['ncs_version']} 환경 셸 시작 (exit 로 종료)")
    return run([shell], env, args.project)


COMMANDS = {
    "build": cmd_build,
    "flash": cmd_flash,
    "dfu": cmd_dfu,
    "erase": cmd_erase,
    "reset": cmd_reset,
    "clean": cmd_clean,
    "menuconfig": cmd_menuconfig,
    "debugserver": cmd_debugserver,
    "tools": cmd_tools,
    "env": cmd_env,
    "shell": cmd_shell,
}


def main():
    parser = argparse.ArgumentParser(prog="fw", description="NU54-DK firmware helper")
    parser.add_argument("command", choices=COMMANDS.keys())
    parser.add_argument("-p", "--pristine", action="store_true", help="build: 전체 재빌드")
    parser.add_argument("--project", type=Path, default=Path.cwd(), help="프로젝트 폴더 (기본: 현재 폴더)")
    parser.add_argument("--transport", choices=["serial", "ble"], default="serial",
                        help="dfu: 업데이트 경로 (기본 serial)")
    parser.add_argument("--port", help="dfu: 시리얼 포트 (없으면 자동 탐색)")
    parser.add_argument("--ble-name", help="dfu: BLE 장치 이름 (기본 NU54V-DK)")
    parser.add_argument("--no-confirm", action="store_true", help="dfu: 확정하지 않는다")
    parser.add_argument("--probe", help="프로브 UID (여러 대 연결 시. pyocd list 로 확인, 환경변수 FW_PROBE 도 가능)")
    args = parser.parse_args()
    args.project = args.project.resolve()

    if args.command not in ("env", "shell", "tools") and not (args.project / "CMakeLists.txt").exists():
        die(f"프로젝트 폴더가 아님 (CMakeLists.txt 없음): {args.project}")

    cfg, sdk_dir, env = resolve()
    sys.exit(COMMANDS[args.command](args, cfg, sdk_dir, env))


if __name__ == "__main__":
    main()
