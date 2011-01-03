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
	libsensor \
	libisphal \
	libci \
	libadvci

LOCAL_SRC_FILES += \
	CameraHardware.cpp \
	IntelCamera.cpp

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
	hardware/intel/libci/include

LOCAL_STATIC_LIBRARIES +=

include $(BUILD_SHARED_LIBRARY)

# ci-app test program
include $(CLEAR_VARS)
LOCAL_MODULE := ci-app
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := \
	libcamera_client \
	libutils \
	libcutils \
	libisphal \
	libci \
	libadvci

LOCAL_SRC_FILES += ci-app.c

LOCAL_CFLAGS += -DLOG_TAG=\"CI-APP\" -DBOOL_ENABLE

LOCAL_C_INCLUDES += \
	hardware/intel/libci/include

include $(BUILD_EXECUTABLE)

endif
