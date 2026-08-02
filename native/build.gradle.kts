plugins {
    id("com.android.library")
}

android {
    namespace = "com.hdrblock.native_"
    compileSdk = 36

    defaultConfig {
        minSdk = 27 // Zygisk API requires Magisk/KernelSU with Zygisk, API 27+
    }

    externalNativeBuild {
        ndkBuild {
            path("jni/Android.mk")
        }
    }
}
