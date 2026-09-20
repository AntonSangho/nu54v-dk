/*
 * main.js — UI 와 연결 관리
 *
 * dapjs 로 보드의 CMSIS-DAP 프로브에 WebUSB 로 붙는다.
 * 프로브가 USB 인터페이스로 MSD/CDC/HID/WebUSB 를 내놓는데, 그중 WebUSB(벌크) 를 쓴다.
 */

const PROBE_VID = 0x0d28;          // ARM (CMSIS-DAP)
const PROBE_PID = 0x0204;

let device = null;                 // USBDevice
let dap = null;                    // DAPjs.CmsisDAP
let target = null;                 // DAPjs.CortexM

const $ = (id) => document.getElementById(id);

function log(msg, cls) {
  const el = $("log");
  const time = new Date().toLocaleTimeString("ko-KR", { hour12: false });
  const line = document.createElement("span");
  line.className = cls || "";
  line.textContent = `[${time}] ${msg}\n`;
  el.appendChild(line);
  el.scrollTop = el.scrollHeight;
}

function setConnected(on) {
  $("connect").disabled = on;
  $("disconnect").disabled = !on;
  $("read").disabled = !on;
  $("reset").disabled = !on;
  $("recover").disabled = !on;
  $("erase").disabled = !on;
  $("program").disabled = !on || $("hexfile").files.length === 0;
}

function checkSupport() {
  if (navigator.usb) {
    $("support").textContent =
      "USB 케이블로 보드를 꽂고 연결을 누른다. 장치 목록에서 프로브를 고른다.";
    return true;
  }
  $("support").innerHTML =
    "<b class='err'>이 브라우저는 WebUSB 를 지원하지 않는다.</b> " +
    "Chrome / Edge / Opera (데스크톱 또는 Android) 로 열어야 한다.";
  $("connect").disabled = true;
  return false;
}

async function connect() {
  try {
    device = await navigator.usb.requestDevice({
      filters: [{ vendorId: PROBE_VID, productId: PROBE_PID }],
    });
  } catch (e) {
    log("장치를 고르지 않았다", "err");
    return;
  }

  try {
    log(`장치 : ${device.productName || "(이름 없음)"} / ${device.serialNumber || "-"}`);

    const transport = new DAPjs.WebUSB(device);
    dap = new DAPjs.CmsisDAP(transport);
    target = new DAPjs.CortexM(transport);

    await target.connect();
    await clearErrors();
    await prepareDp();
    log("SWD 연결됨", "ok");

    setConnected(true);
    $("i-probe").textContent = device.productName || "-";
    await readInfo();
  } catch (e) {
    log(`연결 실패 : ${e.message || e}`, "err");
    log("다른 프로그램(pyOCD, 디버거)이 프로브를 쓰고 있지 않은지 본다");
    await disconnect();
  }
}

async function disconnect() {
  try {
    if (target) await target.disconnect();
  } catch (e) { /* 이미 끊겼으면 무시 */ }
  target = null;
  dap = null;
  device = null;
  setConnected(false);
  log("연결 끊음");
}

/*
 * 남아 있는 전송 오류(sticky error)를 지운다.
 *
 * 앞선 세션이 알고리즘 실행 중에 끊기면 DP 에 오류가 남아, 다시 붙어도
 * 메모리 읽기가 "Transfer response FAULT" 로 실패한다.
 */
async function clearErrors() {
  try {
    await target.clearAbort();
  } catch (e) {
    // 지원하지 않는 프로브면 무시한다
  }
}

/*
 * 디버그 포트를 쓸 수 있는 상태로 만든다.
 *
 * dapjs 의 connect() 만으로는 부족한 경우가 있다. 앞선 세션이 남긴 오류가 있거나
 * 디버그 전원이 올라가 있지 않으면 첫 메모리 접근부터 FAULT 가 난다.
 *
 *   ABORT(0x00)      : 남은 오류 비트를 모두 지운다
 *   CTRL/STAT(0x04)  : 디버그·시스템 전원을 올리고 확인될 때까지 기다린다
 */
// dapjs 의 readDP/writeDP 는 레지스터의 바이트 주소를 받는다 (인덱스가 아니다)
const DP_ABORT = 0x00;
const DP_CTRL_STAT = 0x04;
const ABORT_ALL = 0x1e;      // STKCMPCLR|STKERRCLR|WDERRCLR|ORUNERRCLR
const PWRUP_REQ = 0x50000000;
const PWRUP_ACK = 0xa0000000;

async function prepareDp() {
  invalidateDapCache(target);          // 캐시가 하드웨어와 어긋나 있을 수 있다
  await target.writeDP(DP_ABORT, ABORT_ALL);
  await target.writeDP(DP_CTRL_STAT, PWRUP_REQ);

  for (let i = 0; i < 50; i++) {
    const stat = await target.readDP(DP_CTRL_STAT);
    if ((stat & PWRUP_ACK) === PWRUP_ACK) return;
    await new Promise((r) => setTimeout(r, 10));
  }
  // dapjs 의 connect() 가 이미 올려 두는 경우가 많다. 확인이 안 돼도 막지는 않는다.
  log("경고 : 디버그 전원 확인 실패. 그대로 진행한다");
}

