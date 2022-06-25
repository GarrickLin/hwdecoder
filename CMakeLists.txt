cmake_minimum_required(VERSION 3.14)
project(h264decoder)

set(CMAKE_CXX_STANDARD 11)

set (BuildExamples False CACHE BOOL "Build the examples")

# Not working on Windows
# find_package(PkgConfig REQUIRED)
# pkg_check_modules(libswscale REQUIRED libswscale)
# pkg_check_modules(libavutil REQUIRED libavutil)
# pkg_check_modules(libavcodec REQUIRED libavcodec)
#
# Not on Ubuntu ...
# find_package(FFMPEG REQUIRED swscale avutil avcodec)

# So we do it more low level
find_path( AVCODEC_INCLUDE_DIR libavcodec/avcodec.h )
find_library( AVCODEC_LIBRARY avcodec )
find_path( AVUTIL_INCLUDE_DIR libavutil/avutil.h )
find_library( AVUTIL_LIBRARY avutil )
find_path( SWSCALE_INCLUDE_DIR libswscale/swscale.h )
find_library( SWSCALE_LIBRARY swscale )


set(FFMPEG_LIBRARIES ${AVCODEC_LIBRARY} ${AVUTIL_LIBRARY} ${SWSCALE_LIBRARY})
set(FFMPEG_INCLUDE_DIR ${AVCODEC_INCLUDE_DIR} ${AVUTIL_INCLUDE_DIR} ${SWSCALE_INCLUDE_DIR})


find_package(pybind11)
if(pybind11_FOUND)
  message("Using existing pybind11 v${pybind11_VERSION}")
else()
  message("Fetching pybind11")
  include(FetchContent)
  FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://gitee.com/mirrors/pybind11
    GIT_TAG v2.5.0
    GIT_SUBMODULES ""
    )
  FetchContent_MakeAvailable(pybind11)
endif()


set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 14)
if (NOT WIN32)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wpedantic")
endif()

aux_source_directory(src decoder_src)

set(hw_decoder_src src/hwdecoder.cpp)
add_library(hwdecoderlib STATIC ${hw_decoder_src})
target_link_libraries(hwdecoderlib PUBLIC ${FFMPEG_LIBRARIES})
target_include_directories(hwdecoderlib PUBLIC ${FFMPEG_INCLUDE_DIR})
target_include_directories(hwdecoderlib PUBLIC "src")

pybind11_add_module(hwdecoder python/hwdecoder_python.cpp)
target_link_libraries(hwdecoder PRIVATE hwdecoderlib)

if (BuildExamples)
  add_subdirectory(examples)
endif ()

find_path( AVFORMAT_INCLUDE_DIR libavformat/avformat.h )
find_library( AVFORMAT_LIBRARY avformat )

add_executable(hw_decode tests/hw_decode.c)
target_link_libraries(hw_decode PUBLIC hwdecoderlib ${AVFORMAT_LIBRARY})
target_include_directories(hw_decode PUBLIC ${AVFORMAT_INCLUDE_DIR})

add_executable(test_hw_decoder tests/test_hw_decoder.cpp)
target_link_libraries(test_hw_decoder PUBLIC hwdecoderlib)