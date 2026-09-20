/*
 * 보드 없이 돌리는 회귀 시험.
 *
 *   bun run web/selftest.js      (또는 node)
 *
 * 왜 있나 — 조각 크기를 시리얼 기준으로 200 → 512 로 올렸다가 BLE 를 망가뜨렸다.
 * BLE 는 패킷 하나가 한 번의 write 에 들어가야 해서 MTU 에 묶이는데, 첫 요청만
 * len 과 sha 를 같이 실어 75 바이트쯤 커진다는 것을 놓쳤다. 증상은 오류가 아니라
 * "응답이 없다" 였고 보드가 멀쩡한지부터 확인해야 했다.
 *
 * 한쪽을 고칠 때 다른 쪽이 조용히 깨지지 않도록, 두 전송 계층을 함께 검사한다.
 */

const fs = require("fs");
const path = require("path");
const vm = require("vm");

const JS = path.join(__dirname, "js");
const ctx = vm.createContext({
  btoa, atob, console, TextEncoder, TextDecoder, Uint8Array,
  setTimeout, clearTimeout, crypto, performance,
});

for (const [file, names] of [
  ["smp.js", ["cborEncode", "smpRequest", "SmpClient"]],
  ["serial.js", ["SerialSmpTransport", "crc16"]],
  ["ble.js", ["BleSmpTransport"]],
]) {
  const src = fs.readFileSync(path.join(JS, file), "utf8");
  const expose = names.map((n) => `globalThis.${n}=${n};`).join("");
  vm.runInContext(src + "\n" + expose, ctx);
}

let failed = 0;

function check(name, ok, detail) {
  console.log(`${ok ? "  ok  " : "  실패"} ${name}${detail ? "  — " + detail : ""}`);
  if (!ok) failed++;
}

/* 업로드 요청 하나를 실제로 만들어 크기를 잰다 (upload() 와 같은 형태) */
function uploadPacket(chunk, withShaLen, imageLen) {
  const payload = {
    image: 1,
    off: withShaLen ? 0 : imageLen - chunk,   // off 가 클수록 CBOR 이 커진다
    data: new Uint8Array(chunk),
  };
  if (withShaLen) {
    payload.len = imageLen;
    payload.sha = new Uint8Array(32);
  }
  return ctx.smpRequest(2, 1, 1, payload, 0).length;
}

console.log("전송 계층별 패킷 크기 (249 KB 이미지 기준)");

const IMAGE = 254712;

for (const [name, tr] of [
  ["시리얼", new ctx.SerialSmpTransport(() => {})],
  ["BLE", new ctx.BleSmpTransport(() => {})],
]) {
  const chunk = tr.chunkSize;
  const first = Math.max(64, chunk - 96);     // upload() 의 firstChunk 와 같은 식

  const firstPkt = uploadPacket(first, true, IMAGE);
  const steadyPkt = uploadPacket(chunk, false, IMAGE);

  console.log(`\n${name} — 조각 ${chunk}, 한계 ${tr.maxPacket}`);
  check(`${name} 첫 패킷`, firstPkt <= tr.maxPacket,
        `${firstPkt} B (첫 요청은 len·sha 때문에 커진다)`);
  check(`${name} 평소 패킷`, steadyPkt <= tr.maxPacket, `${steadyPkt} B`);
}

/* 시험이 실제로 이번 버그를 잡는지 확인한다 (통과만 하는 시험은 의미가 없다).
 * BLE 조각을 시리얼 값(512)으로 잘못 두면 반드시 걸려야 한다. */
console.log("\n시험이 버그를 잡는지");
{
  const bad = uploadPacket(512, false, IMAGE);
  const ble = new ctx.BleSmpTransport(() => {});
  check("BLE 에 조각 512 를 주면 걸린다", bad > ble.maxPacket,
        `${bad} B > ${ble.maxPacket} B`);

  // 첫 요청을 줄이지 않으면 조각 200 에서도 넘쳤다 (이번에 난 회귀)
  const noShrink = uploadPacket(200, true, IMAGE);
  check("첫 요청을 안 줄이면 조각 200 도 넘친다", noShrink > ble.maxPacket,
        `${noShrink} B > ${ble.maxPacket} B`);
}

/* 시리얼 프레이밍 왕복 — 표식이 줄 중간에 와도(프롬프트가 붙어도) 찾아야 한다 */
console.log("\n시리얼 프레이밍");
{
  const tr = new ctx.SerialSmpTransport(() => {});
  const got = [];
  const lines = [];
  tr.onPacket = (p) => got.push(p);
  tr.writer = { write: async (b) => lines.push(Buffer.from(b)) };

  const pkt = new Uint8Array(538).map((_, i) => i & 0xff);
  tr.send(pkt).then(() => {
    const all = Buffer.concat(lines);

    tr.receive(new Uint8Array(all));
    check("한 덩어리로 받기", got.length === 1 && Buffer.compare(Buffer.from(got[0]), Buffer.from(pkt)) === 0);

    got.length = 0;
    tr.receive(new TextEncoder().encode("\r\ncli# "));   // 줄바꿈 없는 프롬프트
    tr.receive(new Uint8Array(all));
    check("프롬프트가 앞에 붙은 경우", got.length === 1, `markInline ${tr.stats().markInline}`);

    got.length = 0;
    for (const b of all) tr.receive(Uint8Array.of(b));   // 1 바이트씩
    check("1 바이트씩 나뉘어 오는 경우", got.length === 1);

    console.log(failed === 0 ? "\n모두 통과" : `\n${failed} 개 실패`);
    process.exit(failed === 0 ? 0 : 1);
  });
}
