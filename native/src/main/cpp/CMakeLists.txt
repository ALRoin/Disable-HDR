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
# no flag that fixes this on this toolchain.
# -----------------------------------------------------------------------
# 2026-08-07 update - a date-based auto-resolve ("newest master commit
# before 2026-07-01") was tried and tested for real. The date was wrong -
# CI proved it by resolving straight into a commit that still had
# "module lsplant;" - so it was replaced with a search loop: walk
# backwards from origin/master's tip, commit by commit, actually checking
# each one's lsplant.cc for module syntax, stop at the newest one that's
# clean. See git history of this file / README.md for the full trail.
# -----------------------------------------------------------------------
# 2026-08-08 update - the search loop's check was extended to also catch
# "import x;" / "export import x;" (not just "module x;" / "export module
# x;"), after a real CI run found a commit whose lsplant.cc doesn't
# declare itself as a module but does "import dex_builder;" - a second,
# already-modularized LSPlant component (LSPosed/DexBuilder) - which the
# original check didn't catch. Same underlying cause, different shape.
# -----------------------------------------------------------------------
# 2026-08-09 update - the search loop (now checking both shapes) was run
# for real again, and this time it walked all the way back through every
# single commit with Android-16 support looking for one clean of C++20
# modules, and didn't stop until LSPosed/LSPlant@386200f ("Run CI on
# Apple silicon") - which sits *before* LSPosed/LSPlant@16205e7 ("Hook
# ReinitializeMethodsCode on A16+", 2025-07-13, the first Android-16 ART
# hook). The search loop's own printed WARNING said so plainly. Worse: that
# resolved commit then failed to compile for a *third*, unrelated reason -
# "fatal error: 'parallel_hashmap/phmap.h' file not found" - because
# LSPlant's own CMakeLists.txt fetches that header-only dependency itself
# and we don't run LSPlant's own CMakeLists.txt at all; we hand-glob its
# .cc/.cpp files and hardcode include paths, which works fine for LSPlant
# itself but silently misses anything *LSPlant* depends on that changes
# over time.
#
# Three CI rounds, three different failure shapes, all found by walking
# further and further back through the exact same search. That's a
# pattern, not bad luck: LSPlant's C++20-modules adoption goes back
# further than its Android-16 support does, so "modules-free" and
# "has Android 16 hooks" are not simultaneously satisfiable anywhere in
# the range this search can reach - and even where the search finds
# something that compiles, there is no guarantee it doesn't depend on
# something else we haven't vendored, because we never run LSPlant's own
# build, which is the only thing that actually knows LSPlant's real
# dependency graph. Continuing to patch this reactively, one CI round at
# a time, was not converging - see README.md's "Build fix, part 4" for
# the research this prompted (alternative hook libraries, both dead at
# Android 11; why a system-property-only approach can't replace hooking;
# what a real fix looks like) and the reasoning behind what changed below.
#
# The default now is LSPLANT_GIT_TAG="6.4" - LSPlant's last Maven-Central
# release (Apr 2024), confirmed buildable, confirmed to predate all of
# this. It does NOT have Android 16 ART hook support - that trade-off is
# explicit, not hidden; see the message this prints below and README.md.
# The history-walking search still exists - it's occasionally useful for
# exploring what's out there - but it's opt-in now
# (LSPLANT_AUTO_SEARCH_HISTORY=ON), because three real runs found it
# reliably discovering new ways to fail rather than a build that's both
# current and buildable.
# -----------------------------------------------------------------------
set(LSPLANT_GIT_TAG "6.4" CACHE STRING "Exact LSPlant git tag/branch/commit to build. Only used when LSPLANT_AUTO_SEARCH_HISTORY is OFF (the default). \"6.4\" is LSPlant's last Maven-published release: confirmed real, confirmed buildable on this toolchain, confirmed to predate Android 16 ART hook support - see README.md's \"Build fix, part 4\".")
set(LSPLANT_AUTO_SEARCH_HISTORY OFF CACHE BOOL "If ON, ignore LSPLANT_GIT_TAG and instead walk backwards from origin/master's tip looking for the newest commit this toolchain can compile (see the comments above). Off by default: three real runs of this found new failure shapes, not a usable build newer than 6.4. Turn it on to explore anyway, e.g. after LSPlant lands a real fix upstream.")
set(LSPLANT_MAX_SEARCH_COMMITS "400" CACHE STRING "Only used when LSPLANT_AUTO_SEARCH_HISTORY is ON. Caps how many commits back from origin/master's tip the search will look. LSPlant's whole history is a few hundred commits, so 400 comfortably covers all of it.")

