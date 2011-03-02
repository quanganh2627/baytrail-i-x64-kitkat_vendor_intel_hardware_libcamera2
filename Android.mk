# Copyright (c) 2009-2010 Wind River Systems, Inc.
ifeq ($(USE_CAMERA_STUB),false)

#
# libcamera
#

LOCAL_PATH := $(call my-dir)

LIBCAMERA_TOP := $(LOCAL_PATH)

include $(CLEAR_VARS)

LOCAL_MODULE := libcamera
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := \
	libcamera_client \
	libutils \
	libcutils \
	libdl \
	libbinder \
	libskia

LOCAL_SRC_FILES += \
	CameraHALBridge.cpp \
	CameraHardware.cpp \
	IntelCamera.cpp \
	v4l2.c

LOCAL_CFLAGS += -DLOG_NDEBUG=1

ifeq ($(BOARD_USES_CAMERA_TEXTURE_STREAMING), true)
LOCAL_CFLAGS += -DBOARD_USE_CAMERA_TEXTURE_STREAMING
else
LOCAL_CFLAGS += -UBOARD_USE_CAMERA_TEXTURE_STREAMING
endif

ifneq ($(BOARD_USES_WRS_OMXIL_CORE), true)
LOCAL_CFLAGS += -DBOARD_USE_SOFTWARE_ENCODE
else
LOCAL_CFLAGS += -UBOARD_USE_SOFTWARE_ENCODE
endif

LOCAL_C_INCLUDES += \
	frameworks/base/include/camera \
	external/skia/include/core \
	external/skia/include/images \
	hardware/intel/libcamera/colorconvert/src

LOCAL_STATIC_LIBRARIES += libcameracc
LOCAL_SHARED_LIBRARIES += libutils

include $(BUILD_SHARED_LIBRARY)

#
# color convert
#
include hardware/intel/libcamera/colorconvert/Android.mk


endif
