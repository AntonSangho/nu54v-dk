# 기본 러너: 보드에 실장된 CMSIS-DAP(nRF52840) → pyOCD
# 외부 J-Link를 SWD 커넥터(J4/P3)에 연결한 경우: west flash -r jlink / -r nrfutil
board_runner_args(pyocd "--target=nrf54l" "--frequency=4000000")
board_runner_args(jlink "--device=nRF54L15_M33" "--speed=4000")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
