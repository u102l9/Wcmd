#!/system/bin/sh
# KernelSU Next 安装期校验：仅 arm64 + Zygisk 可用时继续
MODPATH="${MODPATH:-/data/adb/modules/VirtualCamKSU}"

ui_print "- VirtualCam (Zygisk) 安装中..."
ABI="$(getprop ro.product.cpu.abilist)"
case "$ABI" in
  arm64-v8a*) ui_print "- 架构: arm64-v8a OK" ;;
  *) ui_print "! 不支持的架构: $ABI (本模块仅 arm64-v8a)"; abort 2>/dev/null || exit 1 ;;
esac

# 校验 Zygisk 库存在，否则提示
if [ ! -f "$MODPATH/zygisk/arm64-v8a.so" ]; then
  ui_print "! 未发现 zygisk/arm64-v8a.so"
  ui_print "! 请用 GitHub Actions 编译产物，或本地 NDK 编译后放入 zygisk/"
  ui_print "! 继续安装仅会装外壳(WebUI/脚本)，无 Hook 效果"
fi

# 建素材目录
mkdir -p /sdcard/VirtualCam
[ -f /sdcard/VirtualCam/README.txt ] || \
  echo "放这里: virtual.mp4(替换视频)  photo.jpg(替换拍照)  front.jpg(前摄)  建 disable 文件=临时停用" \
  > /sdcard/VirtualCam/README.txt 2>/dev/null

set_perm_recursive "$MODPATH" 0 0 0755 0644
ui_print "- 完成。重启后在 KernelSU 模块页打开 WebUI 配置。"
