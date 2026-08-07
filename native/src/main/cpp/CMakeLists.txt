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
# 2026-08-06 update - the next bug this caused:
#
# Pinning GIT_TAG to "master" (as this file did until now) traded the
# dynamic-linking bug above for a reproducibility bug: master is a moving
# target, and it moved. CI started failing with:
#
#   .../_deps/lsplant_src-src/lsplant/src/main/jni/lsplant.cc:25:8:
#   fatal error: module 'lsplant' not found
#      25 | module lsplant;
#
# `module lsplant;` is standard C++20 - it declares that file as the
# *implementation unit* of a named module called "lsplant"
# (https://en.cppreference.com/w/cpp/language/modules). Compiling it needs
# a build system that scans for module dependencies, compiles the
# corresponding *interface* unit into a BMI first, and passes every
# consumer "-fmodule-file=lsplant=<path>" so the compiler can find it
# (https://clang.llvm.org/docs/StandardCPlusPlusModules.html; for the
# identical failure shape see https://github.com/llvm/llvm-project/issues/53661).
# CMake didn't support any of that until 3.28 (Dec 2023) - six minor
# releases past the 3.22.1 the NDK bundles here - and even 3.28 only for
# Ninja/VS generators paired with Clang 16+/GCC 14+/MSVC 14.34+
# (https://cmake.org/cmake/help/latest/release/3.28.html;
# https://www.kitware.com/import-cmake-the-experiment-is-over/). There is
# no flag that fixes this on this toolchain. The only real fix is making
# sure the LSPlant commit we fetch predates whatever commit put that line
# there.
#
# Pinning to LSPlant's last *tagged* release ("6.4", Apr 2024 - confirmed
# via mvnrepository.com and central.sonatype.com) would dodge the modules
# problem, but 6.4 predates any Android 16 ART-hooking support LSPlant
# has, which defeats the point of vendoring current LSPlant at all - see
# the 2026-08-07 update below for exactly how far back that support
# actually goes, since the first attempt at dating it (in this section,
# originally) turned out to be wrong. LSPLANT_GIT_TAG below still accepts
# "6.4" as a manual, guaranteed-buildable fallback if you need one.
# -----------------------------------------------------------------------
# 2026-08-07 update - the auto-resolve mechanism this section originally
# added (pin to "the newest master commit before a hardcoded date,
# 2026-07-01") was tested for real and the date was wrong. CI logs from
# that run show the guard below did exactly its job - it failed the
# *configure* step immediately, by name, instead of letting a compiler
# error happen:
#
#   CMake Error at CMakeLists.txt:233 (message):
#     lsplant.cc declares a C++20 module ("module lsplant;") - ...
#     The commit currently checked out under .../_deps/lsplant_src-src
#     (resolved from LSPLANT_PIN_BEFORE=2026-07-01T00:00:00Z) has drifted
#     onto a version of LSPlant that uses C++20 modules.
#
# i.e. the mechanism worked exactly as designed; the date guess fed into
# it didn't. Further digging (this time via LSPlant's own /actions
# history and individual commit pages, both of which turned out to be
# fetchable even though /tags and /commits are not - see README.md's
# "Sources consulted" table) found real evidence the modules regression
# is very recent - a burst of commits in the "dual mapping" / FD-handling
# area of lsplant.cc from around June-July 2026 - sitting on top of a
# codebase that already had working Android 16 support going back much
# further than first thought: LSPlant's "Hook ReinitializeMethodsCode on
# A16+" commit (LSPosed/LSPlant@16205e7) is dated 2025-07-13, and a
# cluster of routine commits on 2026-04-25 (".github: Update build and
# test workflows", "lsplant: [skip ci] Fix typos in lsplant.hpp header
# comments") is the newest point this investigation could positively
# confirm as still being module-free.
#
# Rather than encode that specific date as a second guess - the first one
# was wrong by five-plus weeks, and there's no reason a second hand-picked
# date is inherently more trustworthy - LSPLANT_PIN_BEFORE is gone.
# Auto-resolve now does what a human would do by hand if they had a
# terminal open to the real repo: walk backwards from origin/master's tip,
# commit by commit, actually checking each one's lsplant.cc for module
# syntax, and stop at the newest one that's clean. That needs real network
# access to LSPlant's history, which is exactly what CI has and this
# sandbox doesn't (see "What I verified locally, and what I could not" in
# README.md) - but unlike a hardcoded date, it never goes stale: if
# upstream introduces modules again next month, or removes them again
# next year, this finds the correct answer either way without anyone
# needing to touch this file.
# -----------------------------------------------------------------------
set(LSPLANT_GIT_TAG "" CACHE STRING "Exact LSPlant git tag/branch/commit to build. Empty (default) auto-resolves by searching master's history instead - see the comments above. Set this once you've verified a specific ref yourself; \"6.4\" is a confirmed-real, confirmed-buildable fallback that predates Android 16 ART support (see README.md).")
set(LSPLANT_MAX_SEARCH_COMMITS "400" CACHE STRING "Only used when LSPLANT_GIT_TAG is empty. Auto-resolve walks back from origin/master's tip through at most this many commits looking for the newest one whose lsplant.cc doesn't use C++20 modules. LSPlant's whole history is a few hundred commits (see README.md), so 400 comfortably covers all of it; raise it only if that stops being true.")

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
    # Auto-resolve: needs real ancestry to walk, so this is a normal clone
    # rather than FetchContent's shallow single-commit fetch. Reuses a
    # previous checkout if one is already there instead of re-cloning
    # every configure; always fetches first so a stale local clone can't
    # miss recent history.
    if(NOT EXISTS "${lsplant_src_SOURCE_DIR}/.git")
        message(STATUS "LSPlant: cloning full history (auto-resolving the newest buildable commit)")
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
        COMMAND "${GIT_EXECUTABLE}" rev-list "--max-count=${LSPLANT_MAX_SEARCH_COMMITS}" origin/master
        WORKING_DIRECTORY "${lsplant_src_SOURCE_DIR}"
        OUTPUT_VARIABLE LSPLANT_CANDIDATE_SHAS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    string(REPLACE "\n" ";" LSPLANT_CANDIDATE_SHAS "${LSPLANT_CANDIDATE_SHAS}")
    list(LENGTH LSPLANT_CANDIDATE_SHAS LSPLANT_CANDIDATE_COUNT)
    message(STATUS "LSPlant: searching back from origin/master HEAD (up to ${LSPLANT_CANDIDATE_COUNT} commits) for the newest one this toolchain can compile")

    set(LSPLANT_RESOLVED_SHA "")
    set(LSPLANT_SEARCH_INDEX 0)
    foreach(lsplant_candidate_sha ${LSPLANT_CANDIDATE_SHAS})
        if(NOT lsplant_candidate_sha)
            continue()
        endif()
        math(EXPR LSPLANT_SEARCH_INDEX "${LSPLANT_SEARCH_INDEX} + 1")

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -c advice.detachedHead=false checkout --force ${lsplant_candidate_sha}
            WORKING_DIRECTORY "${lsplant_src_SOURCE_DIR}"
            COMMAND_ERROR_IS_FATAL ANY
        )

        file(GLOB_RECURSE LSPLANT_CANDIDATE_SOURCES CONFIGURE_DEPENDS
            "${lsplant_src_SOURCE_DIR}/lsplant/*.cc"
            "${lsplant_src_SOURCE_DIR}/lsplant/*.cpp"
        )
        list(FILTER LSPLANT_CANDIDATE_SOURCES EXCLUDE REGEX "/test/")

        set(LSPLANT_CANDIDATE_HAS_MODULES FALSE)
        foreach(lsplant_candidate_src_file ${LSPLANT_CANDIDATE_SOURCES})
            file(STRINGS "${lsplant_candidate_src_file}" LSPLANT_CANDIDATE_MODULE_MATCH
                 REGEX "^ *(export +)?module +[A-Za-z_][A-Za-z0-9_.]* *;")
            if(LSPLANT_CANDIDATE_MODULE_MATCH)
                set(LSPLANT_CANDIDATE_HAS_MODULES TRUE)
                break()
            endif()
        endforeach()

        if(LSPLANT_CANDIDATE_HAS_MODULES)
            message(STATUS "LSPlant: commit ${lsplant_candidate_sha} (#${LSPLANT_SEARCH_INDEX} back from HEAD) uses C++20 modules, trying an older commit")
        else()
            set(LSPLANT_RESOLVED_SHA "${lsplant_candidate_sha}")
            message(STATUS "LSPlant: resolved ${LSPLANT_RESOLVED_SHA} (#${LSPLANT_SEARCH_INDEX} commit(s) back from HEAD, counting HEAD as #1) as the newest commit this toolchain can build")
            break()
        endif()
    endforeach()

    if(NOT LSPLANT_RESOLVED_SHA)
        message(FATAL_ERROR
            "LSPlant: searched the newest ${LSPLANT_CANDIDATE_COUNT} commits on master "
            "and every single one uses C++20 module syntax this toolchain (CMake "
            "3.22.1) cannot compile. Either LSPlant has fully committed to a "
            "modules-only architecture now (in which case this vendor-from-source "
            "approach needs a fundamentally newer CMake/Ninja, not just a different "
            "commit - see the comment near the top of this file), or "
            "LSPLANT_MAX_SEARCH_COMMITS=${LSPLANT_MAX_SEARCH_COMMITS} simply isn't deep "
            "enough yet - check https://github.com/LSPosed/LSPlant/actions to see how "
            "far back the modules-affected range actually goes now.")
    endif()
    # LSPLANT_RESOLVED_SHA is already checked out - the loop's last
    # iteration (the one that broke out of it) left the working tree there.
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
# source turns out to be a C++20 module declaration - see the comments
# above LSPLANT_GIT_TAG for why nothing in this toolchain can compile one.
# In auto-resolve mode (the default) the search loop above already
# guarantees this can't fire - it only rechecks here for defense in depth
# in case that logic ever has a bug. In manual-pin mode (LSPLANT_GIT_TAG
# set), this is the *only* thing that catches a bad pin - there's no
# search to fall back to, so this is what actually fires if you pin
# straight into the modules-affected range by hand. Deliberately a simple
# heuristic (does some line look like a bare "module x;" / "export module
# x;" statement) rather than a real C++ parser - good enough to catch this
# specific regression class, the same way check_self_contained.cmake's
# readelf grep is good enough for its job without being a real ELF
# library.
# -----------------------------------------------------------------------
foreach(lsplant_src_file ${LSPLANT_SOURCES})
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
            "this file. If you're seeing this in auto-resolve mode "
            "(LSPLANT_GIT_TAG empty), the search loop above has a bug - "
            "please report it. If you set LSPLANT_GIT_TAG manually, pick a "
            "different ref - https://github.com/LSPosed/LSPlant/actions "
            "shows real commit history with dates even though /tags and "
            "/commits don't load directly (see README.md).")
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
