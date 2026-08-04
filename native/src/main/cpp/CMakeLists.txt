cmake_minimum_required(VERSION 3.22.1)
project(disable_hdr)

# lsplant / dobby arrive as Prefab packages via the Gradle dependencies in
# native/build.gradle.kts (buildFeatures.prefab = true wires their AARs'
# headers + prebuilt libs into CMAKE_FIND_ROOT_PATH automatically - no
# manual path wrangling or git submodules needed).
find_package(lsplant REQUIRED CONFIG)
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
    lsplant::lsplant
    dobby::dobby
    log
)