find_package(Git REQUIRED)

set(lsplant_src_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/lsplant_src-src")

# -----------------------------------------------------------------------
# parallel-hashmap (greg7mdp/parallel-hashmap): a header-only dependency
# LSPlant's own CMakeLists.txt fetches for itself, that we now vendor
# defensively too - see the 2026-08-09 comment above for the CI run that
# found it missing. Header-only means there's nothing to build here, just
# a directory to add to the include path (upstream's own install
# instructions are literally "copy the parallel_hashmap directory to your
# project, update your include path" -
# https://github.com/greg7mdp/parallel-hashmap). Fetched unconditionally,
# whichever LSPlant path below is taken, since it costs one small extra
# clone and removes a whole class of "LSPlant needed a header we didn't
# know about" failures rather than just the one instance already seen.
# -----------------------------------------------------------------------
FetchContent_Declare(
    phmap_src
    GIT_REPOSITORY https://github.com/greg7mdp/parallel-hashmap.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(phmap_src)

if(LSPLANT_AUTO_SEARCH_HISTORY)
    # Auto-resolve: needs real ancestry to walk, so this is a normal clone
    # rather than FetchContent's shallow single-commit fetch. Reuses a
    # previous checkout if one is already there instead of re-cloning
    # every configure; always fetches first so a stale local clone can't
    # miss recent history.
    message(STATUS "LSPlant: LSPLANT_AUTO_SEARCH_HISTORY=ON - searching master's history instead of using LSPLANT_GIT_TAG=${LSPLANT_GIT_TAG}")
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

        # Three statement shapes: a self-declaration ("module x;" /
        # "export module x;"), a named-module import ("import x;" - e.g.
        # lsplant.cc's "import dex_builder;"), and "export import x;". All
        # three are equally unbuildable here - see the comment above
        # LSPLANT_GIT_TAG. Note this only proves the checked-out commit
        # doesn't declare/import a *named C++20 module* - it says nothing
        # about other missing dependencies (see the 2026-08-09 comment:
        # parallel-hashmap is now vendored defensively above specifically
        # because this check has no way to know about the next one).
        set(LSPLANT_CANDIDATE_HAS_MODULES FALSE)
        foreach(lsplant_candidate_src_file ${LSPLANT_CANDIDATE_SOURCES})
            file(STRINGS "${lsplant_candidate_src_file}" LSPLANT_CANDIDATE_MODULE_MATCH
                 REGEX "^ *(export +)?(module|import) +[A-Za-z_][A-Za-z0-9_.]* *;")
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
            "3.22.1) cannot compile. Set LSPLANT_AUTO_SEARCH_HISTORY=OFF to use "
            "LSPLANT_GIT_TAG=${LSPLANT_GIT_TAG} instead, or raise "
            "LSPLANT_MAX_SEARCH_COMMITS=${LSPLANT_MAX_SEARCH_COMMITS}.")
    endif()
    # LSPLANT_RESOLVED_SHA is already checked out - the loop's last
    # iteration (the one that broke out of it) left the working tree there.

    # -------------------------------------------------------------------
    # Same check as the 2026-08-07 finding: a modules-free commit can
    # still predate Android 16 ART hook support. Not fatal - it builds and
    # installs fine either way - but needs to be loud, since "builds fine,
    # quietly wrong ART offsets" is far harder to notice than a build
    # failure. See README.md's "Build fix, part 4" for the full trail.
    # -------------------------------------------------------------------
    set(LSPLANT_ANDROID16_FLOOR_SHA "16205e71aa3f028a729b00ed4aafd86793ea94f7")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" merge-base --is-ancestor
                ${LSPLANT_RESOLVED_SHA} ${LSPLANT_ANDROID16_FLOOR_SHA}
        WORKING_DIRECTORY "${lsplant_src_SOURCE_DIR}"
        RESULT_VARIABLE LSPLANT_RESOLVED_PREDATES_A16
    )
    if(LSPLANT_RESOLVED_PREDATES_A16 EQUAL 0)
        message(WARNING
            "LSPlant: the resolved commit ${LSPLANT_RESOLVED_SHA} is at or "
            "before LSPosed/LSPlant@${LSPLANT_ANDROID16_FLOOR_SHA} (2025-07-13, "
            "the first known Android-16 ART hook) - this build likely does "
            "NOT have working Android 16 support, even though it compiled "
            "successfully. This is not fatal - the build continues - but "
            "check README.md's \"Build fix, part 4\" before relying on this "
            "build against Android 16 hooks specifically.")
    endif()
