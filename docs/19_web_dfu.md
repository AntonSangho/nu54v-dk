# 19. 웹 업데이트 도구 (`web/`)

<https://chcbaram.github.io/nu54v-dk/>

브라우저에서 바로 굽는 정적 페이지 하나. 사용자는 아무것도 설치하지 않는다.
GitHub Pages 가 HTTPS 라 아래 세 API 의 요구 조건을 만족한다.

| 탭 | API | 쓰는 때 | 빈 보드 | 실측 |
|---|---|---|---|---|
| **SWD** | WebUSB + CMSIS-DAP (dapjs) | 부트로더+앱 전체 설치, 벽돌 복구 | **가능** | 304 KB / 9.5 초 |
| **BLE** | Web Bluetooth + SMP | 앱 무선 업데이트 | 불가 | 249 KB / 15.8 초 |
| **시리얼** | Web Serial + SMP | 앱 유선 업데이트 | 불가 | 249 KB / 42 초 |

로그는 탭 아래에 계속 보인다. [자세히] 를 켜면 구간별 속도와 보드 계수기까지 남는다.

**되는 브라우저** — WebUSB·Web Serial 은 Chrome / Edge 데스크톱 (WebUSB 는 Android 도),
Web Bluetooth 는 여기에 Opera 추가. **Safari · Firefox · iOS 는 전부 안 된다.**

## 1. 파일 구성

```
web/index.html          페이지. js 태그에 ?v=<빌드시각> (캐시 버스터)
web/js/main.js          화면·흐름. 세 탭의 버튼과 로그
web/js/flash.js         SWD 굽기 (dapjs 위)
web/js/flash_algo.js    nRF54L15 RRAM 쓰기 루틴 (pyOCD 에서 가져옴)
web/js/nrf54l.js        타깃 정보 읽기 (FICR, 파티션, 벡터 테이블)
web/js/smp.js           SMP 프로토콜 + CBOR (직접 만듦)
web/js/ble.js           SMP 전송 계층 — Web Bluetooth
web/js/serial.js        SMP 전송 계층 — Web Serial
web/fw/                 페이지에 실은 예제 이미지 + manifest.json
web/selftest.js         보드 없이 도는 회귀 시험
web/pack_fw.py          예제 이미지를 web/fw/ 로 싣는다
web/bump_version.py     ?v= 갱신
.github/workflows/pages.yml   web/ 를 Pages 로 배포
```

SMP 와 CBOR 는 직접 만들었다. **전송 계층만 갈아 끼우면 된다** — `ble.js` 와 `serial.js` 는
`send(bytes)` 와 `onPacket(콜백)` 만 제공하고, 나머지는 `smp.js` 가 한다.

## 2. SWD (WebUSB) — 빈 보드도 굽는다

보드의 프로브가 WebUSB 를 내놓는다 (`DETAILS.TXT` → `USB Interfaces: MSD, CDC, HID, WebUSB`).
MSD 드래그앤드롭이 실패하는 것은 DAPLink 의 *타깃 인식* 쪽이고 CMSIS-DAP 자체는 멀쩡하다.

nRF54L15 는 RRAM 이라 쓰기 루틴이 아주 작다. pyOCD 의 `FLASH_ALGO` 는 명령어 240 바이트
남짓이고 RRAM 컨트롤러(`0x5004B000`)를 찔러 워드 단위로 쓰는 게 전부다
(`page_size: 0x4`, erase 없음). 그대로 dapjs 에 실었다.

> 출처 : `pyocd/target/builtin/target_nRF54L15.py`, `pyocd/target/family/target_nRF54L.py`
> (CTRL-AP 로 ERASEALL / APPROTECT 해제하는 부분도 여기 있다)

### 함정 1 — dapjs 의 버그 세 가지

브라우저에서만 실패하고 pyOCD 로 같은 순서를 재현하면 잘 되어서, Node 에서 dapjs 를
그대로 돌려 좁혔다 (`usb` 패키지의 WebUSB 폴리필 → 브라우저와 같은 경로).

| 문제 | 증상 | 우회 |
|---|---|---|
| **`writeBlock` 이 256 워드까지만 맞다** | 그보다 크면 안에서 나눠 보내며 **주소를 진행시키지 않아** 덩어리가 모두 같은 자리에 겹쳐 쓰인다. 1024 워드를 쓰면 0 번째 자리에 768 번째 값이 들어온다 | 256 워드씩 나눠 쓴다 (`writeBlockChunked`) |
| `waitDelay(fn, timeout, interval)` | interval 이 아니라 timeout 만큼 잠든다. 알고리즘 호출 한 번에 최대 10 초 | `execute()` 를 쓰지 않고 직접 폴링 (2 ms). 0.4 → 29.4 KB/s |
| `SELECT` / `CSW` 캐시 | 타깃을 리셋하면 하드웨어는 초기화되는데 캐시가 남아 이후 전송이 엉뚱한 AP·뱅크로 간다. `connect()` 도 지우지 않는다 | 리셋할 때마다 `selectedAddress`/`cswValue` 를 버린다 |

