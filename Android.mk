# Copyright (c) 2009-2010 Wind River Systems, Inc.
ifeq ($(USE_CAMERA_STUB),false)
ifeq ($(CUSTOM_BOARD), medfield)
#
# libcamera
#

ENABLE_BUFFER_SHARE_MODE := false
ENABLE_HWLIBJPEG_BUFFER_SHARE := false

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
	libskia \
	libmfldadvci \
	libs3cjpeg \
	libandroid \
	libui \

LOCAL_SRC_FILES += \
	CameraHardware.cpp \
	IntelCamera.cpp \
	CameraAAAProcess.cpp

LOCAL_CFLAGS += -DLOG_NDEBUG=1 -DSTDC99 -Wno-write-strings

ifeq ($(TARGET_PRODUCT), mfld_cdk)
LOCAL_CFLAGS += -DMFLD_CDK
else
LOCAL_CFLAGS += -DMFLD_PR2
endif

ifeq ($(TARGET_PRODUCT), mfld_dv09)
LOCAL_CFLAGS += -DMFLD_DV09
endif

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
	frameworks/base/include \
	frameworks/base/include/binder \
	frameworks/base/include/camera \
	external/skia/include/core \
	external/skia/include/images \
	$(TARGET_OUT_HEADERS)/libmfldadvci \
	$(TARGET_OUT_HEADERS)/libsharedbuffer \
	hardware/intel/libs3cjpeg

LOCAL_SHARED_LIBRARIES += libutils

ifeq ($(ENABLE_BUFFER_SHARE_MODE),true)
    LOCAL_CFLAGS  += -DENABLE_BUFFER_SHARE_MODE=1
    LOCAL_SHARED_LIBRARIES += libsharedbuffer
endif

LOCAL_SHARED_LIBRARIES += libjpeg
LOCAL_SRC_FILES += libjpegwrap.cpp
LOCAL_C_INCLUDES += hardware/intel/libva \
                     external/jpeg
ifeq ($(ENABLE_HWLIBJPEG_BUFFER_SHARE),true)
    LOCAL_CFLAGS += -DENABLE_HWLIBJPEG_BUFFER_SHARE
endif

include $(BUILD_SHARED_LIBRARY)

endif
endif
