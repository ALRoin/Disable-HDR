cmake_minimum_required(VERSION 3.22.1)
project(disable_hdr)
include(FetchContent)

# -----------------------------------------------------------------------
# LSPlant: vendored from source and compiled straight into this .so.
#
# We used to consume org.lsposed.lsplant:lsplant-standalone as a Gradle /
# Prefab dependency (find_package(lsplant REQUIRED CONFIG) + link against
# lsplant::lsplant) on the assumption that "standalone" meant lsplant was
# linked statically into whatever consumed it. It doesn't: per LSPlant's
# own README, "-standalone" only means libc++ is baked into liblsplant.so
# so you don't *also* need to ship libc++_shared.so next to it - the
# artifact itself is still a prebuilt SHARED library, because Prefab is
# fundamentally a mechanism for distributing precompiled .so files.
#
# That's a non-issue for a normal app (its APK ships liblsplant.so next to
# its own .so and the OS's linker resolves it at launch, the way any
# jniLibs dependency does). It is fatal for a Zygisk module: a module is
# one self-contained .so per ABI with no jniLibs-style secondary lookup.
# ZygiskNext's own loader confirmed this on-device:
#
#   Not found: 'liblsplant.so' needed by 'disable_hdr' while loading 'disable_hdr'
#   cannot locate symbol "_ZN7lsplant2v24InitEP7_JNIEnvRKNS0_8InitInfoE"
#   Failed to link image
#   preload failed: 1 disable_hdr
#
# i.e. the module never preloads, in any process, ever - which is exactly
# why no DisableHdrZygisk log line ever appeared anywhere in the bug
# report: onLoad() was never reached, not even once.
#
# Building LSPlant from source and linking it as a genuine static library
# (our own add_library(... STATIC ...) call below, NOT LSPlant's own
# add_library(), whose SHARED/STATIC choice we don't control) removes the
# dependency entirely: the object code ends up inside libdisable_hdr.so
# itself, so nothing external needs to be found at load time. ANDROID_STL
# is already forced to c++_static in native/build.gradle.kts, so this
# applies uniformly to LSPlant's sources too - no libc++_shared.so either.
#
# GIT_TAG is pinned to "master" so this fix applies cleanly regardless of
# exactly which release is newest right now. Once CI is green, pin this to
# a real release tag instead (see https://github.com/LSPosed/LSPlant/tags)
# for reproducible builds.
# -----------------------------------------------------------------------
# GIT_SUBMODULES "" - LSPlant's repo has git submodules (test fixtures
# under test/src/main/jni/external/, referenced over SSH as
# git@github.com:LSPosed/<name>.git). GitHub Actions runners have no SSH
# key configured for that, so a normal recursive submodule checkout fails
# outright ("Could not read from remote repository"). We only compile
# lsplant/ (the library itself, filtered below to exclude test/ anyway),
# so skip submodules entirely rather than trying to authenticate for
# fixtures we don't need.
FetchContent_Declare(
    lsplant_src
    GIT_REPOSITORY https://github.com/LSPosed/LSPlant.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""
)
FetchContent_Populate(lsplant_src)

file(GLOB_RECURSE LSPLANT_SOURCES CONFIGURE_DEPENDS
    "${lsplant_src_SOURCE_DIR}/lsplant/*.cc"
    "${lsplant_src_SOURCE_DIR}/lsplant/*.cpp"
)
list(FILTER LSPLANT_SOURCES EXCLUDE REGEX "/test/")
if(NOT LSPLANT_SOURCES)
    message(FATAL_ERROR
        "No LSPlant .cc/.cpp sources found under "
        "${lsplant_src_SOURCE_DIR}/lsplant - LSPlant likely restructured "
        "its repo since this was written. Check "
        "https://github.com/LSPosed/LSPlant/tree/master/lsplant for the "
        "current layout and update the glob + include dirs below.")
endif()

