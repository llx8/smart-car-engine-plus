# ── CMake 交叉编译工具链：RK3588 (ARM64 / aarch64) ──
# 用法: cmake -B build_rk3588 -GNinja -DCMAKE_TOOLCHAIN_FILE=cmake/rk3588-toolchain.cmake

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# 交叉编译器
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# sysroot: 包含目标平台的所有头文件和库
set(CMAKE_SYSROOT /opt/rk3588-sysroot)

# 查找策略：只在 sysroot 里找库和头文件
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Qt5 cmake 配置路径
set(Qt5_DIR /opt/rk3588-sysroot/usr/lib/aarch64-linux-gnu/cmake/Qt5)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8.2-a+crypto -mtune=cortex-a76.cortex-a55")