/* 메모리 읽기. 한 번 실패하면 DP 를 정리하고 다시 해 본다. */
async function readMemRetry(address) {
  try {
    return await target.readMem32(address);
  } catch (e) {
    await clearErrors();
    await prepareDp();
    return await target.readMem32(address);
  }
}

/* 코어를 리셋해 멈춘 상태로 만든다. 상태가 꼬였을 때 쓴다. */
async function recover() {
  try {
    log("복구 : 오류 지우고 디버그 전원 확인, 리셋 후 halt");
    await clearErrors();
    await prepareDp();
    const flasher = new Flasher(target, log);
    await flasher.ctrlApReset();                 // 코어를 거치지 않는 리셋
    await target.connect();                      // 리셋 뒤 dapjs 캐시를 다시 맞춘다
    await clearErrors();
    await prepareDp();
    log("복구됨", "ok");
    await readInfo();
  } catch (e) {
    log(`복구 실패 : ${e.message || e}`, "err");
    log("USB 를 뽑았다 다시 꽂는다");
  }
}

async function readInfo() {
  try {
    const dpid = await target.readDP(0);           // DPIDR
    $("i-idcode").textContent = hex32(dpid);

    await readMemRetry(0x00000000);                // 접근이 되는지 먼저 본다
    const info = await readTargetInfo(target);
    $("i-part").textContent = `${info.part} (${info.partName})`;
    $("i-devid").textContent = info.deviceId;
    $("i-mem").textContent = info.mem;
    $("i-vec").textContent = info.vector;
    $("i-boot").textContent = info.boot;

    log(`타깃 : ${info.partName}, ${info.boot}`, "ok");
  } catch (e) {
    log(`읽기 실패 : ${e.message || e}`, "err");
    log("[복구] 를 눌러 본다");
  }
}

async function resetTarget() {
  try {
    await target.reset();
    await new Promise((r) => setTimeout(r, 500));
    await target.connect();          // 리셋 뒤에는 DP 를 다시 붙여야 한다
    log("리셋했다", "ok");
  } catch (e) {
    log(`리셋 실패 : ${e.message || e}`, "err");
  }
}

async function programFiles() {
  const files = [...$("hexfile").files];
  if (files.length === 0) return;

  $("program").disabled = true;
  const flasher = new Flasher(target, log);

  try {
    // 파일을 다 구운 뒤에 한 번만 리셋한다.
    // 중간에 리셋하면 dapjs 가 캐시해 둔 SELECT/CSW 가 하드웨어와 어긋나
    // 다음 파일부터 전송이 깨진다.
    for (const file of files) {
      log(`--- ${file.name} ---`);
      const text = await file.text();
      const t0 = performance.now();

      const result = await flasher.program(text, (done, total) => {
        const pct = Math.floor((done * 100) / total);
        $("progress").textContent = `${file.name} : ${pct}%  (${(done / 1024).toFixed(0)} / ${(total / 1024).toFixed(0)} KB)`;
      });

      const sec = (performance.now() - t0) / 1000;
      log(`완료 : ${result.pages} 페이지, ${(result.bytes / 1024).toFixed(0)} KB, ` +
          `${sec.toFixed(1)} 초 (${(result.bytes / 1024 / sec).toFixed(1)} KB/s)`, "ok");
    }

    log("리셋 후 실행");
    await flasher.resetRun();        // 디버그를 떼고 하드웨어 리셋 → 펌웨어가 돈다

    // 여기서 다시 붙으면 코어가 디버그 모드로 돌아간다.
    // 펌웨어가 제대로 도는지 보려면 붙지 않는 편이 낫다 (LED, 시리얼로 확인).
    // 리셋하면 dapjs 의 캐시(SELECT/CSW)가 하드웨어와 어긋난다. 다시 붙여서 맞춘다.
    await target.connect();
    await clearErrors();

    log("굽기 끝. 펌웨어가 돈다 (LED 로 확인. 다시 읽으려면 [다시 읽기])", "ok");
  } catch (e) {
    log(`굽기 실패 : ${e.message || e}`, "err");
  } finally {
    $("program").disabled = false;
    $("progress").textContent = "";
  }
}

$("hexfile").addEventListener("change", () => {
  $("program").disabled = !target || $("hexfile").files.length === 0;
});
$("program").addEventListener("click", programFiles);
$("connect").addEventListener("click", connect);
$("disconnect").addEventListener("click", disconnect);
$("read").addEventListener("click", readInfo);
$("recover").addEventListener("click", recover);

$("erase").addEventListener("click", async () => {
  if (!confirm("칩 전체를 지운다. 펌웨어가 모두 사라진다. 계속할까?")) return;
  try {
    log("전체 삭제 (CTRL-AP)");
    const flasher = new Flasher(target, log);
    await flasher.massErase();
    await target.connect();                      // 리셋 뒤 dapjs 캐시를 다시 맞춘다
    await clearErrors();
    await prepareDp();
    log("전체 삭제 완료. 이제 다시 구울 수 있다", "ok");
    await readInfo();
  } catch (e) {
    log(`전체 삭제 실패 : ${e.message || e}`, "err");
  }
});
$("reset").addEventListener("click", resetTarget);

navigator.usb?.addEventListener("disconnect", (e) => {
  if (device && e.device === device) {
    log("USB 가 빠졌다", "err");
    disconnect();
  }
});

checkSupport();
log("준비됨");
