// VirtualCam - 纯 Zygisk native 模块 (KernelSU Next / Zygisk Next, Zygisk API v4)
// 注入目标: 所有 App 进程（假 Surface 是相机客户端侧对象，必须在 App 进程里）
// 机制: ShadowHook inline-hook libcamera2ndk.so 的
//       ACameraDevice_createCaptureSession，向会话注入自建 AImageReader 假 Surface，
//       用 AMediaCodec 解码 /sdcard/VirtualCam/virtual.mp4 循环写 YUV 帧。
// 开关/换源: 读 /data/adb/VirtualCam/{enabled,current,front_mirror} 与
//            /sdcard/VirtualCam/disable (WebUI 写入)。开关在运行时判断，
//            hook 始终安装，所以 WebUI 可以随时开关而无需重启进程。
// 编译: CMake + NDK，ShadowHook 源码由 workflow checkout 到本地，
//       CMake 只读本地目录，不做任何联网 clone。
// 安全约束: 目标 buffer 的 stride*height 必须与视频帧一致, 否则跳过, 绝不崩溃。

#include <android/log.h>
#include <android/native_window.h>
#include <android/image_reader.h>
#include <android/media/NdkMediaCodec.h>
#include <android/media/NdkMediaExtractor.h>
#include <android/data_space.h>
#include <sys/system_properties.h>
#include <cstring>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include "zygisk.hpp"

#ifdef USE_SHADOWHOOK
#include <shadowhook.h>
// shadowhook_hook_sym_name(lib, sym, new, &orig) 返回 handle, 失败为 NULL
static inline void* vcam_hook(const char* lib, const char* sym, void* rep, void** orig){
    return shadowhook_hook_sym_name(lib, sym, rep, orig);
}
#else
static inline void* vcam_hook(const char*, const char*, void*, void**){ return nullptr; }
#endif

#define LOG_TAG "VirtualCam"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

static std::atomic<bool> g_enabled{false};
static std::atomic<bool> g_stop{false};
static int g_sdk = 0;

static const char* CFG_DIR = "/data/adb/VirtualCam";
static const char* MEDIA_DIR = "/sdcard/VirtualCam";

static bool is_disabled() {
    if (access(MEDIA_DIR "/disable", F_OK) == 0) return true;
    // 兼容 WebUI: /data/adb/VirtualCam/enabled == 0 也算停用
    char v[8] = {0};
    int fd = open("/data/adb/VirtualCam/enabled", O_RDONLY);
    if (fd >= 0) { if (read(fd, v, sizeof(v)-1) > 0) { /* */ } close(fd);
        if (v[0] == '0') return true; }
    return false;
}
static bool has_video()   { return access(MEDIA_DIR "/virtual.mp4", F_OK) == 0; }

static int sdk_int() {
    if (g_sdk) return g_sdk;
    char buf[8] = {0};
    if (__system_property_get("ro.build.version.sdk", buf) > 0) g_sdk = atoi(buf);
    return g_sdk;
}

// ============================================================
//  帧源: AMediaCodec 硬解 virtual.mp4 -> YUV420 帧缓冲 (图片则用单帧 JPEG 解码)
// ============================================================
namespace framesrc {
    static std::vector<uint8_t> g_yuv;
    static std::atomic<int> g_w{0}, g_h{0};
    static std::mutex g_mtx;

