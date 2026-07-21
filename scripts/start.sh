#!/bin/bash
# ── Smart Car Engine Plus 启动脚本 ──
# 启动顺序: vcan0 → can_sim → actuator_srv → car_core(→ fork car_dashboard)
# 崩溃恢复: actuator_srv 由 while true 兜底重启, car_core 内部管理 car_dashboard

# Qt 显示后端 — VNC server 模式，监听 0.0.0.0:5900
# Windows 端用 tightVNC 客户端连 板子IP:5900 即可看到 car_dashboard 仪表盘
export QT_QPA_PLATFORM=vnc

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Smart Car Engine Plus — 启动 ==="
echo "工作目录: $(pwd)"

# Step 1: 创建 vcan0
if ! ip link show vcan0 &>/dev/null; then
    echo "[start] 创建 vcan0 ..."
    sudo ip link add dev vcan0 type vcan
    sudo ip link set up vcan0
else
    echo "[start] vcan0 已存在"
    sudo ip link set up vcan0 2>/dev/null || true
fi

# Step 2: 清理残留 socket 文件
rm -f /tmp/car_core.sock /tmp/car_actuator.sock

# Step 3: 启动 CAN 模拟器（后台）
echo "[start] 启动 can_sim ..."
./tools/can_sim &
CAN_SIM_PID=$!

# Step 4: 启动 actuator_srv（带自动重启兜底）
echo "[start] 启动 actuator_srv ..."
(
    RESTART_COUNT=0
    while true; do
        ./actuator_srv/actuator_srv
        RESTART_COUNT=$((RESTART_COUNT + 1))
        echo "[start] actuator_srv 退出 (已重启 $RESTART_COUNT 次), 1 秒后重试..."
        sleep 1
        # 崩溃保护: 连续 5 次崩溃则停止重启
        if [ $RESTART_COUNT -ge 5 ]; then
            echo "[start] actuator_srv 连续崩溃 $RESTART_COUNT 次，停止重启"
            exit 1
        fi
    done
) &
ACTUATOR_PID=$!

# 等 actuator_srv 就绪
sleep 1

# Step 5: 启动 car_core（带自动重启兜底,崩溃保护）
echo "[start] 启动 car_core ..."
set +e   # car_core 崩溃时不应终止整个脚本,while 循环负责重启
(
    CORE_RESTART=0
    while true; do
        ./car_core/car_core
        CORE_RESTART=$((CORE_RESTART + 1))
        echo "[start] car_core 退出 (已重启 $CORE_RESTART 次, exit=$?), 1 秒后重试..."
        sleep 1
        if [ $CORE_RESTART -ge 5 ]; then
            echo "[start] car_core 连续崩溃 $CORE_RESTART 次，停止重启"
            exit 1
        fi
        # 重拉 car_dashboard 前先清理残留共享内存
        if [ -f /tmp/car_core.shmid ]; then
            read SHMID _ < /tmp/car_core.shmid
            ipcrm -m "$SHMID" 2>/dev/null || true
            rm -f /tmp/car_core.shmid
        fi
        rm -f /tmp/car_core.sock
    done
) &
CORE_PID=$!

# 等待 car_core 和 car_dashboard 就绪
sleep 2

echo ""
echo "=== 全链路就绪: car_core(PID=$CORE_PID) / can_sim(PID=$CAN_SIM_PID) / actuator_srv(PID=$ACTUATOR_PID) ==="
echo "   打开 tightVNC 连接到 $(hostname -I | awk '{print $1}'):5900 即可看到仪表盘"
echo "   按 Ctrl+C 退出并清理"
echo ""

# 前台等待 car_core 异常退出后再清理
wait $CORE_PID 2>/dev/null
CORE_EXIT=$?

echo ""
echo "=== car_core 退出 (exit=$CORE_EXIT), 清理中... ==="

# 清理
kill $CAN_SIM_PID 2>/dev/null || true
kill $ACTUATOR_PID 2>/dev/null || true
sudo ip link set down vcan0 2>/dev/null || true
sudo ip link delete vcan0 2>/dev/null || true
rm -f /tmp/car_core.sock /tmp/car_actuator.sock

echo "=== 清理完成 ==="
exit $CORE_EXIT
