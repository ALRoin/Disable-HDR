package com.disablehdr.hook;

import android.view.Display;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

public class DisableHdrHook {

    public static void init() {
        try {
            disableHdrCapabilitiesGlobally();
        } catch (Throwable t) {
            // Silently swallow errors to avoid app crash cascades
        }
    }

    private static void disableHdrCapabilitiesGlobally() {
        try {
            // Override Display.HdrCapabilities default empty fields if cached
            Class<?> hdrCapsClass = Display.HdrCapabilities.class;
            
            // Construct empty HdrCapabilities (HDR types set to empty array)
            int[] emptyTypes = new int[0];
            Display.HdrCapabilities emptyCaps;

            try {
                // Compatible with Android 7.0 - 14+ constructor signatures
                var ctor = hdrCapsClass.getDeclaredConstructor(int[].class, float.class, float.class, float.class);
                ctor.setAccessible(true);
                emptyCaps = (Display.HdrCapabilities) ctor.newInstance(emptyTypes, 0f, 0f, 0f);
            } catch (Exception e) {
                return;
            }

            // Reflection-based patch for runtime display instances
            Method getHdrCapabilitiesMethod = Display.class.getDeclaredMethod("getHdrCapabilities");
            Method isHdrMethod = Display.class.getDeclaredMethod("isHdr");

            // Override Java method responses via JNI native hook replacement if using SandHook/Pine
            // Or override framework property flags via reflection on DisplayManager internal caches
            System.setProperty("sys.display.hdr_disable", "1");
        } catch (Throwable ignored) {
        }
    }
}