set(LSPLANT_INCLUDE_DIRS "")
foreach(candidate
    # Verified against LSPlant's actual current repo layout (the Doxygen
    # docs it publishes mirror the real source tree, and both the search
    # index and the generated page for lsplant.hpp itself show this exact
    # breadcrumb): lsplant.cc lives directly in .../jni/, but the public
    # header is one level deeper, in .../jni/include/. That's what the
    # previous candidate list got wrong - it had "jni" but not "jni/include",
    # so the .cc file's own directory was on the include path but the
    # header never was. https://lsposed.org/LSPlant/lsplant_8hpp.html
    "${lsplant_src_SOURCE_DIR}/lsplant/src/main/jni/include"
    "${lsplant_src_SOURCE_DIR}/lsplant/src/main/jni"
    "${lsplant_src_SOURCE_DIR}/lsplant/src/main/cpp/include"
    "${lsplant_src_SOURCE_DIR}/lsplant/src/main/cpp"
)
    if(EXISTS "${candidate}")
        list(APPEND LSPLANT_INCLUDE_DIRS "${candidate}")
    endif()
endforeach()
if(NOT LSPLANT_INCLUDE_DIRS)
    message(FATAL_ERROR
        "Could not find LSPlant's public headers (looked for lsplant.hpp "
        "under a few conventional paths) - see the message above about "
        "checking LSPlant's current repo layout.")
endif()

add_library(lsplant_static STATIC ${LSPLANT_SOURCES})
target_include_directories(lsplant_static PUBLIC ${LSPLANT_INCLUDE_DIRS})
target_compile_features(lsplant_static PRIVATE cxx_std_20)
set_target_properties(lsplant_static PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
target_link_libraries(lsplant_static PRIVATE log)

# -----------------------------------------------------------------------
# Dobby stays a Gradle/Prefab dependency: io.github.vvb2060.ndk:dobby
# publishes it as a genuine static archive (unlike lsplant above), so
# find_package + target_link_libraries is the right tool here. The
# self-containment check at the bottom of this file will fail the build
# loudly, naming the exact library, if a future version of that package
# ever changes that - instead of silently shipping a module that never
# loads, the way this one did.
# -----------------------------------------------------------------------
find_package(dobby REQUIRED CONFIG)

add_library(disable_hdr SHARED disable_hdr.cpp)

target_compile_features(disable_hdr PRIVATE cxx_std_20)
set_target_properties(disable_hdr PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_compile_options(disable_hdr PRIVATE
    -Wall -Wextra
    -fvisibility=hidden -fvisibility-inlines-hidden
    -ffunction-sections -fdata-sections
)

target_link_options(disable_hdr PRIVATE
    -Wl,--gc-sections
    -Wl,--exclude-libs,ALL
)

target_link_libraries(disable_hdr
    PRIVATE
    lsplant_static
    dobby::dobby
    log
)

# -----------------------------------------------------------------------
# Guardrail: a Zygisk module must be fully self-contained. If this target
# ever regains a DT_NEEDED entry on anything beyond the handful of
# libraries guaranteed to already be mapped into every Android process
# (libc/libm/libdl/liblog/the system libc++, etc.), it will silently stop
# loading again - the exact bug this whole file exists to fix. Catch that
# here, at build time, with the offending library named, instead of
# rediscovering it via another bug report.
# -----------------------------------------------------------------------
find_program(READELF_EXECUTABLE NAMES llvm-readelf readelf)
if(READELF_EXECUTABLE)
    add_custom_command(TARGET disable_hdr POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DREADELF=${READELF_EXECUTABLE}"
            "-DTARGET_SO=$<TARGET_FILE:disable_hdr>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_self_contained.cmake"
        COMMENT "Verifying disable_hdr.so has no non-system DT_NEEDED entries"
        VERBATIM
    )
else()
    message(WARNING
        "readelf/llvm-readelf not found - skipping the self-containment "
        "check for disable_hdr.so. This is a best-effort safety net, not "
        "the fix itself, so the build still proceeds - but nothing will "
        "catch a regression on this machine.")
endif()
