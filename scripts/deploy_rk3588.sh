#!/bin/bash
# ── 部署到 RK3588 板子 ──
set -e

BOARD_IP="${1:-192.168.0.102}"
BOARD_USER="${2:-ubuntu}"
BOARD_DIR="${3:-/home/ubuntu/smartcar}"

BUILD_DIR="build_rk3588"

echo "=== 部署到 ${BOARD_USER}@${BOARD_IP}:${BOARD_DIR} ==="

# 创建目标目录（保留子目录结构，匹配 start.sh 中的 ./tools/xxx 等路径）
ssh "${BOARD_USER}@${BOARD_IP}" "mkdir -p ${BOARD_DIR}/tools ${BOARD_DIR}/car_core ${BOARD_DIR}/car_dashboard ${BOARD_DIR}/actuator_srv ${BOARD_DIR}/conf"

# 传输可执行文件（每个二进制放到对应子目录）
for bin in \
    car_core/car_core \
    car_dashboard/car_dashboard \
    actuator_srv/actuator_srv \
    tools/can_sim \
    tools/car_ctl \
    tools/car_ai; do
    if [ -f "${BUILD_DIR}/${bin}" ]; then
        echo "  scp ${bin} ..."
        scp "${BUILD_DIR}/${bin}" "${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/$(dirname ${bin})/"
    else
        echo "  SKIP ${bin} (not built)"
    fi
done

# 传输启动脚本（放到 ${BOARD_DIR} 根目录，start.sh 内部用 SCRIPT_DIR 自我定位）
echo "  scp scripts/start.sh ..."
scp scripts/start.sh "${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/"

# 传输配置文件
echo "  scp conf/ ..."
scp conf/car_ai.conf "${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/conf/"

# 给所有可执行文件加 +x
ssh "${BOARD_USER}@${BOARD_IP}" "chmod +x ${BOARD_DIR}/start.sh ${BOARD_DIR}/car_core/* ${BOARD_DIR}/car_dashboard/* ${BOARD_DIR}/actuator_srv/* ${BOARD_DIR}/tools/* 2>/dev/null || true"

echo ""
echo "=== 部署完成 ==="
echo "登录板子执行:"
echo "  ssh ${BOARD_USER}@${BOARD_IP}"
echo "  cd ${BOARD_DIR}"
echo "  sudo bash start.sh"
