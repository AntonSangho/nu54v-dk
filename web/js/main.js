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

/* 코어를 리셋해 멈춘 상태로 만든다. 상태가 꼬였을 때 쓴다. */
async function recover() {
  try {
    log("복구 : 오류 지우고 리셋 후 halt");
    await clearErrors();
    const flasher = new Flasher(target, log);
    await flasher.resetHalt();
    await clearErrors();
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
    log("굽기 끝. 펌웨어가 돈다 (다시 읽으려면 [다시 읽기])", "ok");
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
$("reset").addEventListener("click", resetTarget);

navigator.usb?.addEventListener("disconnect", (e) => {
  if (device && e.device === device) {
    log("USB 가 빠졌다", "err");
    disconnect();
  }
});

checkSupport();
log("준비됨");
