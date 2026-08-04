/*
 * Disable HDR - Zygisk module
 *
 * Hooks two framework methods so every app process sees "no HDR support",
 * regardless of what the panel/HWC underneath actually reports:
 *
 *   - android.view.Display$HdrCapabilities#getSupportedHdrTypes()  -> int[0]
 *   - android.view.Display#isHdr()                                -> false
 *
 * Hooking is done with LSPlant (github.com/LSPosed/LSPlant), the same ART
 * hook engine LSPosed itself is built on, using Dobby (github.com/jmpews/Dobby)
 * as the underlying inline-hook/symbol-resolver backend LSPlant needs. Both
 * are linked in statically from prebuilt Maven/prefab packages - nothing here
 * requires LSPosed (the app/framework) to be installed. This module never
 * touches the LSPosed manager, never registers as an Xposed module, and does
 * not care whether LSPosed is present on the device at all: it is a
 * self-contained Zygisk .so, in the same spirit as HMA-OSS's Zygisk-only
 * branch, just implemented directly in C++ instead of via a Java/Kotlin
 * entrypoint library.
 *
 * classes.dex (see hookdex/) is read directly off disk in preAppSpecialize(),
 * while the process is still running as root - privilege drop to the app's
 * own UID happens during specialization, not before, and /data/adb is not
 * readable once that drop happens. This avoids needing Zygisk's
 * companion-process mechanism entirely.
 *
 * That dex contains exactly one class (com.disablehdr.hook.Bridge) with two
 * native-method stubs and no other logic - LSPlant's Hook() requires the
 * hook target and callback to be real java.lang.reflect.Method objects
 * backed by *some* loaded class, and this is the only reason Bridge exists.
 * Its methods are implemented here in C++ and bound with RegisterNatives.
 * Because Bridge has no dependency on any app-defined class, and neither do
 * the two methods we're hooking (both are plain android.view.* framework
 * classes, always resolvable from the boot classpath), we load it with the
 * system ClassLoader as parent right here in postAppSpecialize. We
 * deliberately do NOT try to fetch the target app's own ClassLoader via
 * ActivityThread - at this point in process specialization (right after
 * fork, before ActivityThread.main() has even run) it is not reliably
 * populated yet, and we don't need it for this narrow use case anyway.
 */

#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <android/log.h>

#include "zygisk.hpp"
#include "lsplant.hpp"
#include "dobby.h"

#define LOG_TAG "DisableHdrZygisk"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

namespace {

// Target package list + dex both live under the module's own data dir so
// they can be inspected/edited (via the WebUI) without recompiling anything.
constexpr const char *kTargetsPath = "/data/adb/modules/disable_hdr/targets.txt";
constexpr const char *kDexPath = "/data/adb/modules/disable_hdr/classes.dex";
constexpr const char *kBridgeBinaryName = "com/disablehdr/hook/Bridge";
constexpr const char *kBridgeDotName = "com.disablehdr.hook.Bridge";

// One package name per line, '#' for comments. Missing file, or a file with
// no real entries, means "hook everything" (global mode) - matches the
// WebUI's "leave everything unchecked" convention.
bool ShouldHook(const char *pkg) {
    FILE *f = fopen(kTargetsPath, "r");
    if (!f) return true;

    char line[256];
    bool hasAnyEntry = false;
    bool matched = false;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' ')) {
            line[--len] = '\0';
        }
        if (len == 0 || line[0] == '#') continue;
        hasAnyEntry = true;
        if (strcmp(pkg, line) == 0) {
            matched = true;
            break;
        }
    }
    fclose(f);
    return !hasAnyEntry || matched;
}

// Reads a file fully into a malloc'd buffer. Caller owns the result and
// must free() it. Returns nullptr on any failure.
void *ReadFileFully(const char *path, int32_t *outSize) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        LOGE("could not open %s", path);
        return nullptr;
    }

    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return nullptr;
    }

    auto size = static_cast<int32_t>(st.st_size);
    void *buf = malloc(static_cast<size_t>(size));
    if (!buf) {
        close(fd);
        return nullptr;
    }

    ssize_t total = 0;
    while (total < size) {
        ssize_t n = read(fd, static_cast<char *>(buf) + total, static_cast<size_t>(size - total));
        if (n <= 0) break;
        total += n;
    }
    close(fd);

    if (total != size) {
        LOGE("short read of %s (%zd/%d)", path, total, size);
        free(buf);
        return nullptr;
    }

    *outSize = size;
    return buf;
}

// Logs + clears a pending JNI exception if there is one. Returns true if an
// exception was pending. We check this after every JNI call that could
// plausibly throw (e.g. a method that doesn't exist on some OEM build), so
// one missing method never leaves the JNIEnv in a broken state for the rest
// of the sequence, and never crashes the host app - matches the "fail
// silently, leave HDR untouched" philosophy from the original module.
bool ClearPendingException(JNIEnv *env, const char *what) {
    if (env->ExceptionCheck()) {
        LOGE("%s threw", what);
        env->ExceptionDescribe();
        env->ExceptionClear();
        return true;
    }
    return false;
}

