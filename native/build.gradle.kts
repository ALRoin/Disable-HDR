plugins {
    id("com.android.library")
}

android {
    namespace = "com.disablehdr.native_"
    compileSdk = 36

    defaultConfig {
        // Zygisk API requires Magisk, or KernelSU + a Zygisk provider
        // (ZygiskNext / ReZygisk) - either way, API 27+.
        minSdk = 27

        externalNativeBuild {
            cmake {
                // lsplant-standalone statically bundles libc++, so our own
                // code links against the static runtime too - this keeps
                // the module a single self-contained .so with nothing that
                // needs to resolve libc++_shared.so at dlopen time inside
                // an arbitrary app process.
                arguments += listOf("-DANDROID_STL=c++_static")
                abiFilters += listOf("armeabi-v7a", "arm64-v8a", "x86", "x86_64")
            }
        }
    }

    // Required for Gradle to wire up LSPlant's / Dobby's Prefab (native AAR)
    // packages so CMake's find_package() can see their headers + prebuilt libs.
    buildFeatures {
        prefab = true
    }

    externalNativeBuild {
        cmake {
            path("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}

dependencies {
    // LSPlant: the ART hook engine LSPosed itself is built on (actively
    // maintained by the LSPosed org - see github.com/LSPosed/LSPlant).
    // "-standalone" bundles libc++ statically; pairs with ANDROID_STL=c++_static above.
    implementation("org.lsposed.lsplant:lsplant-standalone:+")

    // Dobby: the inline-hook + ELF-symbol-resolver backend LSPlant needs
    // under the hood. Prefab-packaged build maintained by vvb2060
    // (github.com/vvb2060/dobby-android), upstream at github.com/jmpews/Dobby.
    implementation("io.github.vvb2060.ndk:dobby:+")
}
