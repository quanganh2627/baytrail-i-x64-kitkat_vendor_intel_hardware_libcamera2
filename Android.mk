LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	ControlThread.cpp \
	PreviewThread.cpp \
	PictureThread.cpp \
	VideoThread.cpp \
	AAAThread.cpp \
	AtomISP.cpp \
	DebugFrameRate.cpp \
	Callbacks.cpp \
	AtomAAA.cpp \
	AtomHAL.cpp \
	ColorConverter.cpp \
	EXIFMaker.cpp \
	JpegCompressor.cpp \
	OlaFaceDetect.cpp \
	CallbacksThread.cpp \

LOCAL_C_INCLUDES += \
	frameworks/base/include \
	frameworks/base/include/binder \
	frameworks/base/include/camera \
	external/jpeg \
	hardware/libhardware/include/hardware \
	external/skia/include/core \
	external/skia/include/images \
	hardware/intel/libs3cjpeg \
	$(TARGET_OUT_HEADERS)/libsharedbuffer \
	$(TARGET_OUT_HEADERS)/libmfldadvci \
	$(TARGET_OUT_HEADERS)/libCameraFaceDetection \

ifeq ($(USE_INTEL_JPEG), true)
LOCAL_C_INCLUDES += \
	hardware/intel/libva \

endif

LOCAL_SHARED_LIBRARIES := \
	libcamera_client \
	libutils \
	libcutils \
	libbinder \
	libskia \
	libandroid \
	libui \
	libs3cjpeg \
	libsharedbuffer \
	libmfldadvci \
	libCameraFaceDetection \

ifeq ($(USE_INTEL_JPEG), true)
LOCAL_SHARED_LIBRARIES += \
	libjpeg \

LOCAL_CFLAGS += -DUSE_INTEL_JPEG

endif

# Setup a conditional so that the camera can be upright on both platforms that
# have "normal" (e.g., DV2) and "upside-down" (e.g., DV1) cameras.
define select_180_rotation
  $(eval LOCAL_CFLAGS += -DCAMERA_180_ROTATION)
endef
ifeq ($(TARGET_PRODUCT),mfld_dv10)
  $(eval $(call $(select_180_rotation)))
endif

# The camera.<TARGET_DEVICE>.so will be built for each platform
# (which should be unique to the TARGET_DEVICE environment)
# to use Camera Imaging(CI) supported by intel.
# If a platform does not support camera the USE_CAMERA_STUB 
# should be set to "true" in BoardConfig.mk
# LOCAL_MODULE := camera.$(TARGET_DEVICE)
 
ifeq ($(TARGET_PRODUCT),mfld_gi)
LOCAL_MODULE := camera.mfld_gi
else ifeq ($(TARGET_PRODUCT), mfld_dv10)
LOCAL_MODULE := camera.mfld_dv10
else ifeq ($(TARGET_PRODUCT), ctp_pr0)
LOCAL_MODULE := camera.ctp_pr0
else
LOCAL_MODULE := camera.mfld_pr2
endif
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
