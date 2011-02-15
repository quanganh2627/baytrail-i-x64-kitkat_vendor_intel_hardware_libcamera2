LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	src/ccrgb16toyuv420sp.cpp \
 	src/cczoomrotationbase.cpp \

LOCAL_MODULE := libcameracc

LOCAL_CFLAGS := -DFALSE=false

# Temporary workaround
ifeq ($(strip $(USE_SHOLES_PROPERTY)),true)
LOCAL_CFLAGS += -DSHOLES_PROPERTY_OVERRIDES
endif

LOCAL_STATIC_LIBRARIES := 

LOCAL_SHARED_LIBRARIES := libutils

LOCAL_C_INCLUDES := \
	./src/

include $(BUILD_STATIC_LIBRARY)
