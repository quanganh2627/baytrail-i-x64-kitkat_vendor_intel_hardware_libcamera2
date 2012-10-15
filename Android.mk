ifeq ($(USE_CAMERA_STUB),false)
ifeq ($(USE_CAMERA_HAL2),true)
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

USE_INTEL_JPEG :=true

# Intel camera extras (HDR, face detection, etc.)
ifeq ($(USE_INTEL_CAMERA_EXTRAS),true)
LOCAL_CFLAGS += -DENABLE_INTEL_EXTRAS
endif

LOCAL_SRC_FILES := \
	ControlThread.cpp \
	PreviewThread.cpp \
	PictureThread.cpp \
	VideoThread.cpp \
	AAAThread.cpp \
	AtomISP.cpp \
	DebugFrameRate.cpp \
	PerformanceTraces.cpp \
	Callbacks.cpp \
	AtomAAA.cpp \
	AtomDvs.cpp \
	AtomHAL.cpp \
	CameraConf.cpp \
	ColorConverter.cpp \
	EXIFMaker.cpp \
	JpegCompressor.cpp \
	SWJpegEncoder.cpp \
	CallbacksThread.cpp \
	LogHelper.cpp \
	PlatformData.cpp \
	IntelParameters.cpp \
	exif/ExifCreater.cpp \
	OlaService/IOlaBuffer.cpp \
	OlaService/IOlaBuffer.h  \
	OlaService/OlaBufferService.cpp \
	OlaService/HalProxyOla.cpp \
	PostProcThread.cpp \
	PanoramaThread.cpp \
	AtomCommon.cpp \
	FaceDetector.cpp \
	CameraDump.cpp \
	CameraAreas.cpp

ifeq ($(USE_INTEL_JPEG), true)
LOCAL_SRC_FILES += \
	JpegHwEncoder.cpp
endif

ifeq ($(USE_INTEL_CAMERA_EXTRAS),true)
LOCAL_SRC_FILES += AtomCP.cpp
endif

LOCAL_C_INCLUDES += \
	frameworks/base/include \
	frameworks/base/include/binder \
	frameworks/av/include/camera \
	external/jpeg \
	hardware/libhardware/include/hardware \
	external/skia/include/core \
	external/skia/include/images \
	external/sqlite/dist \
	$(TARGET_OUT_HEADERS)/libtbd \
	$(TARGET_OUT_HEADERS)/libmix_videoencoder \
	$(TARGET_OUT_HEADERS)/cameralibs \
	$(TARGET_OUT_HEADERS)/libmfldadvci \
	$(TARGET_OUT_HEADERS)/libCameraFaceDetection \
	$(TARGET_OUT_HEADERS)/pvr/hal \
	$(TARGET_OUT_HEADERS)/libva

ifeq ($(USE_INTEL_JPEG), true)
LOCAL_C_INCLUDES += \
	hardware/intel/libva
endif

LOCAL_SHARED_LIBRARIES := \
	libcamera_client \
	libutils \
	libcutils \
	libbinder \
	libjpeg \
	libandroid \
	libui \
	libmfldadvci \
	libintelmetadatabuffer \
	libtbd \
	libva \
	libva-tpi \
	libva-android \
	libsqlite

ifeq ($(USE_INTEL_JPEG), true)
LOCAL_CFLAGS += -DUSE_INTEL_JPEG
endif

ifeq ($(TARGET_PRODUCT), mfld_cdk)
LOCAL_CFLAGS += -DMFLD_CDK
else ifeq ($(TARGET_PRODUCT),mfld_gi)
LOCAL_CFLAGS += -DMFLD_GI
else ifneq (,$(findstring $(TARGET_PRODUCT),mfld_dv10 redridge salitpa))
LOCAL_CFLAGS += -DMFLD_DV10
else ifneq (,$(findstring $(TARGET_PRODUCT),victoriabay ctp_pr1 ctp_nomodem))
LOCAL_CFLAGS += -DCLVT
else ifeq ($(TARGET_PRODUCT), mrfl_vp)
LOCAL_CFLAGS += -DMRFL_VP
else
LOCAL_CFLAGS += -DMFLD_PR2
endif

# enable R&D features only in R&D builds
ifneq ($(filter userdebug eng tests, $(TARGET_BUILD_VARIANT)),)
LOCAL_CFLAGS += -DLIBCAMERA_RD_FEATURES
endif

# The camera.<TARGET_DEVICE>.so will be built for each platform
# (which should be unique to the TARGET_DEVICE environment)
# to use Camera Imaging(CI) supported by intel.
# If a platform does not support camera the USE_CAMERA_STUB
# should be set to "true" in BoardConfig.mk
LOCAL_MODULE := camera.$(TARGET_DEVICE)

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

endif  #ifeq ($(USE_CAMERA_HAL2),true)
endif #ifeq ($(USE_CAMERA_STUB),false)