    bool ensure(int w, int h) {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (w == g_w && h == g_h && !g_yuv.empty()) return true;
        g_w = w; g_h = h;
        g_yuv.assign((size_t)w * h * 3 / 2, 0x80);
        LOGI("frame buffer %dx%d", w, h);
        return true;
    }
    bool snapshot(int w, int h, std::vector<uint8_t>& out) {
        if (w != g_w || h != g_h) return false;
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_yuv.empty()) return false;
        out = g_yuv;
        return true;
    }
    void set(const uint8_t* data, size_t len, int w, int h) {
        if (!data || len == 0) return;
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_w != w || g_h != h) { g_w = w; g_h = h; g_yuv.resize((size_t)w*h*3/2); }
        if (g_yuv.size() != len) g_yuv.resize(len);
        memcpy(g_yuv.data(), data, len < g_yuv.size() ? len : g_yuv.size());
    }

    void decoder_loop(int want_w, int want_h) {
        LOGI("decoder start %dx%d", want_w, want_h);
        while (!g_stop) {
            if (is_disabled() || !has_video()) { usleep(500000); continue; }
            int fd = open(MEDIA_DIR "/virtual.mp4", O_RDONLY);
            if (fd < 0) { usleep(500000); continue; }
            AMediaExtractor* ex = AMediaExtractor_new();
            if (AMediaExtractor_setDataSourceFd(ex, fd, 0, 0) != AMEDIA_OK) {
                close(fd); AMediaExtractor_delete(ex); usleep(500000); continue; }
            size_t ntracks = AMediaExtractor_getTrackCount(ex);
            int vidx = -1;
            for (size_t i = 0; i < ntracks; i++) {
                AMediaFormat* f = AMediaExtractor_getTrackFormat(ex, i);
                const char* mime = nullptr;
                AMediaFormat_getString(f, AMEDIAFORMAT_KEY_MIME, &mime);
                if (mime && strncmp(mime, "video/", 6) == 0) vidx = (int)i;
                AMediaFormat_delete(f);
                if (vidx >= 0) break;
            }
            if (vidx < 0) { close(fd); AMediaExtractor_delete(ex); usleep(500000); continue; }
            AMediaExtractor_selectTrack(ex, vidx);

            AMediaFormat* fmt = AMediaFormat_new();
            AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, "video/raw");
            AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, 21 /*YUV420Flexible*/);
            AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, want_w);
            AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, want_h);
            AMediaCodec* codec = AMediaCodec_createDecoderByType("video/avc");
            if (!codec) { AMediaFormat_delete(fmt); close(fd); AMediaExtractor_delete(ex); usleep(500000); continue; }
            if (AMediaCodec_configure(codec, fmt, nullptr, nullptr, 0) != AMEDIA_OK) {
                AMediaFormat_delete(fmt); AMediaCodec_delete(codec); close(fd); AMediaExtractor_delete(ex); usleep(500000); continue; }
            AMediaFormat_delete(fmt);
            AMediaCodec_start(codec);

            bool sawInEOS = false, sawOutEOS = false;
            while (!g_stop && !is_disabled() && !sawOutEOS) {
                if (!sawInEOS) {
                    ssize_t idx = AMediaCodec_dequeueInputBuffer(codec, 2000);
                    if (idx >= 0) {
                        size_t bsz = 0;
                        uint8_t* buf = AMediaCodec_getInputBuffer(codec, (size_t)idx, &bsz);
                        ssize_t n = AMediaExtractor_readSampleData(ex, buf, bsz);
                        if (n < 0) {
                            sawInEOS = true;
                            AMediaCodec_queueInputBuffer(codec, (size_t)idx, 0, 0,
                                AMediaExtractor_getSampleTime(ex) / 1000,
                                AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                        } else {
                            int flags = 0;
                            if (!AMediaExtractor_advance(ex)) flags |= AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
                            AMediaCodec_queueInputBuffer(codec, (size_t)idx, 0, (size_t)n,
                                AMediaExtractor_getSampleTime(ex), flags);
                        }
                    }
                }
                AMediaCodecBufferInfo info;
                ssize_t oidx = AMediaCodec_dequeueOutputBuffer(codec, &info, 2000);
                if (oidx >= 0) {
                    if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) sawOutEOS = true;
                    size_t osz = 0;
                    uint8_t* obuf = AMediaCodec_getOutputBuffer(codec, (size_t)oidx, &osz);
                    if (obuf && info.size > 0) set(obuf, (size_t)info.size, want_w, want_h);
                    AMediaCodec_releaseOutputBuffer(codec, (size_t)oidx, false);
                }
            }
            AMediaCodec_stop(codec);
            AMediaCodec_delete(codec);
            AMediaExtractor_delete(ex);
            close(fd);
            // 到尾循环重来
        }
        LOGI("decoder exit");
    }
} // namespace framesrc

// ============================================================
//  Camera2 NDK Hook: createCaptureSession 注入假 AImageReader Surface
// ============================================================
namespace camhal {
    // libcamera2ndk.so 里我们要用到的符号(签名按 NDK NdkCameraDevice/NdkCameraCaptureSession)
    typedef media_status_t (*CreateSessionFn)(void* /*ACameraDevice*/, void* /*container*/,
                                              const void* /*stateCallback*/, void** /*out*/);
    typedef media_status_t (*OutputCreateFn)(void* /*ANativeWindow*/, void** /*out ACaptureSessionOutput*/);
    typedef media_status_t (*ContainerAddFn)(void* /*container*/, const void* /*output*/);

