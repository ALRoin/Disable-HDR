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
# -----------------------------------------------------------------------
# 2026-08-06 update - the next bug this caused, and why the fix below
# looks the way it does:
#
# Pinning GIT_TAG to "master" (as this file did until now) traded the
# dynamic-linking bug above for a reproducibility bug: master is a moving
# target, and it moved. CI started failing with:
#
#   FAILED: native/CMakeFiles/lsplant_static.dir/.../lsplant.cc.o
#   .../_deps/lsplant_src-src/lsplant/src/main/jni/lsplant.cc:25:8:
#   fatal error: module 'lsplant' not found
#      25 | module lsplant;
#         | ~~~~~~~^~~~~~~
#
# That line is standard C++20 - it declares lsplant.cc as the
# *implementation unit* of a named module "lsplant"
# (https://en.cppreference.com/w/cpp/language/modules). Compiling a file
# like that requires a build system that scans sources for module
# dependencies up front, compiles the corresponding *interface* unit into
# a BMI first, and passes every consumer "-fmodule-file=lsplant=<path>" so
# the compiler can find it - see Clang's own docs
# (https://clang.llvm.org/docs/StandardCPlusPlusModules.html) and, for the
# identical failure shape against a hand-written example,
# https://github.com/llvm/llvm-project/issues/53661 ("Clang won't compile
# C++20 module implementation units" without being told where the
# interface's .pcm lives - same "module is not available" class of error
# we hit here). CMake did not support any of that until 3.28 (Dec 2023),
# six minor releases past what's in play here, and even 3.28 only enabled
# it for Ninja/Visual-Studio generators paired with Clang 16+, GCC 14+, or
# MSVC 14.34+ (https://cmake.org/cmake/help/latest/release/3.28.html;
# Kitware's own announcement calls it exiting *experimental* status in
# 3.28: https://www.kitware.com/import-cmake-the-experiment-is-over/).
# What actually drives the compiler in this project is AGP's external
# native build, invoking the NDK's bundled CMake 3.22.1
# (/usr/local/lib/android/sdk/cmake/3.22.1/bin/ninja, per the CI log) -
# which predates modules support entirely, experimental or not - and it
# just runs "clang++ ... -c lsplant.cc -o lsplant.cc.o" per file, with no
# scanning, no BMI, nothing. There's no flag that fixes this; the only fix
# is making sure the LSPlant commit we fetch predates whatever commit
# introduced that line.
#
# The obvious move - pin to LSPlant's last tagged release instead of
# master - has its own trap. The last version actually published to Maven
# Central is 6.4 (Apr 2024; confirmed independently via
# https://mvnrepository.com/artifact/org.lsposed.lsplant/lsplant and
# https://central.sonatype.com/artifact/org.lsposed.lsplant/lsplant/versions,
# changelog "Try support for 16k page size / Fix riscv64 support"). LSPlant
# did not support Android 16 yet as of a Nov 2025 upstream discussion
# asking exactly that (https://github.com/LSPosed/LSPlant/discussions/161,
# "when supports android 16.", unanswered) - support was added sometime
# after, and this project targets Android 16. Pinning to 6.4 would fix
# *this* build and silently reintroduce "hooks don't work on the OS
# version this module exists for", which is worse than a build failure -
# a build failure is loud, a wrong ART offset is not.
#
# github.com blocks automated fetches of its own /tags and /commits list
# views (robots.txt), and the read-only mirrors tried during this
# investigation (git.haisto.cn, rat.dev) block the same way, so there was
# no way to browse LSPlant's history directly to hand-pick "the commit
# right before the modules refactor, but on/after Android 16 support
# landed" from outside the repo. What's resolvable instead: LSPlant's
# GitHub org activity page shows continued commits through Aug 3, 2026 -
# three days before the CI run above - and a Gitea mirror's rendered log
# (also not fetchable directly, but its search-indexed content was) shows
# a "Update supported Android versions in README" commit dated 2026-04-25.
# So the safe window is real but its edges are estimated, not verified -
# see the two knobs below, and README.md's "Sources consulted" section
# for the complete trail with every source that went into this estimate
# and exactly what each one does and doesn't confirm.
#
# LSPLANT_GIT_TAG: set this if you've picked an exact tag/branch/commit
# yourself (e.g. by actually opening
# https://github.com/LSPosed/LSPlant/commits/master in a browser, which
# this investigation could not do). Empty (the default) means auto-resolve
# instead, via LSPLANT_PIN_BEFORE below - "last commit on master strictly
# before this date" is a far softer failure mode than a hardcoded tag
# guessed from outside the repo: if the date estimate is wrong, the
# module-declaration guard further down this file fails the *configure*
# step immediately with a plain-English explanation, and correcting course
# is a one-line change to LSPLANT_PIN_BEFORE - not another multi-hour
# bug-report round trip through a cryptic clang error.
# -----------------------------------------------------------------------
set(LSPLANT_GIT_TAG "" CACHE STRING "Exact LSPlant git tag/branch/commit to build. Empty (default) auto-resolves via LSPLANT_PIN_BEFORE instead - see the comment above. Set this once you've verified a specific ref yourself; \"6.4\" is a confirmed-real, confirmed-buildable fallback that predates Android 16 ART support (see README.md).")
set(LSPLANT_PIN_BEFORE "2026-07-01T00:00:00Z" CACHE STRING "Only used when LSPLANT_GIT_TAG is empty. Resolves to the newest commit on LSPlant's master branch strictly before this UTC timestamp. See README.md for how this date was chosen, and move it earlier if the module-declaration guard below still trips.")

find_package(Git REQUIRED)

