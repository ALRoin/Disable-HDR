LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := hdr_block
LOCAL_SRC_FILES := hdr_block.cpp
LOCAL_STATIC_LIBRARIES := libcxx
LOCAL_LDLIBS := -llog
include $(BUILD_SHARED_LIBRARY)

include jni/libcxx/Android.mk
