// Zygisk API v4 精简声明（与 Zygisk Next / ZNNext 兼容）
// 摘自官方 zygisk.hpp (topjohnwu/zygisk-module-sample)，仅保留本模块需要的部分。
#pragma once
#include <jni.h>
#include <string_view>
#include <unistd.h>

namespace zygisk {

struct Api;
struct ModuleBase;

struct AppSpecializeArgs {
    jint &uid; jint &gid; jintArray &gids; jint &runtime_flags;
    jobjectArray &rlimits; jint &mount_external; jstring &se_info;
    jstring &nice_name; jstring &instruction_set; jstring &app_data_dir;
    jboolean &is_child_zygote; jint &mount_mode; jstring &app_zw_data_dir;
    jboolean &is_top_app; jstring &pkg_data_info_list; jstring &whitelisted_data_info_list;
    jboolean &mount_data_dirs; jboolean &mount_storage_dirs;
    AppSpecializeArgs() = delete;
};

struct ServerSpecializeArgs {
    jint &uid; jint &gid; jintArray &gids; jint &runtime_flags;
    jlong &permitted_capabilities; jlong &effective_capabilities;
    jlong &bounding_capabilities;
    ServerSpecializeArgs() = delete;
};

enum class State : uint32_t { UNKNOWN = 0, UMOUNTED = 1, MOUNTED = 2 };
enum class Option : uint32_t {
    NO_CONDITIONAL_WATCHDOG = (1u << 0),
    DLCLOSE_MODULE_LIBRARY   = (1u << 1),
};

struct ModuleBase {
    virtual void onLoad(Api *api, JNIEnv *env) {}
    virtual void preAppSpecialize(AppSpecializeArgs *args) {}
    virtual void postAppSpecialize(const AppSpecializeArgs *args) {}
    virtual void preServerSpecialize(ServerSpecializeArgs *args) {}
    virtual void postServerSpecialize(const ServerSpecializeArgs *args) {}
    virtual ~ModuleBase() = default;
};

struct Api {
    virtual bool registerModule(ModuleBase *mod) = 0;
    virtual void setOption(Option opt) = 0;
    virtual State moduleState() = 0;
    virtual int  getFlags() = 0;
    virtual void hookJniNativeMethods(JNIEnv *env, const char *cls,
                                      JNINativeMethod *methods, int num) = 0;
    virtual void pltHookRegister(const char *cond, const char *sym, void *fn, void **orig) = 0;
    virtual void pltHookExclude(const char *a, const char *b) = 0;
    virtual bool pltHookCommit() = 0;
};

} // namespace zygisk

extern "C" {
__attribute__((visibility("default"))) void zygisk_init(zygisk::Api *api, JNIEnv *env);
}

#define REGISTER_ZYGISK_MODULE(T)                                  \
    extern "C" [[gnu::visibility("default")]] void zygisk_init(   \
            zygisk::Api *api, JNIEnv *env) {                       \
        static T instance;                                         \
        instance.onLoad(api, env);                                 \
        api->registerModule(&instance);                            \
    }
