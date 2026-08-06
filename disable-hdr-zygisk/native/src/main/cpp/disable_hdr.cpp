/*
 * Disable HDR - Zygisk module
 *
 * Hooks three framework methods so every app process sees "no HDR support",
 * regardless of what the panel/HWC underneath actually reports, whether the
 * app asks the Display or asks the codec:
 *
 *   - android.view.Display$HdrCapabilities#getSupportedHdrTypes()  -> int[0]
 *   - android.view.Display#isHdr()                                -> false
 *   - android.media.MediaCodecInfo#getCapabilitiesForType(String)  -> real
 *     CodecCapabilities, minus any HDR-only profile (HDR10, HDR10Plus,
 *     every Dolby Vision profile, VP9/AV1/APV HDR variants, ...) from its
 *     profileLevels. Apps that skip Display entirely and probe decoder
 *     profiles directly (ExoPlayer-based players in particular do this) see
 *     the same codec, at the same non-HDR profiles it would report anyway -
 *     nothing is removed that isn't HDR-specific, so normal SDR playback
 *     (including plain 10-bit SDR, which is a different thing from HDR and
 *     is deliberately left alone) is unaffected.
 *
 *     The HDR-vs-not distinction here is derived at runtime by reflecting
 *     over MediaCodecInfo$CodecProfileLevel's own declared constants and
 *     pattern-matching their *names* (contains "HDR", or starts with
 *     "DolbyVision"), rather than a hardcoded table of integer values.
 *     Android keeps adding new profile constants (APV's HDR10/HDR10Plus
 *     variants are new as of API 36) and a fixed table would silently miss
 *     whatever gets added next; reading the real constants on the actual
 *     device, in the actual hooked process, self-adapts to that instead.
 *
 * Hooking is done with LSPlant (github.com/LSPosed/LSPlant), the same ART
 * hook engine LSPosed itself is built on, using Dobby (github.com/jmpews/Dobby)
 * as the underlying inline-hook/symbol-resolver backend LSPlant needs.
 * LSPlant is built from source and linked statically (see CMakeLists.txt for
 * why: the Maven/prefab package turned out to be a shared library, which a
 * Zygisk module can never resolve at load time); Dobby is linked statically
 * from its prebuilt Maven/prefab package, which does publish a genuine
 * static archive. Nothing here requires LSPosed (the app/framework) to be
 * installed. This module never touches the LSPosed manager, never registers
 * as an Xposed module, and does not care whether LSPosed is present on the
 * device at all: it is a self-contained Zygisk .so, in the same spirit as
 * HMA-OSS's Zygisk-only branch, just implemented directly in C++ instead of
 * via a Java/Kotlin entrypoint library.
 *
 * classes.dex (see hookdex/) is read directly off disk in preAppSpecialize(),
 * while the process is still running as root - privilege drop to the app's
 * own UID happens during specialization, not before, and /data/adb is not
 * readable once that drop happens. This avoids needing Zygisk's
 * companion-process mechanism entirely.
 *
 * That dex contains exactly one class (com.disablehdr.hook.Bridge) with
 * three native-method stubs and no other logic - LSPlant's Hook() requires the
 * hook target and callback to be real java.lang.reflect.Method objects
 * backed by *some* loaded class, and this is the only reason Bridge exists.
 * Its methods are implemented here in C++ and bound with RegisterNatives.
 * Because Bridge has no dependency on any app-defined class, and neither do
 * the methods we're hooking (plain android.view.* / android.media.* framework
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
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

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

// ---- MediaCodecInfo#getCapabilitiesForType(String) support ----
//
// Unlike the two hooks above, this one needs the *real* return value (to
// filter it) rather than a fixed replacement, so it needs the backup method
// LSPlant::Hook() returns. Stashed as a global ref, set once per process the
// first (and only) time we install this hook - see HookGetCapabilitiesForType.
jobject g_getCapsBackup = nullptr;

bool IsHdrConstantName(const std::string &name) {
    return name.find("HDR") != std::string::npos || name.rfind("DolbyVision", 0) == 0;
}

// Lazily reflects over MediaCodecInfo$CodecProfileLevel's own declared
// `public static final int` fields and returns the set of values whose
// field name marks them as HDR-only (see IsHdrConstantName). Deliberately
// NOT a hardcoded table: Android keeps adding profile constants (APV's HDR
// variants are new in API 36) and reading the real ones on the actual
// device self-adapts to whatever this specific build defines, including
// anything added after this module was compiled. Computed once per process
// and cached, since this is JNI reflection, not a hot path.
const std::unordered_set<int32_t> &HdrProfileValues(JNIEnv *env) {
    static std::unordered_set<int32_t> values;
    static std::once_flag computedOnce;
    // getCapabilitiesForType() can be called from several app threads
    // concurrently once the app is up (e.g. a media/decoder thread probing
    // capabilities while something else does too) - unlike hook
    // installation itself, which only ever runs once, synchronously,
    // during specialization. call_once makes first-use population race-free
    // instead of risking concurrent writers on the same unordered_set.
    std::call_once(computedOnce, [env]() {
        jclass cplClass = env->FindClass("android/media/MediaCodecInfo$CodecProfileLevel");
        if (ClearPendingException(env, "FindClass CodecProfileLevel") || !cplClass) return;

        jclass classCls = env->FindClass("java/lang/Class");
        jmethodID getFields = env->GetMethodID(classCls, "getFields", "()[Ljava/lang/reflect/Field;");
        auto fields = static_cast<jobjectArray>(env->CallObjectMethod(cplClass, getFields));
        if (ClearPendingException(env, "CodecProfileLevel.getFields") || !fields) return;

        jclass fieldCls = env->FindClass("java/lang/reflect/Field");
        jmethodID getName = env->GetMethodID(fieldCls, "getName", "()Ljava/lang/String;");
        jmethodID getModifiers = env->GetMethodID(fieldCls, "getModifiers", "()I");
        jmethodID getInt = env->GetMethodID(fieldCls, "getInt", "(Ljava/lang/Object;)I");
        jclass modifierCls = env->FindClass("java/lang/reflect/Modifier");
        jmethodID isStatic = env->GetStaticMethodID(modifierCls, "isStatic", "(I)Z");

        jsize count = env->GetArrayLength(fields);
        for (jsize i = 0; i < count; i++) {
            jobject field = env->GetObjectArrayElement(fields, i);
            jint mods = env->CallIntMethod(field, getModifiers);
            if (env->CallStaticBooleanMethod(modifierCls, isStatic, mods)) {
                auto jname = static_cast<jstring>(env->CallObjectMethod(field, getName));
                const char *nameChars = env->GetStringUTFChars(jname, nullptr);
                std::string name(nameChars ? nameChars : "");
                if (nameChars) env->ReleaseStringUTFChars(jname, nameChars);

                if (IsHdrConstantName(name)) {
                    jint value = env->CallIntMethod(field, getInt, nullptr);
                    if (!ClearPendingException(env, "Field.getInt")) values.insert(value);
                }
                env->DeleteLocalRef(jname);
            }
            env->DeleteLocalRef(field);
        }
        env->DeleteLocalRef(fields);
        LOGD("discovered %zu HDR-only CodecProfileLevel constant(s)", values.size());
    });
    return values;
}

// Rebuilds `caps.profileLevels`, dropping every entry whose `.profile`
// value is HDR-only per HdrProfileValues(). Leaves colorFormats and
// everything else on the CodecCapabilities object untouched, and leaves
// non-HDR entries (including plain 10-bit SDR profiles, which are a
// different thing from HDR and must keep working) exactly as reported.
void StripHdrFromCapabilities(JNIEnv *env, jobject caps) {
    if (!caps) return;
    const auto &hdrValues = HdrProfileValues(env);
    if (hdrValues.empty()) return;

    jclass capsClass = env->FindClass("android/media/MediaCodecInfo$CodecCapabilities");
    if (ClearPendingException(env, "FindClass CodecCapabilities") || !capsClass) return;
    jfieldID profileLevelsField = env->GetFieldID(
        capsClass, "profileLevels", "[Landroid/media/MediaCodecInfo$CodecProfileLevel;");
    if (ClearPendingException(env, "GetFieldID profileLevels") || !profileLevelsField) return;

    auto levels = static_cast<jobjectArray>(env->GetObjectField(caps, profileLevelsField));
    if (!levels) return;
    jsize count = env->GetArrayLength(levels);

    jclass cplClass = env->FindClass("android/media/MediaCodecInfo$CodecProfileLevel");
    jfieldID profileField = env->GetFieldID(cplClass, "profile", "I");
    if (ClearPendingException(env, "GetFieldID profile") || !profileField) {
        env->DeleteLocalRef(levels);
        return;
    }

    std::vector<jobject> kept;
    kept.reserve(static_cast<size_t>(count));
    bool droppedAny = false;
    for (jsize i = 0; i < count; i++) {
        jobject level = env->GetObjectArrayElement(levels, i);
        jint profile = env->GetIntField(level, profileField);
        if (hdrValues.count(profile) != 0) {
            droppedAny = true;
            env->DeleteLocalRef(level);
        } else {
            kept.push_back(level);
        }
    }

    if (droppedAny) {
        auto filtered = env->NewObjectArray(static_cast<jsize>(kept.size()), cplClass, nullptr);
        for (size_t i = 0; i < kept.size(); i++) {
            env->SetObjectArrayElement(filtered, static_cast<jsize>(i), kept[i]);
        }
        env->SetObjectField(caps, profileLevelsField, filtered);
        env->DeleteLocalRef(filtered);
    }
    for (jobject l : kept) env->DeleteLocalRef(l);
    env->DeleteLocalRef(levels);
}

// Callback for Bridge#hookGetCapabilitiesForType(Object[] args). args[0] is
// the MediaCodecInfo receiver, args[1] is the mime-type String parameter -
// same "receiver first, then real params" layout as the two no-arg hooks
// above, just with an actual parameter this time. Calls through to the real
// method via the backup (java.lang.reflect.Method.invoke), then filters
// what it returns.
jobject StripHdrCapabilities(JNIEnv *env, jobject /*thiz*/, jobjectArray args) {
    if (!g_getCapsBackup) return nullptr;

    jobject receiver = env->GetObjectArrayElement(args, 0);
    jobject mimeArg = env->GetObjectArrayElement(args, 1);

    jclass methodCls = env->GetObjectClass(g_getCapsBackup);
    jmethodID invokeId = env->GetMethodID(
        methodCls, "invoke", "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
    jclass objectCls = env->FindClass("java/lang/Object");
    jobjectArray invokeArgs = env->NewObjectArray(1, objectCls, mimeArg);

    jobject caps = env->CallObjectMethod(g_getCapsBackup, invokeId, receiver, invokeArgs);
    ClearPendingException(env, "backup.invoke(getCapabilitiesForType)");

    env->DeleteLocalRef(invokeArgs);
    env->DeleteLocalRef(receiver);
    env->DeleteLocalRef(mimeArg);

    if (caps) StripHdrFromCapabilities(env, caps);
    return caps;
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

// Same shape as HookNoArgMethod, but for MediaCodecInfo#getCapabilitiesForType
// specifically: it takes a real parameter and StripHdrCapabilities needs to
// call the original implementation (unlike the two fixed-return hooks
// above), so this keeps the backup method LSPlant::Hook() returns.
bool HookGetCapabilitiesForType(JNIEnv *env, jclass bridgeClass, jobject bridgeInstance) {
    JNINativeMethod native[] = {
        {"hookGetCapabilitiesForType", "([Ljava/lang/Object;)Ljava/lang/Object;",
         reinterpret_cast<void *>(&StripHdrCapabilities)},
    };
    if (env->RegisterNatives(bridgeClass, native, 1) != 0) {
        ClearPendingException(env, "RegisterNatives");
        LOGE("RegisterNatives failed for hookGetCapabilitiesForType");
        return false;
    }

    jclass infoClass = env->FindClass("android/media/MediaCodecInfo");
    if (ClearPendingException(env, "FindClass MediaCodecInfo") || !infoClass) {
        LOGE("android.media.MediaCodecInfo not found - nothing to hook");
        return false;
    }

    jmethodID targetMethodId = env->GetMethodID(
        infoClass, "getCapabilitiesForType",
        "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;");
    if (ClearPendingException(env, "getCapabilitiesForType") || !targetMethodId) {
        LOGE("MediaCodecInfo#getCapabilitiesForType not found on this build");
        return false;
    }

    jmethodID bridgeMethodId = env->GetMethodID(bridgeClass, "hookGetCapabilitiesForType",
                                                 "([Ljava/lang/Object;)Ljava/lang/Object;");
    if (ClearPendingException(env, "hookGetCapabilitiesForType") || !bridgeMethodId) {
        LOGE("bridge method hookGetCapabilitiesForType not found");
        return false;
    }

    jobject targetMethod = env->ToReflectedMethod(infoClass, targetMethodId, JNI_FALSE);
    jobject callbackMethod = env->ToReflectedMethod(bridgeClass, bridgeMethodId, JNI_FALSE);
    if (ClearPendingException(env, "ToReflectedMethod") || !targetMethod || !callbackMethod) {
        LOGE("ToReflectedMethod failed for getCapabilitiesForType");
        return false;
    }

    jobject backup = lsplant::Hook(env, targetMethod, bridgeInstance, callbackMethod);
    if (!backup) {
        LOGE("lsplant::Hook failed for getCapabilitiesForType");
        return false;
    }
    g_getCapsBackup = env->NewGlobalRef(backup);

    LOGD("hooked MediaCodecInfo#getCapabilitiesForType");
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

        // Covers apps that probe decoder profiles directly instead of (or
        // in addition to) asking Display - see the file header comment.
        HookGetCapabilitiesForType(env, bridgeClass, bridgeInstance);
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
