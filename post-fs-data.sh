#!/system/bin/sh
# post-fs-data 早期：确保 Zygisk 库权限正确，KernelSU 才能 dlopen
MODDIR="${0%/*}"
[ -f "$MODDIR/zygisk/arm64-v8a.so" ] && chmod 0644 "$MODDIR/zygisk/arm64-v8a.so"
# 配置目录尽早可用
mkdir -p /data/adb/VirtualCam /sdcard/VirtualCam 2>/dev/null
exit 0
