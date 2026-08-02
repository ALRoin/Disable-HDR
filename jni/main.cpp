#include <jni.h>
#include <android/log.h>
#include "zygisk.hpp"

#define LOG_TAG "HDRKiller"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;

class HDRKillerModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        // Automatically unload library from process memory after execution to save RAM
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

    void postAppSpecialize(const AppSpecializeArgs *args) override {
        if (!env) return;

        // Nullify HDR Capabilities via Framework reflection
        stripHdrCapabilities(env);
    }

private:
    Api *api;
    JNIEnv *env;

    void stripHdrCapabilities(JNIEnv *env) {
        // Find HdrCapabilities class
        jclass hdrCapsClass = env->FindClass("android/view/Display$HdrCapabilities");
        if (!hdrCapsClass) {
            env->ExceptionClear();
            return;
        }

        // Access the empty static capability constant or override returning array
        jfieldID emptyHdrField = env->GetStaticFieldID(hdrCapsClass, "INVALID_LUMINANCE", "F");
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }

        LOGI("Successfully injected SDR override into target app process.");
    }
};

REGISTER_ZYGISK_MODULE(HDRKillerModule)
