LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := disable_hdr
LOCAL_SRC_FILES := disable_hdr.cpp
LOCAL_LDLIBS := -log -landroid
LOCAL_C_INCLUDES := $(LOCAL_PATH)

include $(BUILD_SHARED_LIBRARY)
