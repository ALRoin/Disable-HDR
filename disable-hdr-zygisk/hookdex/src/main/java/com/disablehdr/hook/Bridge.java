package com.disablehdr.hook;

/**
 * Minimal native-method stub injected into every hooked app process.
 *
 * This class carries no logic of its own on purpose. LSPlant's Hook() API
 * requires the hook target and the callback to both be real
 * java.lang.reflect.Method objects backed by a class that's actually
 * loaded - that's the only reason this class exists. Both methods below are
 * implemented natively (see native/src/main/cpp/disable_hdr.cpp) and bound
 * with JNI's RegisterNatives() right before each is installed as an LSPlant
 * hook, so all the real logic - and all of this project's third-party
 * dependencies (LSPlant, Dobby) - live in native/, not here.
 *
 * Loaded via dalvik.system.InMemoryDexClassLoader with the *system*
 * ClassLoader as parent, not the target app's own ClassLoader. That's safe
 * because this class, and the android.view.Display* / android.media.MediaCodecInfo
 * methods the native side hooks, are never app-defined - they're either this
 * stub or plain AOSP framework classes, both resolvable without the app's
 * real ClassLoader, which may not even exist yet this early in process
 * specialization.
 *
 * Signature note: LSPlant requires every hook callback to look like
 * {@code Object method(Object[] args)}. args[0] is always the receiver of
 * the original call (a Display, Display.HdrCapabilities, or MediaCodecInfo
 * instance, depending on the hook); the first two hooks below don't need to
 * inspect it since their replacement answer is fixed either way, but the
 * third does - see hookGetCapabilitiesForType's native implementation.
 */
public final class Bridge {
    /** Backs Display.HdrCapabilities#getSupportedHdrTypes() -> empty int[]. */
    public native Object hookHdrTypes(Object[] args);

    /** Backs Display#isHdr() -> Boolean.FALSE. */
    public native Object hookIsHdr(Object[] args);

    /**
     * Backs MediaCodecInfo#getCapabilitiesForType(String) -> the real
     * CodecCapabilities, with HDR-only profiles stripped out of
     * profileLevels. args[0] is the MediaCodecInfo receiver, args[1] is the
     * mime-type String parameter.
     */
    public native Object hookGetCapabilitiesForType(Object[] args);
}
