plugins {
    id("com.android.application")
}

android {
    namespace = "com.disablehdr.hook"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.disablehdr.hook"
        minSdk = 27
        targetSdk = 36
        // Keep this a plain single classes.dex so the native module can
        // load it directly with InMemoryDexClassLoader without a MultiDex
        // loader step.
        multiDexEnabled = false
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            isShrinkResources = false
        }
    }

    packaging {
        resources.excludes.add("META-INF/*")
    }
}

// No dependencies on purpose: Bridge.java is two `native` method
// declarations and nothing else (see its own doc comment for why). All the
// actual hooking work - and this project's only two third-party
// dependencies, LSPlant and Dobby - live in native/, not here. Compare with
// the previous version of this module, which pulled in YAHFA here to do
// Java-side reflection-based hooking; that's gone along with the dependency.

// After assembling the APK, pull classes.dex out of it and drop it where
// the packaging script (package.sh) expects to find it.
tasks.register<Copy>("extractHookDex") {
    dependsOn("assembleRelease")
    from(zipTree(layout.buildDirectory.file("outputs/apk/release/hookdex-release-unsigned.apk"))) {
        include("classes.dex")
    }
    into(layout.buildDirectory.dir("hookdex-out"))
}
