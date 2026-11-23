cmake_minimum_required(VERSION 3.22)
project(VirtualCam LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ShadowHook 源码由 workflow 的 actions/checkout 放到 ../third_party/shadowhook。
# 这里只读本地目录，绝不在 CMake 子构建里做 git clone（CI 子构建无凭据会失败）。
set(SH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/shadowhook")
if(NOT EXISTS "${SH_ROOT}")
    message(FATAL_ERROR "缺少 ${SH_ROOT}，请确认 workflow 的 Checkout ShadowHook source 步骤执行成功")
endif()

# 收集全部头文件所在目录，保证 module.cpp 的 #include <shadowhook.h> 能找到
file(GLOB_RECURSE SH_HDRS "${SH_ROOT}/*.h")
set(SH_INC_DIRS "")
foreach(h IN LISTS SH_HDRS)
    get_filename_component(hd "${h}" DIRECTORY)
    list(APPEND SH_INC_DIRS "${hd}")
endforeach()
list(REMOVE_DUPLICATES SH_INC_DIRS)

# 优先用 ShadowHook 官方 CMake（正确设置宏/架构）；若根目录没有 CMakeLists 或
# 没产出可链接 target，则回退为把源码直接编入本模块。
set(SH_LINK_TARGET "")
if(EXISTS "${SH_ROOT}/CMakeLists.txt")
    add_subdirectory("${SH_ROOT}" shadowhook-build)
    if(TARGET shadowhook_static)
        set(SH_LINK_TARGET shadowhook_static)
    elseif(TARGET shadowhook)
        set(SH_LINK_TARGET shadowhook)
    endif()
endif()

if(SH_LINK_TARGET STREQUAL "")
    file(GLOB_RECURSE SH_SRCS "${SH_ROOT}/*.c")
    list(FILTER SH_SRCS EXCLUDE REGEX "(^|/)(test|tests|demo|sample|example|benchmark)(/|$)")
    if(SH_SRCS STREQUAL "")
        message(FATAL_ERROR "未找到 ShadowHook 源码(*.c)，目录=${SH_ROOT}")
    endif()
    add_library(VirtualCam SHARED module.cpp ${SH_SRCS})
else()
    add_library(VirtualCam SHARED module.cpp)
endif()

target_compile_definitions(VirtualCam PRIVATE USE_SHADOWHOOK)
target_include_directories(VirtualCam PRIVATE . ${SH_INC_DIRS})
target_link_libraries(VirtualCam PRIVATE log android mediandk dl)
if(NOT SH_LINK_TARGET STREQUAL "")
    target_link_libraries(VirtualCam PRIVATE ${SH_LINK_TARGET})
endif()

# Zygisk 规范：库名按 ABI 命名
set_target_properties(VirtualCam PROPERTIES OUTPUT_NAME "arm64-v8a")
