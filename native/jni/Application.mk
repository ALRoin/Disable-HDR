APP_ABI := arm64-v8a armeabi-v7a x86 x86_64
APP_PLATFORM := android-26
APP_STL := c++_static
APP_CPPFLAGS := -std=c++17 -fvisibility=hidden -fdata-sections -ffunction-sections
APP_LDFLAGS := -Wl,--gc-sections