**첫 번째가 결정적이었다.** 굽기는 "성공" 하는데 내용이 쓰레기라, MCUboot 가 부팅할 이미지를
못 찾고 멈추고 → APPROTECT 가 걸린 채 남아 → 디버그 접근까지 막혔다.

### 함정 2 — APPROTECT, 즉 "코어를 못 찾는다"

**AHB-AP 가 비활성이면 APPROTECT 가 걸린 것이다.** nRF54L 은 리셋할 때 걸린 채 부팅하고,
정상 펌웨어가 돌면서 해제한다. 그래서 **펌웨어가 돌지 않으면**(패닉, 이미지 없음, 코어 halt)
디버그가 막힌다. 빠져나오려면 CTRL-AP 전체 삭제뿐이다 ([전체 삭제] 버튼, `pyocd erase --mass`).

지키는 것 셋 :

- **MCUboot 만 굽지 않는다** → `build/merged.hex` 를 쓴다. 부팅할 앱이 없으면 그대로 벽돌이 된다
- 굽기 뒤에 코어를 halt 로 두지 않는다. 먼저 `C_HALT` 를 풀고 리셋한다
  (halt 상태에서 `C_DEBUGEN` 을 0 으로 쓰면 ARM 사양상 동작 미정의)
- 리셋은 **CTRL-AP RESET 에 2 → 0** (pyOCD 와 같다). `AIRCR.SYSRESETREQ` 가 아니다

### 함정 3 — `writeDP`/`readDP` 는 바이트 주소를 받는다

인덱스가 아니다. `DP_CTRL_STAT` 는 1 이 아니라 **0x04** 다.

## 3. BLE / 시리얼 (SMP)

### 조각 크기는 전송 계층이 정한다

한 값으로 묶으면 한쪽이 깨진다. 실제로 시리얼을 맞추다 BLE 를 망가뜨렸다.

| | 조각 | 패킷 상한 | 왜 |
|---|---|---|---|
| **BLE** | 160 | **241** (MTU 244 − 3) | 패킷 하나가 **한 번의 write** 에 통째로 들어가야 한다 |
| **시리얼** | 512 | 1222 (보드 net_buf 1230 − 8) | 줄로 쪼개 보내고 보드가 다시 잇는다 |

**첫 요청만 `len` 과 `sha`(32 B)를 같이 실어 약 75 바이트 커진다.** 이것을 잊으면

```
조각 160 → 첫 패킷 230 B  ✓        조각 200 → 첫 패킷 275 B  ✗ (241 초과)
         평소   183 B                       평소   232 B  ✓
```

평소 조각은 멀쩡한데 **첫 조각에서만** 멈춘다. 넘치면 브라우저가 잘라 보내고 보드는
불완전한 패킷을 기다리기만 하므로 **오류 없이 "응답이 오지 않는다"** 로만 보인다.
`upload()` 는 첫 조각을 `chunkSize - 96` 으로 줄여 여유를 만들고,
`ble.js` 는 `maxPacket` 으로 넘치는 것을 먼저 막는다.

> Web Bluetooth 는 MTU 를 알려주지 않는다. BLE 상한은 보드에서 협상되는 값
> (`ble info` → `mtu`)을 코드에 적어 둔 것이다. 보드 BLE 설정을 바꾸면 여기도 바꾼다.

### 시리얼 프레이밍에서 걸린 것

- **base64 는 줄마다 따로 인코딩한 것이 아니라 하나의 연속 문자열을 줄로 쪼갠 것**이다
  (마지막 줄에만 `=` 패딩). 받을 때도 문자열을 먼저 이어 붙인 뒤 디코딩한다.
  줄마다 디코딩하면 아무것도 안 나온다
- 보드는 길이를 미리 주지 않는 CBOR(indefinite length)로 답한다. 그것도 읽어야 한다
- **표식이 줄 첫머리에 있다고 가정하면 안 된다.** cli 는 프롬프트(`cli# `)를 줄바꿈 없이
  찍으므로 한 줄에 `cli# ` + `0x06 0x09` + base64 가 같이 온다. 표식 위치를 찾아
  앞부분은 cli 출력으로 넘긴다
- **cli 의 엔터는 CR(0x0D)** 이다. SMP 프레임이 쓰는 LF 와 다르다
- 프레임은 바이트로 봐야 하지만 **cli 출력은 UTF-8 한글**이다. 텍스트로 넘길 때만 다시 푼다
  (안 그러면 `(평균/최대)` 가 `(íê· /ìµë)` 로 나온다)
- Chrome 의 Web Serial 읽기 버퍼 기본값은 **255 바이트**고 넘치면 버린다 → `bufferSize: 16384`

### 쓰기 페이싱 — 프로브가 버스트를 못 받는다

프로브는 USB(12 Mbps)로 받은 것을 115200 으로 흘려보낸다. 흐름제어가 없어 한 번에 너무
많이 쓰면 내부 버퍼가 넘치고 **조용히 버리거나 덮어쓴다**. 보드는 `rx drop 0, rx stop 0`
으로 멀쩡하다 — 보드에 닿기 전에 일어난 일이다.

