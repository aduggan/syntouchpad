LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := syntouchpad
LOCAL_SRC_FILES := syntouchpad.c
LOCAL_CPPFLAGS := -Wall

include $(BUILD_EXECUTABLE)
