# NU54-DK 온보드 디버그 프로브 결함 보고 — VCOM0 사용 시 CMSIS-DAP SWD 동작 불능

작성일 2026-09-20 · 대상 펌웨어 `NU54DK_v2_Pre-release` (Build ID `3601db04e83571edc43dbfacacf2cddbe34be5b9`)

## 1. 요약

온보드 디버그 프로브의 **VCOM0(두 번째 가상 시리얼 포트)를 사용하면 같은 프로브의 CMSIS-DAP SWD 기능이 동작하지 않는다.**
정확히는 다음 두 조건 중 하나만 만족해도 이후 모든 SWD 접근이 `No ACK` 로 실패한다.

1. **호스트 → 보드 방향으로 VCOM0 에 데이터를 보낼 때** (프로브가 UART 송신을 수행할 때). 64 바이트로 재현.
2. **타깃이 VCOM0 의 RTS 핀(P0.02)을 구동할 때.** 데이터가 전혀 오가지 않아도 발생.

반대 방향(보드 → 호스트, 512 바이트)은 문제가 없다. 즉 **프로브의 VCOM0 송신 경로 및 RTS 입력 처리가 SWD 서비스와 충돌**하는 것으로 보인다.

**타깃(nRF54L15) 쪽 문제가 아니다.** 같은 상태에서 J4 헤더에 연결한 외부 프로브(NU-DAP)는 메모리 읽기·쓰기·플래시 모두 정상이다.

**영향**: VCOM0 를 사용하는 펌웨어가 한 번 올라가면 **온보드 프로브만으로는 되돌릴 수 없다.**
프로브가 부팅 직후엔 정상이지만 타깃이 VCOM0 를 켜는 순간 SWD 가 죽어, 새 펌웨어를 굽지 못한다.
외부 프로브가 없으면 보드가 사실상 잠긴다.

별개로 **MSD 드라이브에 `.hex` 파일을 복사하면 프로브 펌웨어가 HardFault 로 죽는다** (§6).

## 2. 시험 환경

| 항목 | 값 |
|---|---|
| 보드 | NU54-DK (NUCODE), 타깃 nRF54L15 |
| 프로브 펌웨어 | `NU54DK_v2_Pre-release`, Build ID `3601db04e83571edc43dbfacacf2cddbe34be5b9` |
| HIC ID | `6e052840` |
| 프로브 Unique ID | `5400360300052840a4efca674d33faa2` (SWD 분리 시) / `5415360300052840a4efca674d33faa2` |
| USB 인터페이스 | MSD, CDC, HID, WebUSB |
| 호스트 | macOS 12.7.6 (21H1320) |
| 디버그 도구 | pyOCD 0.42.0, `--target nrf54l` |
| 타깃 펌웨어 | Zephyr 4.4.2 / nRF Connect SDK v3.4.1 |
| 비교용 외부 프로브 | NU-DAP (`5415360300052840209e58a0bc09b442d...`), J4 10핀 연결 |

VCOM0 = 타깃의 `uart30`

| 신호 | 타깃 핀 | 회로도 네트 | 솔더브리지 |
|---|---|---|---|
| TX | P0.00 | `LPUART_TX` | SB5 |
| RX | P0.01 | `LPUART_RX` | SB6 |
| RTS | P0.02 | `LPUART_RTS` | SB7 |
| CTS | P0.03 | `LPUART_CTS` | SB8 |

VCOM1(`uart20`, P1.04~07)은 콘솔로 계속 사용했으며 **문제 없다**. 시험 내내 VCOM1 로 타깃 CLI 를 정상 사용했다.

## 3. 재현 결과 (최소 조건)

모든 행은 같은 보드·같은 프로브에서 연속으로 측정했다. 판정은 pyOCD 로 타깃 메모리를 읽어 확인했다.

```sh
pyocd cmd -t nrf54l -f 1000000 -u <프로브UID> -c "read32 0x0 4"
```

| # | 타깃 펌웨어 / 동작 | VCOM0 상태 | SWD |
|---|---|---|---|
| 1 | LED 점멸 예제 (uart30 미사용) | 비활성 | ✅ 정상 |
| 2 | BLE 펌웨어 (uart30 미사용) | 비활성 | ✅ 정상 |
| 3 | uart30 활성, **TX/RX 만** 사용 (RTS/CTS 는 하이임피던스), 통신 없음 | 활성 | ✅ 정상 |
| 4 | 위 3번 상태에서 **보드 → 호스트 512 바이트 송신** | 활성 | ✅ 정상 |
| 5 | 위 3번 상태에서 **호스트 → 보드 64 바이트 송신** | 활성 | ❌ **No ACK** |
| 6 | uart30 활성, **RTS/CTS 포함** (기본 pinctrl), 통신 없음 | 활성 | ❌ **No ACK** |

### 판정 근거

3번(정상):

```
00000000:  200037b8                               | .7.|
```

5번(불능) — 호스트에서 VCOM0 로 64 바이트를 쓴 직후:

```
0000411 E Error while initing target: SWD/JTAG communication failure (No ACK);
          check USB cable, reduce debugger clock [commander]
```

6번(불능) — 타깃이 부팅하며 RTS 를 구동한 직후. 데이터는 전혀 오가지 않았다:

```
0000342 E Error reading AP#2 IDR: SWD/JTAG communication failure (No ACK) [discovery]
```

### 핀 상태 변화

`uart30` 이 활성화될 때 각 핀은 다음과 같이 바뀐다 (Zephyr pinctrl 의 sleep → default 전환).

| 핀 | 활성화 전 | 활성화 후 |
|---|---|---|
| P0.00 TX | 하이임피던스 (`low-power-enable`) | 출력, High (UART idle) |
| P0.02 RTS | 하이임피던스 | **출력, Low** (RTS 어서트) |
| P0.01 RX | 하이임피던스 | 입력 + 풀업 |
| P0.03 CTS | 하이임피던스 | 입력 + 풀업 |

6번 항목은 **P0.02 가 하이임피던스에서 Low 구동으로 바뀌는 것만으로** 발생한다.

## 4. 재현 절차

1. 타깃에 `uart30`(VCOM0)을 사용하는 펌웨어를 굽는다. pinctrl 은 TX/RX 만 사용하도록 한다 (RTS/CTS 제외).
2. 보드 USB 를 다시 연결한다. 프로브는 정상 부팅하며 `DETAILS.TXT` 에 `Target Detect: nRF54L15`, `ASSERT.TXT` 없음.
3. `pyocd cmd -t nrf54l -u <UID> -c "read32 0x0 4"` → **정상 동작 확인**.
4. 호스트에서 VCOM0(CDC) 포트를 열고 64 바이트를 쓴다.
5. 3번 명령을 다시 실행 → **`No ACK` 로 실패**.
6. 보드 USB 를 다시 연결하면 3번까지는 다시 정상으로 돌아간다 (4번을 하면 다시 실패).

RTS 조건(6번 항목)은 pinctrl 에 RTS/CTS 를 포함해 굽기만 하면 되며, 4번 단계 없이 부팅 직후 바로 재현된다.

## 5. 정상 동작 확인 (타깃 문제가 아님)

같은 타깃·같은 펌웨어 상태에서 J4 헤더에 외부 프로브(NU-DAP)를 연결하면 모든 동작이 정상이다.

- `read32 0x0` → `20006ca0` 정상 읽힘
- MCUboot 57 KB + 애플리케이션 250 KB 플래시 성공
- 리셋 후 정상 부팅 확인

또한 프로브가 SWD 불능 상태일 때도 **CDC(VCOM0/VCOM1)는 계속 정상 동작**한다.
실제로 SWD 가 죽은 상태에서 VCOM0 를 통해 MCUmgr(SMP) 프로토콜로 이미지 목록 조회까지 성공했다.
즉 프로브의 USB·UART 경로는 살아 있고 **CMSIS-DAP 서비스만 동작하지 않는다.**

## 6. 별개 문제 — MSD 드라이브에 `.hex` 복사 시 HardFault

드라이브(`NU54V2PRE`)에 Intel HEX 파일을 복사하면 프로그래밍이 시작되지 않고(`Last Flash Result: NONE`)
프로브 펌웨어가 즉시 죽는다. 2회 재현했다. 복사 직후 생성되는 `ASSERT.TXT`:

```
Assert
File: /workspace/source/daplink/HardFault_Handler.c
Line: 67
Source: Application
Hexdumps
fffffffd
20002e78
2003fec8
00000000
20002e98
00013d89
05017f48
21000000
40000000
00000100
00000000
00000000
e000ed34
e000ed38
fffffffd
20002e78
```

이 상태가 되면 프로브가 USB 에서 사라지거나 모든 기능이 멈추며, USB 재연결로만 복구된다.
복사한 파일은 MCUboot + 애플리케이션을 병합한 약 840 KB 의 Intel HEX (주소 `0x0`~`0x4CEC7`) 였다.

## 7. 요청 사항

1. **VCOM0 송신 경로와 CMSIS-DAP 서비스의 자원 충돌 여부 확인** — 인터럽트 우선순위, 공유 타이머/DMA, 크리티컬 섹션 등.
   VCOM1 은 같은 조건에서 문제가 없으므로 두 UART 인스턴스의 처리 차이를 비교하면 좁혀질 것으로 보인다.
2. **RTS 입력 처리 확인** — 타깃이 RTS 를 Low 로 구동하는 것만으로 SWD 가 죽는다. 흐름 제어 관련 처리가 SWD 와 얽혀 있는지.
3. **MSD HEX 복사 시 HardFault 수정** (§6).
4. 수정된 인터페이스 펌웨어 제공 및 **MAINTENANCE 모드 진입 방법**(프로브 리셋 버튼 위치/절차) 안내.

## 8. 현재 회피 방법 (참고)

- VCOM0 를 사용하지 않으면 온보드 프로브는 완전히 정상이다. 콘솔은 VCOM1 하나로 충분하다.
- VCOM0 가 필요한 경우(예: MCUmgr 시리얼 DFU) 외부 프로브로 굽고 디버깅해야 한다.
- 보드에 `SW1 DISABLE_SWD` 스위치가 있어 온보드 프로브를 SWD 버스에서 분리할 수 있다.
  외부 프로브와 동시에 연결할 때 버스 충돌을 피하려면 이 스위치를 ON 으로 둔다.