`drainAt`(지금까지 쓴 것이 다 나갈 시각)을 따라가며 256 바이트 이상 앞서지 않게 쓴다.
전선이 어차피 병목이라 속도 손해는 없다.

> **파이썬에서는 듣고 브라우저에서는 안 듣는다 (미해결).**
> `dfu_serial_bench.py` 는 페이싱으로 손상이 0 이 되는데(17,543 프레임) 웹은 그대로다
> (약 7,000 프레임에 2 회). `writer.write()` 가 실제 USB 전송 시점을 보장하지 않기
> 때문으로 **추정**하나 확인은 안 했다. 영향은 43.8 초 중 1.2 초(2.7 %) 다.

자세한 실측과 보드 쪽 계측은 [18_dfu.md](18_dfu.md) §9.

### 그 밖

- `rc` 로 거절된 것은 **재전송하지 않는다**. 보드가 상태를 보고 내린 판단이라 다시 보내도 같다
  (`rc=6` 확정 전, `rc=1` 같은 이미지)
- SMP 는 한 번에 한 요청뿐이다. 업로드 중에는 [현재 이미지 확정] 도 막는다
- BLE 는 중앙이 하나뿐이라 **baram-term 등이 연결 중이면 브라우저 장치 목록에 안 보인다**
  (연결 중에는 광고를 멈춘다). 자동화는 `baram-ctl release` → 업데이트 → `resume`
- 광고에 SMP UUID 가 없으므로(NUS UUID 만 실린다) 장치는 **이름**으로 찾는다

## 4. 예제 싣기

파일을 `build/` 에서 찾아 헤매지 않도록 목록에서 바로 고를 수 있다.
세 탭 모두 [목록에서 고르기] 와 [파일 고르기] 가 함께 있다.

```sh
python3 web/pack_fw.py            # 기본 : dfu (merged.hex + signed.bin)
python3 web/pack_fw.py led shtc3  # 예제를 더 넣을 때
```

`web/fw/` 에 이미지를 복사하고 `manifest.json` 을 만든다. 페이지는 그것을 읽어 목록을 채운다.

> **싣는 것을 고를 때** — 이미지는 빌드할 때마다 내용이 통째로 바뀌므로 갱신할 때마다
> git 히스토리가 그만큼 커진다. 예제 14 개를 다 넣으면 4.6 MB 다.
> 기본은 dfu 하나(1.1 MB)로, 이 페이지의 본래 목적인 DFU 흐름만 담았다.

## 5. 고치고 나서 할 일

### 캐시 버스터

```sh
python3 web/bump_version.py
```

`index.html` 의 `?v=` 를 갱신한다. 안 하면 브라우저가 예전 js 를 계속 쓴다 —
**"고쳤는데 그대로다" 의 원인**이라 새로고침에 기대지 않는다. Pages 워크플로는 배포 전에 자동으로 돌린다.

### 회귀 시험 (보드 없이)

```sh
bun run web/selftest.js      # node 도 된다
```

- 두 전송 계층의 **첫 패킷·평소 패킷**이 각자의 한계 안인지
- 시리얼 프레이밍 왕복 (한 덩어리 / 프롬프트가 앞에 붙은 경우 / 1 바이트씩)
- **이 시험이 실제로 회귀를 잡는지** — 조각 512 와 "첫 요청을 안 줄인 200" 이 한계를
  넘는 것을 함께 확인한다. 통과만 하는 시험은 의미가 없다

**한쪽 전송 계층을 만질 때 이것부터 돌린다.** 조각 크기는 둘이 공유하는 `upload()` 를
지나므로 한쪽만 보고 고치면 다른 쪽이 조용히 깨진다. 실제로 그렇게 BLE 를 망가뜨렸다.

## 6. 배포

`.github/workflows/pages.yml` 이 `web/` 를 그대로 Pages 로 올린다
(`main` 에 `web/**` 가 바뀌면 자동, 손으로도 돌릴 수 있다).

저장소 설정에서 **Settings > Pages > Source 를 "GitHub Actions"** 로 한 번 바꿔야 한다.

## 7. UF2 는 하지 않는다

- **nRF54L15 에는 USB 주변장치가 없다** (Zephyr DTS 에 USB 노드가 없다).
  UF2 부트로더는 칩이 스스로 USB 저장장치로 떠야 하므로 성립하지 않는다
- 보드를 꽂으면 보이는 드라이브(`NU54V2PRE`)는 **온보드 DAPLink 프로브**가 만든 것이다.
  `INFO_UF2.TXT` 가 없으니 UF2 모드가 아니라 `.hex`/`.bin` 을 받는다
- 게다가 이 프로브는 타깃을 인식하지 못한다 (`Target Detect: SWD init failed`).
  pyOCD 는 CMSIS-DAP 명령으로 직접 SWD 를 다루므로 잘 된다
- Zephyr 의 `CONFIG_BUILD_OUTPUT_UF2` 는 family ID 목록에 nRF54L 이 없다

**결론** — 초기 설치·복구는 `fw flash`(SWD), 현장 업데이트는 SMP, 사용자 업데이트는 웹.
UF2 / MSD 경로는 만들지 않는다.