// ---- Dobby-backed inline hook glue, wired into LSPlant's InitInfo ----

void *InlineHooker(void *target, void *hooker) {
    void *backup = nullptr;
    if (DobbyHook(target, hooker, &backup) == 0) return backup;
    return nullptr;
}

bool InlineUnhooker(void *target) {
    return DobbyDestroy(target) == 0;
}

bool EnsureLSPlantInit(JNIEnv *env) {
    static bool attempted = false;
    static bool ok = false;
    if (attempted) return ok;
    attempted = true;

    lsplant::InitInfo info{
        .inline_hooker = InlineHooker,
        .inline_unhooker = InlineUnhooker,
        .art_symbol_resolver =
            [](std::string_view symbol) -> void * {
                return DobbySymbolResolver("libart.so", std::string(symbol).c_str());
            },
        .generated_class_name = "com.disablehdr.hook.LSPHooker",
        .generated_field_name = "backup",
        .generated_method_name = "invoke",
    };

    ok = lsplant::Init(env, info);
    if (!ok) LOGE("lsplant::Init failed");
    return ok;
}

// ---- Native bodies for Bridge's two hook callbacks ----
// LSPlant invokes these exactly as if the app had called
// bridgeInstance.hookXxx(Object[] args) itself: args[0] is the receiver of
// the *original* call (the Display / HdrCapabilities instance), which we
// don't need since the answer is fixed either way.

jobject EmptyHdrTypes(JNIEnv *env, jobject /*thiz*/, jobjectArray /*args*/) {
    return env->NewIntArray(0);
}

jobject FalseBoxed(JNIEnv *env, jobject /*thiz*/, jobjectArray /*args*/) {
    jclass booleanCls = env->FindClass("java/lang/Boolean");
    jmethodID valueOf = env->GetStaticMethodID(booleanCls, "valueOf", "(Z)Ljava/lang/Boolean;");
    jobject result = env->CallStaticObjectMethod(booleanCls, valueOf, JNI_FALSE);
    env->DeleteLocalRef(booleanCls);
    return result;
}

// Hooks one zero-argument instance method so it always returns whatever
// `nativeImpl` computes. `bridgeMethodName` must name one of Bridge's two
// native Object hookXxx(Object[]) methods - that's the callback LSPlant
// will actually invoke; nativeImpl is what makes that callback do something.
bool HookNoArgMethod(JNIEnv *env, jclass targetClass, const char *targetMethodName,
                     const char *targetMethodSig, jclass bridgeClass, jobject bridgeInstance,
                     const char *bridgeMethodName, void *nativeImpl) {
    JNINativeMethod native[] = {
        {bridgeMethodName, "([Ljava/lang/Object;)Ljava/lang/Object;", nativeImpl},
    };
    if (env->RegisterNatives(bridgeClass, native, 1) != 0) {
        ClearPendingException(env, "RegisterNatives");
        LOGE("RegisterNatives failed for %s", bridgeMethodName);
        return false;
    }

    jmethodID targetMethodId = env->GetMethodID(targetClass, targetMethodName, targetMethodSig);
    if (ClearPendingException(env, targetMethodName) || !targetMethodId) {
        LOGE("target method %s%s not found on this build", targetMethodName, targetMethodSig);
        return false;
    }

    jmethodID bridgeMethodId = env->GetMethodID(bridgeClass, bridgeMethodName,
                                                 "([Ljava/lang/Object;)Ljava/lang/Object;");
    if (ClearPendingException(env, bridgeMethodName) || !bridgeMethodId) {
        LOGE("bridge method %s not found", bridgeMethodName);
        return false;
    }

    jobject targetMethod = env->ToReflectedMethod(targetClass, targetMethodId, JNI_FALSE);
    jobject callbackMethod = env->ToReflectedMethod(bridgeClass, bridgeMethodId, JNI_FALSE);
    if (ClearPendingException(env, "ToReflectedMethod") || !targetMethod || !callbackMethod) {
        LOGE("ToReflectedMethod failed for %s", targetMethodName);
        return false;
    }

    jobject backup = lsplant::Hook(env, targetMethod, bridgeInstance, callbackMethod);
    if (!backup) {
        LOGE("lsplant::Hook failed for %s", targetMethodName);
        return false;
    }

    LOGD("hooked %s%s", targetMethodName, targetMethodSig);
    return true;
}

}  // namespace

class DisableHdrModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (!args || !args->nice_name) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        const char *process = env->GetStringUTFChars(args->nice_name, nullptr);
        hookThisProcess = process != nullptr && ShouldHook(process);
        if (process) env->ReleaseStringUTFChars(args->nice_name, process);

        if (!hookThisProcess) {
            // Not a target: let Zygisk unload us from this process right away.
            // Safe here because we have not hooked anything yet.
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        // Still root here (privilege drop happens during specialization,
        // after this callback returns) - read the dex now while we can.
        dexBuf = ReadFileFully(kDexPath, &dexSize);
        if (!dexBuf) {
            LOGE("failed to read hook dex, skipping this process");
            hookThisProcess = false;
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }

    void postAppSpecialize(const AppSpecializeArgs *args) override {
        if (!hookThisProcess || !dexBuf) return;

        installHooks();

        free(dexBuf);
        dexBuf = nullptr;

        // IMPORTANT: we do NOT call setOption(DLCLOSE_MODULE_LIBRARY) here.
        // Our hook callbacks (EmptyHdrTypes / FalseBoxed) live inside this
        // .so; dlclose-ing it after installing a hook would unmap the very
        // code ART jumps to on the next call and crash the app. Once we've
        // hooked anything, this module must stay resident for the life of
        // the process - see the warning on DLCLOSE_MODULE_LIBRARY in
        // zygisk.hpp.
    }

    void preServerSpecialize(ServerSpecializeArgs *args) override {
        // Nothing to do inside system_server for this module.
        api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
    bool hookThisProcess = false;
    void *dexBuf = nullptr;
    int32_t dexSize = 0;

    void installHooks() {
        if (!EnsureLSPlantInit(env)) return;

        jobject dexLoader = loadBridgeDex();
        if (!dexLoader) return;

        jclass bridgeClass = resolveBridgeClass(dexLoader);
        if (!bridgeClass) return;

        jmethodID bridgeCtor = env->GetMethodID(bridgeClass, "<init>", "()V");
        if (ClearPendingException(env, "Bridge ctor lookup") || !bridgeCtor) return;
        jobject bridgeInstance = env->NewObject(bridgeClass, bridgeCtor);
        if (ClearPendingException(env, "new Bridge()") || !bridgeInstance) return;

        // Both target classes are plain AOSP framework classes - always
        // resolvable regardless of which ClassLoader "loaded" Bridge.
        jclass hdrCapsClass = env->FindClass("android/view/Display$HdrCapabilities");
        if (!ClearPendingException(env, "FindClass HdrCapabilities") && hdrCapsClass) {
            HookNoArgMethod(env, hdrCapsClass, "getSupportedHdrTypes", "()[I", bridgeClass,
                            bridgeInstance, "hookHdrTypes", reinterpret_cast<void *>(&EmptyHdrTypes));
        } else {
            LOGE("android.view.Display$HdrCapabilities not found - nothing to hook");
        }

        jclass displayClass = env->FindClass("android/view/Display");
        if (!ClearPendingException(env, "FindClass Display") && displayClass) {
            // isHdr() has existed since API 26; still guard it, some OEM
            // builds have been known to trim or rename framework methods.
            HookNoArgMethod(env, displayClass, "isHdr", "()Z", bridgeClass, bridgeInstance,
                            "hookIsHdr", reinterpret_cast<void *>(&FalseBoxed));
        }
    }

    // Loads Bridge via InMemoryDexClassLoader with the *system* ClassLoader
    // as parent - deliberately not the app's own ClassLoader. See the file
    // header comment for why that's both correct and safer here.
    jobject loadBridgeDex() {
        jclass byteBufferCls = env->FindClass("java/nio/ByteBuffer");
        jmethodID allocateDirect =
            env->GetStaticMethodID(byteBufferCls, "allocateDirect", "(I)Ljava/nio/ByteBuffer;");
        jobject byteBuffer = env->CallStaticObjectMethod(byteBufferCls, allocateDirect, dexSize);
        void *direct = env->GetDirectBufferAddress(byteBuffer);
        if (!direct) {
            LOGE("GetDirectBufferAddress failed");
            return nullptr;
        }
        memcpy(direct, dexBuf, static_cast<size_t>(dexSize));

        jclass classLoaderCls = env->FindClass("java/lang/ClassLoader");
        jmethodID getSystemClassLoader =
            env->GetStaticMethodID(classLoaderCls, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
        jobject systemClassLoader = env->CallStaticObjectMethod(classLoaderCls, getSystemClassLoader);
        if (ClearPendingException(env, "getSystemClassLoader") || !systemClassLoader) return nullptr;

        jclass dexLoaderCls = env->FindClass("dalvik/system/InMemoryDexClassLoader");
        jmethodID dexLoaderCtor = env->GetMethodID(dexLoaderCls, "<init>",
                                                    "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
        jobject dexLoader = env->NewObject(dexLoaderCls, dexLoaderCtor, byteBuffer, systemClassLoader);
        if (ClearPendingException(env, "new InMemoryDexClassLoader") || !dexLoader) return nullptr;
        return dexLoader;
    }

    jclass resolveBridgeClass(jobject dexLoader) {
        jmethodID loadClass = env->GetMethodID(env->GetObjectClass(dexLoader), "loadClass",
                                                "(Ljava/lang/String;)Ljava/lang/Class;");
        jstring className = env->NewStringUTF(kBridgeDotName);
        auto cls = static_cast<jclass>(env->CallObjectMethod(dexLoader, loadClass, className));
        if (ClearPendingException(env, "loadClass(Bridge)") || !cls) {
            LOGE("failed to load %s from injected dex", kBridgeBinaryName);
            return nullptr;
        }
        return cls;
    }
};

REGISTER_ZYGISK_MODULE(DisableHdrModule)
