#
# Copyright (c)2021- Hermit Retro Products Ltd. <http://www.hermitretro.com>
#

cmake_minimum_required (VERSION 2.8)

project (spi-fat-fuse)

include_directories(${PROJECT_SOURCE_DIR}/src)

add_compile_options(-D_FILE_OFFSET_BITS=64 -DBCM2835_IGNORE_DEV_MEM=1)

list(APPEND SOURCES
"${CMAKE_CURRENT_LIST_DIR}/src/bcm2835.c"
"${CMAKE_CURRENT_LIST_DIR}/src/ff.c"
"${CMAKE_CURRENT_LIST_DIR}/src/sdmm.c"
"${CMAKE_CURRENT_LIST_DIR}/src/spi-fat-fuse.c"
)

add_executable(spi-fat-fuse ${SOURCES})
target_link_libraries(spi-fat-fuse fuse3)
