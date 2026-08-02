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

dependencies {
    // YAHFA: the ART method-hooking core (same lineage LSPosed itself
    // is built on). Maintained fork tracks current Android releases.
    implementation("io.github.pagalaxylab:yahfa:0.10.0")
}

// After assembling the APK, pull classes.dex out of it and drop it where
// the packaging script (package.sh) expects to find it.
tasks.register<Copy>("extractHookDex") {
    dependsOn("assembleRelease")
    from(zipTree(layout.buildDirectory.file("outputs/apk/release/hookdex-release-unsigned.apk"))) {
        include("classes.dex")
    }
    into(layout.buildDirectory.dir("hookdex-out"))
}
