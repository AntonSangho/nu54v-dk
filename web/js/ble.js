/*
 * ble.js — SMP over BLE 전송 계층 (Web Bluetooth)
 *
 * SMP Service   8D53DC1D-1DB7-4CD3-868B-8A527460AA84
 * SMP 특성      DA2E7828-FBCE-4E01-AE9E-261174997C48  (write without response + notify)
 *
 * 보내는 쪽은 한 번의 write 에 SMP 패킷 하나가 들어가도록 조각 크기를 잡는다
 * (보드는 재조립도 지원하지만, 브라우저가 MTU 를 알려주지 않아 안전하게 간다).
 * 받는 쪽은 notify 가 여러 번 나뉘어 오므로 헤더의 길이를 보고 다 모일 때까지 기다린다.
 */

/* 한 번의 write 로 보낼 수 있는 패킷의 최대 크기.
 *
 * 브라우저 한계는 512 지만 **실제로 묶는 것은 협상된 MTU** 다 (244 → 241).
 * 512 로 검사하면 241~512 구간이 그대로 통과해 버리고, 증상은 오류가 아니라
 * "응답이 없다" 로 나타나 원인을 찾기 어렵다. 실제 한계로 검사한다.
 *
 * Web Bluetooth 는 MTU 를 알려주지 않으므로 보드에서 협상되는 값을 쓴다
 * (보드 cli 의 `ble info` → mtu). 보드 설정이 바뀌면 여기도 바뀌어야 한다.
 */
const BLE_PACKET_MAX = 241;

const SMP_SERVICE_UUID = "8d53dc1d-1db7-4cd3-868b-8a527460aa84";
const SMP_CHAR_UUID = "da2e7828-fbce-4e01-ae9e-261174997c48";


class BleSmpTransport {
  constructor(log) {
    this.log = log || (() => {});
    this.device = null;
    this.characteristic = null;
    this.onPacket = null;
    this.rx = new Uint8Array(0);

    // 한 번에 올릴 조각 크기. **시리얼보다 훨씬 작아야 한다.**
    //
    // 보내는 쪽은 한 번의 write 에 SMP 패킷 하나가 통째로 들어가야 한다.
    // 협상된 MTU 가 244 면 한 번에 보낼 수 있는 것은 241 바이트다.
    //
    //   조각 160 → 첫 패킷 230 B, 평소 183 B   ✓ 여유 11 B
    //   조각 200 → 첫 패킷 275 B, 평소 232 B   ✗ 첫 패킷이 넘는다
    //
    // 첫 요청만 len 과 sha(32 B)를 함께 실어 약 75 바이트 커진다는 것을 잊기 쉽다.
    // 넘치면 브라우저가 잘라 보내고 보드는 불완전한 패킷을 기다리기만 한다
    // (응답이 없고 첫 조각에서만 멈춘다).
    //
    // 160 은 실기로 확인된 값이다. 시리얼처럼 키우려면 MTU 를 알아야 하는데
    // Web Bluetooth 는 MTU 를 알려주지 않는다. BLE 는 249 KB 에 15.8 초라
    // 더 키울 이유도 없다.
    this.chunkSize = 160;
    this.maxPacket = BLE_PACKET_MAX;
  }

  get isConnected() {
    return this.device !== null && this.device.gatt.connected;
  }

  /*
   * 이름으로 찾는다.
   *
   * 보드는 광고에 NUS UUID 만 싣고 SMP UUID 는 싣지 않는다 (광고 공간이 모자란다).
   * 그래서 services 필터로는 찾을 수 없다. SMP 서비스는 연결한 뒤에 쓰므로
   * optionalServices 로만 선언한다.
   */
  async connect(namePrefix = "NU54") {
    this.device = await navigator.bluetooth.requestDevice({
      filters: [{ namePrefix }],
      optionalServices: [SMP_SERVICE_UUID],
    });
    this.log(`장치 : ${this.device.name || "(이름 없음)"}`);

    this.device.addEventListener("gattserverdisconnected", () => {
      this.log("BLE 연결이 끊겼다");
      this.characteristic = null;
    });

    const server = await this.device.gatt.connect();
    const service = await server.getPrimaryService(SMP_SERVICE_UUID);
    this.characteristic = await service.getCharacteristic(SMP_CHAR_UUID);

    await this.characteristic.startNotifications();
    this.characteristic.addEventListener("characteristicvaluechanged", (e) => {
      this.receive(new Uint8Array(e.target.value.buffer));
    });
  }

  async disconnect() {
    if (this.device && this.device.gatt.connected) this.device.gatt.disconnect();
    this.device = null;
    this.characteristic = null;
  }

  /* notify 는 나뉘어 온다. 헤더의 길이를 보고 한 패킷이 다 모이면 넘긴다. */
  receive(chunk) {
    const merged = new Uint8Array(this.rx.length + chunk.length);
    merged.set(this.rx);
    merged.set(chunk, this.rx.length);
    this.rx = merged;

    while (this.rx.length >= 8) {
      const bodyLen = (this.rx[2] << 8) | this.rx[3];
      const total = 8 + bodyLen;
      if (this.rx.length < total) break;

      const packet = this.rx.slice(0, total);
      this.rx = this.rx.slice(total);
      if (this.onPacket) this.onPacket(packet);
    }
  }

  async send(packet) {
    if (!this.characteristic) throw new Error("BLE 가 연결되어 있지 않다");

    // MTU 를 넘으면 브라우저가 잘라 보내고, 보드는 불완전한 패킷을 기다리기만
    // 한다. 오류도 없이 "응답이 없다" 로만 보이므로 여기서 먼저 막는다.
    if (packet.length > this.maxPacket) {
      throw new Error(`BLE 패킷이 ${packet.length} 바이트로 한계(${this.maxPacket})를 넘는다. `
        + `한 번의 write 에 통째로 들어가야 하므로 chunkSize 를 줄여야 한다 `
        + `(지금 ${this.chunkSize})`);
    }

    await this.characteristic.writeValueWithoutResponse(packet);
  }
}
