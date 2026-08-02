#pragma once

#include <jni.h>

namespace zygisk {

enum Option {
    DLCLOSE_MODULE_LIBRARY = 1,
    FORCE_DENYLIST_UNMOUNT = 2,
};

class Api {
public:
    virtual void setOption(Option option) = 0;
};

class AppSpecializeArgs {
public:
    jint uid;
    jint gid;
    jintArray gids;
    jint runtime_flags;
    jobjectArray mount_external;
    jstring se_info;
    jstring nice_name;
    jstring instruction_set;
    jstring app_data_dir;
};

class ServerSpecializeArgs {};

class ModuleBase {
public:
    virtual void onLoad(Api *api, JNIEnv *env) {}
    virtual void preAppSpecialize(AppSpecializeArgs *args) {}
    virtual void postAppSpecialize(const AppSpecializeArgs *args) {}
    virtual void preServerSpecialize(ServerSpecializeArgs *args) {}
    virtual void postServerSpecialize(const ServerSpecializeArgs *args) {}
};

} // namespace zygisk

#define REGISTER_ZYGISK_MODULE(clazz) \
    extern "C" [[gnu::visibility("default")]] \
    void zygisk_module_entry(zygisk::Api *api, JNIEnv *env) { \
        static clazz module; \
        module.onLoad(api, env); \
}
