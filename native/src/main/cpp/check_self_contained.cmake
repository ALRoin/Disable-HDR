# Run via `cmake -DREADELF=... -DTARGET_SO=... -P check_self_contained.cmake`
# as a POST_BUILD step on the disable_hdr target (see CMakeLists.txt).
#
# A Zygisk module is loaded by the root solution's own minimal in-process
# loader (ZygiskNext logs this as its "builtin linker"), not by a normal
# dlopen() with full linker-namespace / jniLibs-style search-path support.
# That loader can only resolve symbols against libraries already mapped
# into the target process - i.e. the small set of system libraries every
# Android process already has loaded - and nothing else. Any DT_NEEDED
# entry outside that set means the module fails to load, silently, in
# every process, forever: no crash, no log line from the module itself,
# just every hook quietly never installing. That is exactly what shipped
# before this file existed (DT_NEEDED liblsplant.so, discovered only via a
# full bug report and a logcat grep for ZygiskNext's own loader errors).
#
# This script re-derives that check at build time instead.

if(NOT READELF)
    message(FATAL_ERROR "check_self_contained.cmake: READELF was not set")
endif()
if(NOT TARGET_SO)
    message(FATAL_ERROR "check_self_contained.cmake: TARGET_SO was not set")
endif()

execute_process(
    COMMAND "${READELF}" -d "${TARGET_SO}"
    OUTPUT_VARIABLE DYNAMIC_SECTION
    RESULT_VARIABLE READELF_RESULT
)
if(NOT READELF_RESULT EQUAL 0)
    message(FATAL_ERROR "check_self_contained.cmake: '${READELF} -d ${TARGET_SO}' failed")
endif()

# Matches both GNU readelf's "(NEEDED)" and llvm-readelf's "NEEDED" forms,
# e.g.:  0x0000000000000001 (NEEDED)  Shared library: [liblog.so]
string(REGEX MATCHALL "NEEDED\\)?[^\n]*\\[([^]\n]+)\\]" NEEDED_LINES "${DYNAMIC_SECTION}")

# Libraries guaranteed present in every Android process (Bionic, the
# system libc++, and other base platform libs a Zygisk module could
# plausibly and legitimately end up referencing). Deliberately does NOT
# include libc++_shared.so - that one is only present if something else in
# the same process happened to load it, which is not something a Zygisk
# module can rely on.
set(ALLOWED_LIBS
    "libc\\.so" "libm\\.so" "libdl\\.so" "liblog\\.so" "libz\\.so"
    "libc\\+\\+\\.so" "libandroid\\.so"
)

set(BAD_LIBS "")
foreach(line ${NEEDED_LINES})
    string(REGEX REPLACE ".*\\[([^]\n]+)\\].*" "\\1" LIBNAME "${line}")
    set(IS_ALLOWED FALSE)
    foreach(pattern ${ALLOWED_LIBS})
        if(LIBNAME MATCHES "^${pattern}$")
            set(IS_ALLOWED TRUE)
        endif()
    endforeach()
    if(NOT IS_ALLOWED)
        list(APPEND BAD_LIBS "${LIBNAME}")
    endif()
endforeach()

if(BAD_LIBS)
    list(JOIN BAD_LIBS ", " BAD_LIBS_STR)
    message(FATAL_ERROR
        "disable_hdr.so depends on: ${BAD_LIBS_STR}. This is not guaranteed "
        "to be mapped into every Android process, so the root solution's "
        "Zygisk loader will fail with something like "
        "\"Not found: '<lib>' needed by 'disable_hdr'\" on-device, and "
        "every hook will silently never install - the exact bug this "
        "check exists to catch. Link that dependency statically instead "
        "(see how LSPlant is handled above), or add it to ALLOWED_LIBS in "
        "this file if you've confirmed it really is always present.")
else()
    message(STATUS "disable_hdr.so self-containment check passed (no non-system DT_NEEDED entries)")
endif()
