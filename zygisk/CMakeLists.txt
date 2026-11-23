cmake_minimum_required(VERSION 3.22)
project(VirtualCam LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ShadowHook 源码由 workflow 用 actions/checkout 放到 ../third_party/shadowhook。
# CMake 全程只读本地目录，绝不做 git clone（CI 子构建无凭据会失败）。
# 不调用 ShadowHook 自带 CMakeLists：其内容随仓库版本变化，configure 失败不好排查。
# 这里精确筛选 arm64 需要的源码，排除 demo/unittest/32位代码。
set(SH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/shadowhook")
set(SH_CPP "${SH_ROOT}/shadowhook/src/main/cpp")
if(NOT EXISTS "${SH_CPP}")
    message(FATAL_ERROR "缺少 ${SH_CPP}，请检查 workflow 的 Checkout ShadowHook source 步骤")
endif()

# 头文件目录（include/、common/、third_party/ 都可能在里面）
file(GLOB_RECURSE SH_HDRS "${SH_CPP}/*.h")
set(SH_INC_DIRS "")
foreach(h IN LISTS SH_HDRS)
    get_filename_component(hd "${h}" DIRECTORY)
    list(APPEND SH_INC_DIRS "${hd}")
endforeach()
list(REMOVE_DUPLICATES SH_INC_DIRS)

# 源码：根层核心 + arm64 架构层 + common + third_party(xdl 等)
file(GLOB SH_SRCS
    "${SH_CPP}/*.c"
    "${SH_CPP}/arch/arm64/*.c"
    "${SH_CPP}/common/*.c"
)
file(GLOB_RECURSE SH_THIRD "${SH_CPP}/third_party/*.c")
list(APPEND SH_SRCS ${SH_THIRD})

# arm64 可能有汇编源文件，一并纳入
file(GLOB SH_ASM "${SH_CPP}/arch/arm64/*.S" "${SH_CPP}/common/*.S")
if(SH_ASM)
    enable_language(ASM)
    list(APPEND SH_SRCS ${SH_ASM})
endif()

list(REMOVE_DUPLICATES SH_SRCS)
# 防御性排除（用多条简单正则，不依赖 CMake 正则方言的分组/或语法）。
# 关键：不含 arch/arm64，只排除 32 位 arch/arm。
foreach(bad "app/src/main/cpp" "/arch/arm/" "/arch/x86" "/unittest/"
            "/hookee" "/tests/" "/test/" "/demo/" "/sample/"
            "/example/" "/benchmark/")
    list(FILTER SH_SRCS EXCLUDE REGEX "${bad}")
endforeach()
list(LENGTH SH_SRCS SH_N)
if(SH_N EQUAL 0)
    message(FATAL_ERROR "未筛出 ShadowHook 源码，目录=${SH_CPP}")
endif()
message(STATUS "ShadowHook 源文件数: ${SH_N}")
foreach(s IN LISTS SH_SRCS)
    message(STATUS "  SH: ${s}")
endforeach()

add_library(VirtualCam SHARED module.cpp ${SH_SRCS})
target_compile_definitions(VirtualCam PRIVATE USE_SHADOWHOOK)
target_include_directories(VirtualCam PRIVATE . ${SH_INC_DIRS})
target_link_libraries(VirtualCam PRIVATE log android mediandk dl)

# Zygisk 规范要求文件名就是 arm64-v8a.so（不能带 lib 前缀），
# 否则 workflow 里 cp build/arm64-v8a.so 会找不到文件。
set_target_properties(VirtualCam PROPERTIES
    OUTPUT_NAME "arm64-v8a"
    PREFIX ""
)
