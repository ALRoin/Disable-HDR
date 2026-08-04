buildscript {
    repositories {
        google()
        mavenCentral()
    }
    dependencies {
        // Deliberately pinned to the AGP 8.x line rather than 9.x: AGP 9.0
        // (Jan 2026) removed several old DSL classes this project's build
        // files use, and that migration happened after my reliable
        // knowledge cutoff. AGP 8.7 still builds compileSdk 36 (Android 16)
        // fine. If you want to move to AGP 9.x, Android Studio's Upgrade
        // Assistant (Tools > AGP Upgrade Assistant) is the safer path than
        // hand-editing these files against a DSL I can't fully verify.
        classpath("com.android.tools.build:gradle:8.7.0")
    }
}

tasks.register<Delete>("clean") {
    delete(rootProject.layout.buildDirectory)
}
