package com.disablehdr.hook;

import android.util.Log;
import android.view.Display;

import java.lang.reflect.Constructor;
import java.lang.reflect.Method;

import lab.galaxy.yahfa.HookMain;

/**
 * Entry point injected into every hooked app process by the native Zygisk module.
 *
 * install() is invoked from C++ (disable_hdr.cpp) right after the app's own
 * ClassLoader exists, but before the app's Application.onCreate() runs.
 *
 * We hook android.view.Display so any HDR capability query the app makes
 * returns "no HDR support", regardless of what the underlying HWC/panel
 * actually reports.
 */
public final class DisableHdrHook {

    private static final String TAG = "DisableHdrHook";

    // Empty capabilities object handed back to every caller instead of the real one.
    // supportedHdrTypes = empty int[] -> app sees zero supported HDR formats.
    private static Display.HdrCapabilities emptyCapabilities;

    public static void install(ClassLoader appClassLoader) {
        try {
            buildEmptyCapabilities();
            hookGetHdrCapabilities();
            hookIsHdr();

            Log.i(TAG, "HDR hooks installed for this process");
        } catch (Throwable t) {
            // Never crash the host app just because our hook failed on this
            // Android build / OEM ROM. Fail silently and leave HDR untouched.
            Log.e(TAG, "install() failed, HDR left untouched", t);
        }
    }

    private static void buildEmptyCapabilities() throws Exception {
        // Display.HdrCapabilities has no public constructor, so we build it
        // via reflection with an empty supportedHdrTypes array and harmless
        // luminance placeholders.
        Constructor<Display.HdrCapabilities> ctor = Display.HdrCapabilities.class
                .getDeclaredConstructor(int[].class, float.class, float.class, float.class);
        ctor.setAccessible(true);
        emptyCapabilities = ctor.newInstance(
                new int[0],   // supportedHdrTypes -> none
                0f,           // maxLuminance
                0f,           // maxAverageLuminance
                0f            // minLuminance
        );
    }

    private static void hookGetHdrCapabilities() throws Exception {
        Method target = Display.class.getDeclaredMethod("getHdrCapabilities");
        Method hook = DisableHdrHook.class.getDeclaredMethod("getHdrCapabilities_hook", Display.class);
        Method backup = DisableHdrHook.class.getDeclaredMethod("getHdrCapabilities_backup", Display.class);
        HookMain.backupAndHook(target, hook, backup);
    }

    private static void hookIsHdr() throws Exception {
        // Some OEM frameworks (and some app-side checks) call Display#isHdr()
        // directly instead of inspecting HdrCapabilities. Force it false too.
        Method target;
        try {
            target = Display.class.getDeclaredMethod("isHdr");
        } catch (NoSuchMethodException e) {
            // Not present on this API level / OEM build -- nothing to hook.
            return;
        }
        Method hook = DisableHdrHook.class.getDeclaredMethod("isHdr_hook", Display.class);
        HookMain.backupAndHook(target, hook, null);
    }

    // ---- Hook bodies. Must be public/static, first param = the "this" of the target. ----

    @SuppressWarnings("unused")
    public static Display.HdrCapabilities getHdrCapabilities_hook(Display thiz) {
        return emptyCapabilities;
    }

    @SuppressWarnings("unused")
    public static Display.HdrCapabilities getHdrCapabilities_backup(Display thiz) {
        // Placeholder body; YAHFA repoints this to the original implementation
        // after hooking so we never actually execute this code.
        return emptyCapabilities;
    }

    @SuppressWarnings("unused")
    public static boolean isHdr_hook(Display thiz) {
        return false;
    }
}
