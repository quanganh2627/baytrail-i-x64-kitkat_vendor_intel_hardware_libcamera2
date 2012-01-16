LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        LogHelper.cpp \
        ControlThread.cpp \
        PreviewThread.cpp \
        PictureThread.cpp \
        AAAThread.cpp \
        AtomISP.cpp \
        DebugFrameRate.cpp \
        Callbacks.cpp \
        AtomAAA.cpp \
        AtomHAL.cpp \
        ColorConverter.cpp \

LOCAL_C_INCLUDES += \
	frameworks/base/include \
	frameworks/base/include/binder \
	frameworks/base/include/camera \
	hardware/libhardware/include/hardware \

LOCAL_SHARED_LIBRARIES := \
	libcamera_client \
	libutils \
	libcutils \
	libbinder \
	libandroid \
	libui \

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE := camera.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
