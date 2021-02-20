#
# Copyright (c)2021- Alligator Descartes <http://www.hermitretro.com>
#
# This file is part of spi-fat-fuse.
#
#     spi-fat-fuse is free software: you can redistribute it and/or modify
#     it under the terms of the GNU General Public License as published by
#     the Free Software Foundation, either version 3 of the License, or
#     (at your option) any later version.
#
#     spi-fat-fuse is distributed in the hope that it will be useful,
#     but WITHOUT ANY WARRANTY; without even the implied warranty of
#     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#     GNU General Public License for more details.
#
#     You should have received a copy of the GNU General Public License
#     along with spi-fat-fuse.  If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required (VERSION 2.8)

project (spi-fat-fuse)

include_directories(${PROJECT_SOURCE_DIR}/src)

add_compile_options(-D_FILE_OFFSET_BITS=64 -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast)

list(APPEND FUSE_SOURCES
"${CMAKE_CURRENT_LIST_DIR}/src/bcm2835.c"
"${CMAKE_CURRENT_LIST_DIR}/src/ff.c"
"${CMAKE_CURRENT_LIST_DIR}/src/sdmm.c"
"${CMAKE_CURRENT_LIST_DIR}/src/spi-fat-fuse.c"
)

add_executable(spi-fat-fuse ${FUSE_SOURCES})
target_link_libraries(spi-fat-fuse fuse3)

list(APPEND STRESSSD_SOURCES
"${CMAKE_CURRENT_LIST_DIR}/src/bcm2835.c"
"${CMAKE_CURRENT_LIST_DIR}/src/ff.c"
"${CMAKE_CURRENT_LIST_DIR}/src/sdmm.c"
"${CMAKE_CURRENT_LIST_DIR}/src/stresssd.c"
)

add_executable(stresssd ${STRESSSD_SOURCES})
