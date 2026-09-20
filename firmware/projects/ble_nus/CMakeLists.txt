cmake_minimum_required(VERSION 3.20.0)

# BLE : 스택 + 역할 + 서비스별 Kconfig 조각 (hw_def.h 의 _USE_HW_BLE* 와 짝을 이룬다)
list(APPEND EXTRA_CONF_FILE
  ${CMAKE_CURRENT_LIST_DIR}/../../conf/ble.conf
  ${CMAKE_CURRENT_LIST_DIR}/../../conf/ble_peripheral.conf
  ${CMAKE_CURRENT_LIST_DIR}/../../conf/ble_nus.conf
)

# 보드 패키지(firmware/boards)는 fw 스크립트가 -DBOARD_ROOT 로 전달한다.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(ble_nus)

# 지정한 폴더에 있는 파일만 포함한다.
#
file(GLOB SRC_FILES CONFIGURE_DEPENDS
  src/*.c
  src/ap/*.c
)

# 하위폴더에 있는 파일까지 포함한다.
#
file(GLOB_RECURSE SRC_FILES_RECURSE CONFIGURE_DEPENDS
  src/ap/modules/*.c
  src/bsp/*.c
  src/common/*.c
  src/hw/*.c
)

target_sources(app PRIVATE
  ${SRC_FILES}
  ${SRC_FILES_RECURSE}
)

target_include_directories(app PRIVATE
  src
  src/ap
  src/ap/modules
  src/ap/modules/common
  src/bsp
  src/common
  src/common/core
  src/common/hw/include
  src/hw
  src/hw/driver/ble
)

# MODULE_DEF 로 선언한 모듈을 .module 섹션에 모은다.
zephyr_linker_sources(SECTIONS src/bsp/ldscript/ldscript.ld)