set(lsplant_src_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/lsplant_src-src")

if(LSPLANT_GIT_TAG)
    # Manual pin: the same shallow-clone FetchContent flow this file
    # always used for a single fixed ref, just parameterized instead of
    # hardcoded to "master".
    message(STATUS "LSPlant: using manually pinned LSPLANT_GIT_TAG=${LSPLANT_GIT_TAG}")
    # GIT_SUBMODULES "" - LSPlant's repo has git submodules (test fixtures
    # under test/src/main/jni/external/, referenced over SSH as
    # git@github.com:LSPosed/<name>.git). GitHub Actions runners have no
    # SSH key configured for that, so a normal recursive submodule
    # checkout fails outright ("Could not read from remote repository").
    # We only compile lsplant/ (the library itself, filtered below to
    # exclude test/ anyway), so skip submodules entirely rather than
    # trying to authenticate for fixtures we don't need.
    FetchContent_Declare(
        lsplant_src
        GIT_REPOSITORY https://github.com/LSPosed/LSPlant.git
        GIT_TAG        ${LSPLANT_GIT_TAG}
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES ""
    )
    FetchContent_Populate(lsplant_src)
else()
    # Auto-resolve: needs real ancestry to walk (git rev-list against
    # history), so this is a normal clone rather than FetchContent's
    # shallow single-commit fetch. Reuses a previous checkout if one is
    # already there instead of re-cloning every configure; always fetches
    # first so a stale local clone can't pin to yesterday's idea of
    # "newest before the cutoff".
    if(NOT EXISTS "${lsplant_src_SOURCE_DIR}/.git")
        message(STATUS "LSPlant: cloning full history (auto-resolving newest commit before ${LSPLANT_PIN_BEFORE})")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" clone --no-checkout
                    https://github.com/LSPosed/LSPlant.git "${lsplant_src_SOURCE_DIR}"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMAND_ERROR_IS_FATAL ANY
        )
    else()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" fetch --quiet origin master
            WORKING_DIRECTORY "${lsplant_src_SOURCE_DIR}"
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-list -n 1 "--before=${LSPLANT_PIN_BEFORE}" origin/master
        WORKING_DIRECTORY "${lsplant_src_SOURCE_DIR}"
        OUTPUT_VARIABLE LSPLANT_RESOLVED_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE LSPLANT_REVLIST_RESULT
    )
    if(NOT LSPLANT_REVLIST_RESULT EQUAL 0 OR NOT LSPLANT_RESOLVED_SHA)
        message(FATAL_ERROR
            "LSPlant: could not resolve any commit on master before "
            "LSPLANT_PIN_BEFORE=${LSPLANT_PIN_BEFORE}. Either that date is "
            "earlier than LSPlant's first commit, or the clone at "
            "${lsplant_src_SOURCE_DIR} is broken - delete that directory "
            "and reconfigure, or move LSPLANT_PIN_BEFORE later.")
    endif()
    message(STATUS "LSPlant: resolved ${LSPLANT_RESOLVED_SHA} as the newest commit before ${LSPLANT_PIN_BEFORE}")

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -c advice.detachedHead=false checkout --force ${LSPLANT_RESOLVED_SHA}
        WORKING_DIRECTORY "${lsplant_src_SOURCE_DIR}"
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()

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

# -----------------------------------------------------------------------
# Guardrail: refuse, loudly, at configure time, if any fetched LSPlant
# source turns out to be a C++20 module declaration - see the long
# comment above LSPLANT_GIT_TAG for why nothing in this toolchain can
# compile one, and why that's a "pick a different commit" problem rather
# than a "add a compiler flag" problem. This is deliberately a simple
# heuristic (does some line look like a bare "module x;" / "export module
# x;" statement) rather than a real C++ parser - good enough to catch this
# specific regression class, the same way check_self_contained.cmake's
# readelf grep is good enough for its job without being a real ELF
# library. If LSPlant ever also starts requiring "import <name>;" for its
# own modules elsewhere, extend the REGEX below the same way.
# -----------------------------------------------------------------------
foreach(lsplant_src_file ${LSPLANT_SOURCES})
    # Plain spaces only (no \t escape) - deliberately not relying on how
    # CMake's string-escaping vs. its regex engine each treat "\t", since
    # that couldn't be tested in this sandbox (no local cmake binary - see
    # README.md). The one real-world instance of this line seen so far
    # (the CI log quoted above) has zero leading indentation anyway.
    file(STRINGS "${lsplant_src_file}" LSPLANT_MODULE_DECL_MATCH
         REGEX "^ *(export +)?module +[A-Za-z_][A-Za-z0-9_.]* *;")
    if(LSPLANT_MODULE_DECL_MATCH)
        get_filename_component(LSPLANT_SRC_BASENAME "${lsplant_src_file}" NAME)
        message(FATAL_ERROR
            "${LSPLANT_SRC_BASENAME} declares a C++20 module "
            "(\"${LSPLANT_MODULE_DECL_MATCH}\") - this toolchain (CMake "
            "3.22.1, as bundled with the Android NDK / AGP's external "
            "native build) cannot compile C++20 module implementation "
            "units; see the comment above LSPLANT_GIT_TAG near the top of "
            "this file. The commit currently checked out under "
            "${lsplant_src_SOURCE_DIR} (resolved from "
            "LSPLANT_PIN_BEFORE=${LSPLANT_PIN_BEFORE}) has drifted onto a "
            "version of LSPlant that uses C++20 modules. Fix: move "
            "LSPLANT_PIN_BEFORE earlier, delete "
            "${lsplant_src_SOURCE_DIR} so it re-clones, and reconfigure - "
            "or set LSPLANT_GIT_TAG to a specific older ref you've "
            "verified yourself against "
            "https://github.com/LSPosed/LSPlant/commits/master.")
    endif()
endforeach()

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
