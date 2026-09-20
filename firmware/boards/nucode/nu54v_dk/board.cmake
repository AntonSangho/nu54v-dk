# 기본 러너: 보드에 실장된 CMSIS-DAP(nRF52840) → pyOCD
# 외부 J-Link를 SWD 커넥터(J4/P3)에 연결한 경우: west flash -r jlink / -r nrfutil
board_runner_args(pyocd "--target=nrf54l" "--frequency=4000000")
# --dt-flash 는 끈다.
#
# 공통 pyocd.board.cmake 가 --dt-flash=y 를 기본으로 넣는데, 그러면 러너가
# CONFIG_FLASH_LOAD_OFFSET 을 읽는다. MCUboot(sysbuild + DTS 파티션) 빌드에서는
# 이 심볼이 아예 생성되지 않아 KeyError 로 죽는다 (주소는 DT 에서 바로 링커로 간다).
# 우리는 항상 hex 를 굽고 hex 안에 주소가 들어 있으므로 -a 옵션이 필요 없다.
board_runner_args(pyocd "--dt-flash=n")
board_runner_args(jlink "--device=nRF54L15_M33" "--speed=4000")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