else()
    # Manual pin (the default path): a plain shallow-clone FetchContent for
    # one fixed ref. Simple, fast, and - for "6.4" specifically - already
    # proven to actually build on this exact toolchain.
    message(STATUS "LSPlant: using LSPLANT_GIT_TAG=${LSPLANT_GIT_TAG} (set LSPLANT_AUTO_SEARCH_HISTORY=ON to search master's history instead)")
    if(LSPLANT_GIT_TAG STREQUAL "6.4")
        message(STATUS "LSPlant: 6.4 (Apr 2024) predates Android 16 ART hook support (LSPosed/LSPlant@16205e7, 2025-07-13) - see README.md's \"Build fix, part 4\" for what that does and doesn't affect, and for the longer-term options if you need current Android 16 hooks.")
    endif()
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
# source turns out to use C++20 modules - a self-declaration ("module x;"
# / "export module x;"), a named-module import ("import x;"), or a
# re-exported import ("export import x;") - see the comments above
# LSPLANT_GIT_TAG for why nothing in this toolchain can compile any of
# these. With the default LSPLANT_GIT_TAG=6.4 this should never fire; it's
# here mainly for LSPLANT_AUTO_SEARCH_HISTORY=ON (defense in depth - the
# search loop above already guarantees this can't fire there either,
# this only rechecks in case that logic ever has a bug) and for anyone who
# manually pins LSPLANT_GIT_TAG to something else, where this is the
# *only* thing that catches a bad pin. Deliberately a simple heuristic
# (does some line look like one of those statement shapes) rather than a
# real C++ parser - good enough to catch this specific regression class,
# the same way check_self_contained.cmake's readelf grep is good enough
# for its job without being a real ELF library.
# -----------------------------------------------------------------------
foreach(lsplant_src_file ${LSPLANT_SOURCES})
    file(STRINGS "${lsplant_src_file}" LSPLANT_MODULE_DECL_MATCH
         REGEX "^ *(export +)?(module|import) +[A-Za-z_][A-Za-z0-9_.]* *;")
    if(LSPLANT_MODULE_DECL_MATCH)
        get_filename_component(LSPLANT_SRC_BASENAME "${lsplant_src_file}" NAME)
        message(FATAL_ERROR
            "${LSPLANT_SRC_BASENAME} uses C++20 modules "
            "(\"${LSPLANT_MODULE_DECL_MATCH}\") - this toolchain (CMake "
            "3.22.1, as bundled with the Android NDK / AGP's external "
            "native build) cannot compile a module declaration or a "
            "named-module import; see the comment above LSPLANT_GIT_TAG near the top of "
            "this file. Set LSPLANT_GIT_TAG back to \"6.4\" (the default) "
            "or another ref you've verified yourself - "
            "https://github.com/LSPosed/LSPlant/actions shows real commit "
            "history with dates even though /tags and /commits don't load "
            "directly (see README.md).")
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
# parallel-hashmap goes on the include path *after* the "did we actually
# find LSPlant's own headers" check above, deliberately - it always
# resolves (it's fetched unconditionally near the top of this file), so
# seeding LSPLANT_INCLUDE_DIRS with it before that check would make the
# list non-empty regardless of whether LSPlant's own headers were found,
# silently defeating the one thing that check exists to catch.
list(APPEND LSPLANT_INCLUDE_DIRS "${phmap_src_SOURCE_DIR}")

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
