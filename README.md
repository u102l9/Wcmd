# VirtualCamKSU — 纯 Zygisk 虚拟摄像头模块 (KernelSU Next)

用 ShadowHook inline-hook `libcamera2ndk.so` 的 `ACameraDevice_createCaptureSession`，
向相机会话注入自建 `AImageReader` 假 Surface，`AMediaCodec` 解码
`/sdcard/VirtualCam/virtual.mp4` 循环写 YUV 帧。
**无 Java / 无 LSPosed / 无 Xposed 依赖。** 带 WebUI 随时开关、换源。

## 目标环境（已按真机采集结果匹配）
- 一加 Ace 6T / PLR110 / ColorOS 16 / Android 16 (SDK 37) / arm64-v8a
- KernelSU Next (ksud 3.4.2, uapi 5) + Zygisk Next
- SELinux Enforcing（sepolicy.rule 已放行）
- `/system/lib64/libcamera2ndk.so` 存在

## 目录
```
module.prop            zygisk=true 身份
service.sh             开机建 /sdcard/VirtualCam、/data/adb/VirtualCam
post-fs-data.sh        修 zygisk 库权限
customize.sh           刷入校验 arm64 + Zygisk
sepolicy.rule          放行相机进程读素材
zygisk/module.cpp      Zygisk 入口 + Hook + 解码 + 假源注入
zygisk/CMakeLists.txt  只读本地 ../third_party/shadowhook，不联网
zygisk/build.sh        本地 NDK 编译
.github/workflows/     GitHub Actions：checkout 第三方源码 + 云端编译出 zip
webroot/               WebUI：开关 / 换源 / 镜像 / 尺寸 / 重启相机
```

## 编译（GitHub，推荐）
1. 把本目录推到仓库 → Actions → Run workflow。
2. workflow 会用 `actions/checkout` 把 **bytedance/android-inline-hook**
   拉到 `third_party/shadowhook`（带 token、含 submodule），CMake 只读本地目录。
   > 不在 CMake 里做 `git clone`：CI 子构建没有凭据，会报
   > `could not read Username for 'https://github.com'`。这是日志2 的失败原因。
3. 产出 `VirtualCamKSU.zip`（内含 `zygisk/arm64-v8a.so`）。

本地编译：先把 ShadowHook 源码放到 `third_party/shadowhook`，再
`cd zygisk && ./build.sh`（需 NDK r25+）。

## 已修复的编译问题（日志4）
- `fatal error: 'android/media/NdkMediaCodec.h' file not found` →
  NDK 的 media 头**全部**在 `<media/NdkXxx.h>`（sysroot/usr/include/media/），
  `android/media/...` 是 AOSP frameworks 路径，NDK sysroot 没有。日志3 只暴露了
  `android/image_reader.h`，日志4 修完第一个后暴露了剩下两个。现已统一改为
  `<media/NdkImageReader.h>` / `<media/NdkMediaCodec.h>` /
  `<media/NdkMediaExtractor.h>`，并显式加 `<media/NdkMediaFormat.h>`
  （`AMediaFormat_*` 与 `AMEDIAFORMAT_KEY_*` 真正定义处）。
- 验证方式升级：不再用"随便摆的桩"，而是按 **NDK sysroot 真实目录布局**
  （`android/`、`media/`、`sys/`）重建头文件桩后跑 `g++ -fsyntax-only`，
  include 路径写错会当场失败。当前通过（EXIT=0）。
  ShadowHook 源码筛选在日志4 中已验证正确（C 文件全部编译通过，无报错）。

## 已修复的编译问题（日志3）
- `fatal error: android/image_reader.h not found` → NDK 中 AImageReader 头是
  `<media/NdkImageReader.h>`，`android/image_reader.h` 是 AOSP 平台头，NDK sysroot 没有。
- ShadowHook 的 `arch/arm/sh_inst.c` 报 `sh_util_get_arm_cpu_features` 未声明 →
  上一版 `GLOB_RECURSE` 从仓库根抓 `.c`，把 demo/unittest/32位源码全编进来了。
  现在只取 `shadowhook/src/main/cpp` 核心 + `arch/arm64` + `common` + `third_party`。
- 隐藏错误：`static const char* MEDIA_DIR` 与 `"/virtual.mp4"` 做字面量拼接在
  C++ 非法 → 改成宏。
- 隐藏错误：`add_library(SHARED)` 默认带 `lib` 前缀，产物会是
  `libarm64-v8a.so`，workflow 里 `cp build/arm64-v8a.so` 必然失败 → 设 `PREFIX ""`。
- 顺手修解码器：configure 的 format 不能设 `video/raw`（那是输出格式），
  改按 extractor 实际 mime 创建 decoder；`setDataSourceFd` 传真实文件长度。

本地已用 NDK 头桩对完整 `module.cpp` 做过 `g++ -fsyntax-only` 校验通过，
并模拟验证过 CMake 源码筛选不会再把 demo/32位代码编进来。

## 使用
1. KernelSU Next 刷入 zip，重启。
2. 把 `virtual.mp4`(H.264, 建议 1280x720) 放到 `/sdcard/VirtualCam/`，
   拍照图 `photo.jpg`、前摄图 `front.jpg`；WebUI 打开开关。
3. 预览不是 720p 时，在 WebUI 填真实尺寸写 `size.txt`，点「重启相机」。
4. 临时停用：关开关即可，无需重启进程（hook 始终安装，开关在运行时判断）。

## 必须知道的功能边界（重要）
- 这套 hook 落在 **NDK Camera2 客户端库** `libcamera2ndk.so`，且 Zygisk
  注入所有 App 进程。**只有真正加载 libcamera2ndk 的 App（原生/NDK 相机类）才会被替换**。
- 微信、抖音、系统相机等走 **Java Camera2 API**，它们不加载 libcamera2ndk，
  因此**纯 Zygisk + NDK hook 覆盖不到**。要做到全 App 替换，需要按 Android 版本
  hook `libcameraservice.so` 的客户端符号或 Java 层，那是另外一套工程。
- 真机联调项：预览尺寸/stride/格式（`size.txt`）、YUV 逐平面拷贝在高 stride
  场景需按 `AImage_getPlaneRowStride` 精修。用 `adb logcat -s VirtualCam`
  看 `hook_sym_name` / `injected fake surface` 日志判断链路是否通。
- 沙盒无 NDK、无真机，交付的是"可编译、可刷、WebUI 可用、链路自洽"的工程，
  不是已在 Ace 6T 验证出画面的成品。

## 合规
仅用于 App 测试 / 隐私保护 / 直播推流。禁止用于人脸绕过或身份欺诈。
微信 / 支付宝有风控，Root 环境务必用小号测试。
