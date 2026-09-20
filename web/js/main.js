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
  }
}

async function resetTarget() {
  try {
    await target.reset();
    log("리셋했다", "ok");
  } catch (e) {
    log(`리셋 실패 : ${e.message || e}`, "err");
  }
}

$("connect").addEventListener("click", connect);
$("disconnect").addEventListener("click", disconnect);
$("read").addEventListener("click", readInfo);
$("reset").addEventListener("click", resetTarget);

navigator.usb?.addEventListener("disconnect", (e) => {
  if (device && e.device === device) {
    log("USB 가 빠졌다", "err");
    disconnect();
  }
});

checkSupport();
log("준비됨");
