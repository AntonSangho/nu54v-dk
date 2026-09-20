cmake_minimum_required(VERSION 3.20.0)

# 보드 패키지(firmware/boards)는 fw 스크립트가 -DBOARD_ROOT 로 전달한다.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(adc)

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
)

# MODULE_DEF 로 선언한 모듈을 .module 섹션에 모은다.
zephyr_linker_sources(SECTIONS src/bsp/ldscript/ldscript.ld)