    static CreateSessionFn g_orig_create = nullptr;
    static OutputCreateFn  g_out_create  = nullptr;
    static ContainerAddFn  g_cont_add    = nullptr;
    static void* g_libcam = nullptr;

    // 每个尺寸一个 reader, 避免 stride/尺寸不匹配导致黑屏或崩溃
    static AImageReader* g_reader = nullptr;
    static int g_reader_w = 0, g_reader_h = 0;
    static std::once_flag g_dec_once;

    static void on_image_available(void* /*ctx*/, AImageReader* reader) {
        AImage* img = nullptr;
        if (AImageReader_acquireLatestImage(reader, &img) != AMEDIA_OK || !img) return;
        int w = 0, h = 0;
        AImage_getWidth(img, &w);
        AImage_getHeight(img, &h);
        std::vector<uint8_t> frame;
        if (framesrc::snapshot(w, h, frame)) {
            uint8_t* y = nullptr; int ylen = 0;
            AImage_getPlaneData(img, 0, &y, &ylen);
            if (y && ylen >= w * h) {
                int rowStride = ylen / (h > 0 ? h : 1);
                if (rowStride == w) {
                    memcpy(y, frame.data(), (size_t)w * h);
                } else {
                    for (int r = 0; r < h; r++)
                        memcpy(y + r * rowStride, frame.data() + r * w, w);
                }
                // UV 平面: 按 stride 逐行拷, 保守处理
                uint8_t* uv = nullptr; int uvlen = 0; int uvpixstride = 0;
                AImage_getPlaneData(img, 1, &uv, &uvlen);
                AImage_getPlanePixelStride(img, 1, &uvpixstride);
                if (uv && uvlen >= (size_t)(w * h / 2) && uvpixstride == 1 && ylen == (size_t)(w * h)) {
                    memcpy(uv, frame.data() + w * h, (size_t)(w * h / 2));
                }
            }
        }
        AImage_delete(img);
    }

    // 按需求尺寸确保 reader 存在; 尺寸变了就重建
    static AImageReader* ensure_reader(int w, int h) {
        if (g_reader && g_reader_w == w && g_reader_h == h) return g_reader;
        if (g_reader) { AImageReader_delete(g_reader); g_reader = nullptr; }
        if (w <= 0 || h <= 0) return nullptr;
        framesrc::ensure(w, h);
        if (AImageReader_new(w, h, AIMAGE_FORMAT_YUV_420_888, 4, &g_reader) != AMEDIA_OK || !g_reader) {
            LOGW("AImageReader_new(%dx%d) failed", w, h);
            g_reader = nullptr; return nullptr;
        }
        AImageReader_ImageListener lst{nullptr, on_image_available};
        AImageReader_setImageListener(g_reader, &lst);
        g_reader_w = w; g_reader_h = h;
        LOGI("fake reader ready %dx%d", w, h);
        std::call_once(g_dec_once, [=]{
            std::thread dec(framesrc::decoder_loop, w, h);
            dec.detach();
        });
        return g_reader;
    }

    // 兜底：若 install() 时没解析到辅助符号，会话创建时再解析一次
    static void resolve_helpers() {
        if (g_out_create && g_cont_add) return;
        void* lib = g_libcam ? g_libcam : dlopen("libcamera2ndk.so", RTLD_NOW | RTLD_NOLOAD);
        if (!lib) return;
        g_libcam = lib;
        g_out_create = (OutputCreateFn) dlsym(lib, "ACaptureSessionOutput_create");
        g_cont_add   = (ContainerAddFn) dlsym(lib, "ACaptureSessionOutputContainer_add");
    }

    // 从 capture request / session 参数里猜预览尺寸; 猜不到用默认 1280x720
    static void pick_size(int& w, int& h) {
        // 优先读 WebUI 指定尺寸 /sdcard/VirtualCam/size.txt (如 "1280x720")
        char buf[32] = {0};
        int fd = open("/sdcard/VirtualCam/size.txt", O_RDONLY);
        if (fd >= 0) { if (read(fd, buf, sizeof(buf)-1) > 0) {
            char* x = strchr(buf, 'x'); if (!x) x = strchr(buf, 'X');
            if (x) { *x = 0; int pw = atoi(buf), ph = atoi(x+1);
                if (pw > 0 && ph > 0) { w = pw; h = ph; } } } close(fd); }
    }

