# Copyright (c) 2009-2010 Wind River Systems, Inc.
ifeq ($(USE_CAMERA_STUB),false)

#
# libcamera
#
$(shell cp hardware/intel/linux-2.6/include/linux/atomisp.h hardware/intel/include/linux/)
$(shell cp hardware/intel/linux-2.6/include/linux/videodev2.h hardware/intel/include/linux/)

ENABLE_BUFFER_SHARE_MODE := true

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
	libmfldadvci

LOCAL_SRC_FILES += \
	CameraHardware.cpp \
	IntelCamera.cpp \
	CameraAAAProcess.cpp \
	v4l2.c \
	atomisp_config.c \
	atomisp_features.c

LOCAL_CFLAGS += -DLOG_NDEBUG=1 -DSTDC99

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
	hardware/intel/PRIVATE/libmfldadvci/include \
	hardware/intel/include \
        $(TARGET_OUT_HEADERS)/libsharedbuffer

LOCAL_SHARED_LIBRARIES += libutils

ifeq ($(ENABLE_BUFFER_SHARE_MODE),true)
    LOCAL_CFLAGS  += -DENABLE_BUFFER_SHARE_MODE=1
    LOCAL_SHARED_LIBRARIES += libsharedbuffer
endif

include $(BUILD_SHARED_LIBRARY)

dest_dir := $(TARGET_OUT)/etc/atomisp/

files := \
	atomisp.cfg

copy_to := $(addprefix $(dest_dir)/,$(files))

$(copy_to): PRIVATE_MODULE := libcamera_etcdir
$(copy_to): $(dest_dir)/%: $(LOCAL_PATH)/% | $(ACP)
	$(transform-prebuilt-to-target)

ALL_PREBUILT += $(copy_to)

endif
