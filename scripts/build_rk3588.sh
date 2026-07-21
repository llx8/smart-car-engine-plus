#!/bin/bash
# ── RK3588 交叉编译脚本 ──
# 输出目录: build_rk3588/
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="build_rk3588"
TOOLCHAIN="cmake/rk3588-toolchain.cmake"

echo "=== RK3588 交叉编译 ==="
echo "工具链: ${TOOLCHAIN}"
echo "输出:   ${BUILD_DIR}/"

# 清理旧构建
rm -rf "${BUILD_DIR}"

cmake -S . -B "${BUILD_DIR}" -GNinja \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_ACTUATOR_SRV=ON \
    -DBUILD_DASHBOARD=ON \
    -DBUILD_TOOLS=ON \
    -DBUILD_CAR_CORE=ON \
    -DBUILD_TESTS=ON

ninja -C "${BUILD_DIR}" -j"$(nproc)"

echo ""
echo "=== 交叉编译完成 ==="
echo "可执行文件:"
find "${BUILD_DIR}" -maxdepth 3 -type f -executable -name 'car_*' -o -name 'actuator*' -o -name 'can_sim' -o -name 'test_*' | sort

echo ""
echo "部署到板子:"
echo "  scripts/deploy_rk3588.sh  # 默认 ubuntu@192.168.0.102:/home/ubuntu/smartcar"
echo "  或自定义: scripts/deploy_rk3588.sh 192.168.0.103 ubuntu /home/ubuntu/smartcar"
