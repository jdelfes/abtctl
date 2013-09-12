LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := abtctl.c util.c
LOCAL_SHARED_LIBRARIES := libhardware
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE := abtctl

include $(BUILD_EXECUTABLE)
