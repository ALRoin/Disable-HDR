LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := disable_hdr
LOCAL_SRC_FILES := disable_hdr.cpp
LOCAL_LDLIBS := -llog -lstdc++
include $(BUILD_SHARED_LIBRARY)