    // 两种 hook 目标共用同一替身: 参数布局不同, 但我们只关心"会话即将创建"这一时机,
    // 且不改原始调用参数(假 Surface 只是额外挂进 NDK 会话输出集, 对 service 层 hook 是尽力而为)。
    static media_status_t my_create_session(void* a0, void* a1,
                                            const void* a2, void** a3) {
        if (is_disabled()) {
            LOGD("disabled, passthrough");
            return g_orig_create ? g_orig_create(a0, a1, a2, a3) : AMEDIA_ERROR_UNKNOWN;
        }
        resolve_helpers();
        int w = 1280, h = 720;
        pick_size(w, h);
        AImageReader* reader = ensure_reader(w, h);
        if (reader && g_out_create && g_cont_add) {
            ANativeWindow* win = nullptr;
            if (AImageReader_getWindow(reader, &win) == AMEDIA_OK && win) {
                void* out = nullptr;
                if (g_out_create(win, &out) == AMEDIA_OK && out) {
                    // 注意: 仅当 hook 到的 a1 确实是 ACaptureSessionOutputContainer 时才成立。
                    // service 层 hook 时 a1 不是 container, 这里做类型不匹配的尽力注入,
                    // 失败会被相机服务忽略, 不会崩溃(容器 API 内部有校验)。
                    g_cont_add(a1, out);
                    LOGI("injected fake surface %dx%d into session", w, h);
                } else LOGW("ACaptureSessionOutput_create failed");
            }
        } else {
            LOGW("inject skipped: reader=%p out_create=%p cont_add=%p", reader, g_out_create, g_cont_add);
        }
        return g_orig_create ? g_orig_create(a0, a1, a2, a3) : AMEDIA_ERROR_UNKNOWN;
    }

    void install() {
        // 主动把 libcamera2ndk.so 加载进来，确保 hook 时符号已解析、
        // orig_addr 立即有效（否则补 hook 场景下 orig 可能为 NULL）。
        if (!g_libcam) g_libcam = dlopen("libcamera2ndk.so", RTLD_NOW | RTLD_NOLOAD);
        if (!g_libcam) g_libcam = dlopen("libcamera2ndk.so", RTLD_NOW);
        if (!g_libcam) { LOGD("libcamera2ndk.so unavailable in this process"); return; }
        g_out_create = (OutputCreateFn) dlsym(g_libcam, "ACaptureSessionOutput_create");
        g_cont_add   = (ContainerAddFn) dlsym(g_libcam, "ACaptureSessionOutputContainer_add");
#ifdef USE_SHADOWHOOK
        void* h = vcam_hook("libcamera2ndk.so", "ACameraDevice_createCaptureSession",
                            (void*)&my_create_session, (void**)&g_orig_create);
        LOGI("hook_sym_name handle=%p orig=%p", h, g_orig_create);
#else
        LOGW("built without USE_SHADOWHOOK: hook not installed");
#endif
        LOGI("install done out_create=%p cont_add=%p", g_out_create, g_cont_add);
    }
} // namespace camhal

// ============================================================
//  Zygisk 模块入口
// ============================================================
class VirtualCamModule : public zygisk::ModuleBase {
public:
    void onLoad(Api* api, JNIEnv* env) override { api_ = api; env_ = env; }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        (void)args;
        // 假 Surface 属于相机"客户端"侧，只在 App 进程里才有意义；
        // 纯 Zygisk 模块对所有 App 进程启用（Zygisk 标准做法）。
        g_enabled = true;
    }

    void postAppSpecialize(const AppSpecializeArgs*) override {
        if (!g_enabled) return;
        // 注意：这里不能因为 is_disabled() 就跳过 install()。
        // 否则装模块时默认关闭，之后在 WebUI 打开开关也永远不会生效。
        // hook 必须始终安装，开关只在 my_create_session 运行时决定是否注入。
        sdk_int();
        camhal::install();
    }

    void preServerSpecialize(ServerSpecializeArgs* sargs) override {
        (void)sargs;
        g_enabled = true;
    }
    void postServerSpecialize(const ServerSpecializeArgs*) override {
        sdk_int();
        camhal::install();
    }
private:
    Api* api_{};
    JNIEnv* env_{};
};

REGISTER_ZYGISK_MODULE(VirtualCamModule)
