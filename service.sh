#!/system/bin/sh
# late_start 阶段：确保素材/配置就绪。真正 Hook 在 Zygisk 注入相机进程后生效。
MODDIR="${0%/*}"
CFG=/data/adb/VirtualCam
mkdir -p /sdcard/VirtualCam "$CFG" 2>/dev/null

# 首次初始化默认配置（enabled=0 默认关闭，避免装完就替换吓一跳）
if [ ! -f "$CFG/enabled" ]; then
  echo 0 > "$CFG/enabled"
fi
if [ ! -f "$CFG/front_mirror" ]; then
  echo 1 > "$CFG/front_mirror"
fi

# 权限：让相机进程可读素材与配置
chcon -R u:object_r:system_data_file:s0 "$CFG" 2>/dev/null || true
chmod -R 0644 "$CFG" 2>/dev/null || true
chmod 0755 "$CFG" 2>/dev/null || true

# 同步 disable 标志（enabled=0 -> 建 disable，原生侧也读它）
if [ "$(cat "$CFG/enabled" 2>/dev/null)" = "1" ]; then
  rm -f /sdcard/VirtualCam/disable 2>/dev/null
else
  touch /sdcard/VirtualCam/disable 2>/dev/null
fi

# 支持 WebUI 直接改配置：在 $CFG 写入当前启用的源
[ -f "$CFG/current" ] || echo "virtual.mp4" > "$CFG/current"
exit 0
