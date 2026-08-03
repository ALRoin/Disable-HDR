/*
 * Disable HDR - Zygisk module
 *
 * Injects DisableHdrHook.install() (see DisableHdrHook.java) into every target app
 * process via a raw classes.dex loaded through InMemoryDexClassLoader.
 * No LSPosed/Xposed manager required - this is a self-contained Zygisk
 * module, same approach used by HMA-OSS's Zygisk-only branch.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <android/log.h>

#include "zygisk.hpp"

#define LOG_TAG "DisableHdrZygisk"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// ---------------------------------------------------------------------------
// Target package list now lives in a plain text config file instead of being
// hardcoded here, so it can be edited from the module's WebUI (webroot/)
// without recompiling. One package name per line, '#' for comments.
// Empty/missing file = hook every app process (global HDR block).
// ---------------------------------------------------------------------------
static const char *kTargetsPath = "/data/adb/modules/disable_hdr/targets.txt";

static bool shouldHook(const char *pkg) {
    FILE *f = fopen(kTargetsPath, "r");
    if (!f) {
        // No config yet -> default to global mode (matches original behavior).
        return true;
    }

    char line[256];
    bool hasAnyEntry = false;
    bool matched = false;
    while (fgets(line, sizeof(line), f)) {
        // Trim trailing newline/whitespace.
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ')) {
            line[--len] = '\0';
        }
        if (len == 0 || line[0] == '#') continue; // blank line or comment
        hasAnyEntry = true;
        if (strcmp(pkg, line) == 0) {
            matched = true;
            break;
        }
    }
    fclose(f);

    // If the file exists but has no real entries, treat it the same as
    // "no config" -> global mode. Otherwise only hook listed packages.
    return !hasAnyEntry || matched;
}

class DisableHdrModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        const char *process = env->GetStringUTFChars(args->nice_name, nullptr);
        hookThisProcess = shouldHook(process);
        env->ReleaseStringUTFChars(args->nice_name, process);

        if (!hookThisProcess) {
            // Not a target: let Zygisk unload us from this process immediately.
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }

    void postAppSpecialize(const AppSpecializeArgs *args) override {
        if (!hookThisProcess) return;
        injectAndInstallHook();
    }

    void preServerSpecialize(ServerSpecializeArgs *args) override {
        // We don't need to run inside system_server for this use case.
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
    bool hookThisProcess = false;

    // Reads our companion-served classes.dex (built from DisableHdrHook.java + YAHFA)
    // and loads it via dalvik.system.InMemoryDexClassLoader, then calls
    // com.disablehdr.hook.DisableHdrHook.install(ClassLoader).
    void injectAndInstallHook() {
        int fd = api->connectCompanion();
        if (fd < 0) {
            LOGE("failed to connect to companion");
            return;
        }

        int32_t dexSize = 0;
        read(fd, &dexSize, sizeof(dexSize));
        if (dexSize <= 0) {
            LOGE("companion returned invalid dex size: %d", dexSize);
            close(fd);
            return;
        }

        void *dexBuf = malloc(dexSize);
        if (!dexBuf) {
            close(fd);
            return;
        }

        ssize_t total = 0;
        while (total < dexSize) {
            ssize_t n = read(fd, (char *) dexBuf + total, dexSize - total);
            if (n <= 0) break;
            total += n;
        }
        close(fd);

        if (total != dexSize) {
            LOGE("short read of hook dex (%zd/%d)", total, dexSize);
            free(dexBuf);
            return;
        }

        jobject appClassLoader = getAppClassLoader();
        if (appClassLoader == nullptr) {
            LOGE("could not resolve app ClassLoader");
            free(dexBuf);
            return;
        }

        // ByteBuffer.allocateDirect(dexSize) then copy our dex bytes in.
        jclass byteBufferCls = env->FindClass("java/nio/ByteBuffer");
        jmethodID allocateDirect = env->GetStaticMethodID(
                byteBufferCls, "allocateDirect", "(I)Ljava/nio/ByteBuffer;");
        jobject byteBuffer = env->CallStaticObjectMethod(byteBufferCls, allocateDirect, dexSize);

        void *direct = env->GetDirectBufferAddress(byteBuffer);
        memcpy(direct, dexBuf, dexSize);
        free(dexBuf);

        // new dalvik.system.InMemoryDexClassLoader(byteBuffer, parentClassLoader)
        jclass dexLoaderCls = env->FindClass("dalvik/system/InMemoryDexClassLoader");
        jmethodID dexLoaderCtor = env->GetMethodID(
                dexLoaderCls, "<init>",
                "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
        jobject dexLoader = env->NewObject(dexLoaderCls, dexLoaderCtor, byteBuffer, appClassLoader);

        // dexLoader.loadClass("com.disablehdr.hook.DisableHdrHook")
        jmethodID loadClass = env->GetMethodID(
                env->GetObjectClass(dexLoader), "loadClass",
                "(Ljava/lang/String;)Ljava/lang/Class;");
        jstring className = env->NewStringUTF("com.disablehdr.hook.DisableHdrHook");
        jclass hookCls = (jclass) env->CallObjectMethod(dexLoader, loadClass, className);

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            LOGE("failed to load DisableHdrHook class from injected dex");
            return;
        }

        // DisableHdrHook.install(appClassLoader)
        jmethodID installMethod = env->GetStaticMethodID(
                hookCls, "install", "(Ljava/lang/ClassLoader;)V");
        env->CallStaticVoidMethod(hookCls, installMethod, appClassLoader);

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            LOGE("DisableHdrHook.install() threw");
        } else {
            LOGD("HDR hook installed successfully");
        }
    }

    // Walks up from the current thread's context to find the app's real
    // ClassLoader (not the boot classloader), matching what LSPosed does
    // before injecting.
    jobject getAppClassLoader() {
        jclass activityThreadCls = env->FindClass("android/app/ActivityThread");
        if (activityThreadCls == nullptr) {
            env->ExceptionClear();
            return nullptr;
        }
        jmethodID currentActivityThread = env->GetStaticMethodID(
                activityThreadCls, "currentActivityThread", "()Landroid/app/ActivityThread;");
        jobject activityThread = env->CallStaticObjectMethod(activityThreadCls, currentActivityThread);
        if (activityThread == nullptr) return nullptr;

        jmethodID getApplication = env->GetMethodID(
                activityThreadCls, "getApplication", "()Landroid/app/Application;");
        jobject application = env->CallObjectMethod(activityThread, getApplication);
        if (application == nullptr || env->ExceptionCheck()) {
            env->ExceptionClear();
            return nullptr;
        }

        jclass contextCls = env->FindClass("android/content/Context");
        jmethodID getClassLoader = env->GetMethodID(
                contextCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
        return env->CallObjectMethod(application, getClassLoader);
    }
};

// ---------------------------------------------------------------------------
// Companion process: runs with root/system-server privileges outside the
// app sandbox. Its only job here is to hand the module's classes.dex
// (assets/classes.dex, packed into the module APK) to each hooked process.
// ---------------------------------------------------------------------------
#include <fcntl.h>
#include <sys/stat.h>

static const char *kDexPath = "/data/adb/modules/disable_hdr/classes.dex";

static void companion_handler(int fd) {
    int dexFd = open(kDexPath, O_RDONLY);
    if (dexFd < 0) {
        int32_t zero = 0;
        write(fd, &zero, sizeof(zero));
        LOGE("companion: could not open %s", kDexPath);
        close(fd);
        return;
    }

    struct stat st{};
    fstat(dexFd, &st);
    int32_t size = (int32_t) st.st_size;
    write(fd, &size, sizeof(size));

    char buf[65536];
    ssize_t n;
    while ((n = read(dexFd, buf, sizeof(buf))) > 0) {
        write(fd, buf, n);
    }
    close(dexFd);
    close(fd);
}

REGISTER_ZYGISK_MODULE(DisableHdrModule)
REGISTER_ZYGISK_COMPANION(companion_handler)
