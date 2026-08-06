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
                // Forces c++_static for the whole CMake project - including
                // LSPlant, which CMakeLists.txt now builds from source (see
                // that file for why: the Maven/prefab "lsplant-standalone"
                // package turned out to publish a *shared* liblsplant.so,
                // which a Zygisk module can never resolve at load time).
                // c++_static keeps the resulting disable_hdr.so free of a
                // libc++_shared.so dependency too, for the same reason.
                arguments += listOf("-DANDROID_STL=c++_static")
                abiFilters += listOf("armeabi-v7a", "arm64-v8a", "x86", "x86_64")
            }
        }
    }

    // Still needed for Dobby's Prefab (native AAR) package below, so CMake's
    // find_package(dobby) can see its headers + prebuilt static lib. LSPlant
    // no longer goes through Prefab - see CMakeLists.txt.
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
    // LSPlant itself is intentionally NOT listed here anymore. It used to
    // be ("org.lsposed.lsplant:lsplant-standalone:+"), consumed as a
    // Gradle/Prefab dependency and linked via find_package(lsplant) in
    // CMakeLists.txt - which silently produced a disable_hdr.so with a
    // DT_NEEDED dependency on a liblsplant.so that never ships anywhere in
    // a Zygisk module, so the module never loaded, in any process, ever.
    // CMakeLists.txt now fetches LSPlant's source directly and compiles it
    // in statically; see the comment block at the top of that file.

    // Dobby: the inline-hook + ELF-symbol-resolver backend LSPlant needs
    // under the hood. Prefab-packaged build maintained by vvb2060
    // (github.com/vvb2060/dobby-android), upstream at github.com/jmpews/Dobby.
    // Unlike lsplant-standalone, this package does publish a genuine static
    // archive - CMakeLists.txt's self-containment check will say so loudly
    // if that ever stops being true.
    implementation("io.github.vvb2060.ndk:dobby:+")
}
