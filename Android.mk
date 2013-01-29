ifeq ($(USE_CAMERA_STUB),false)
ifeq ($(USE_CAMERA_HAL2),true)
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

ifeq ($(TARGET_DEVICE),merr_vv)
USE_INTEL_JPEG := false
else
USE_INTEL_JPEG := true
endif

USE_INTEL_METABUFFER := true

ifeq ($(USE_INTEL_METABUFFER),true)
LOCAL_CFLAGS += -DENABLE_INTEL_METABUFFER
endif

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
	ImageScaler.cpp \
	ImageScaler1080Pto1024x576.cpp \
	EXIFMaker.cpp \
	JpegCompressor.cpp \
	SWJpegEncoder.cpp \
	CallbacksThread.cpp \
	LogHelper.cpp \
	PlatformData.cpp \
	FeatureData.cpp \
	IntelParameters.cpp \
	exif/ExifCreater.cpp \
	PostProcThread.cpp \
	PanoramaThread.cpp \
	AtomCommon.cpp \
	FaceDetector.cpp \
	nv12rotation.cpp \
	CameraDump.cpp \
	CameraAreas.cpp \
	BracketManager.cpp \
	AtomAcc.cpp

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
	frameworks/native/include/media/openmax \
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
	$(TARGET_OUT_HEADERS)/pvr/hal

ifeq ($(USE_INTEL_JPEG), true)
LOCAL_C_INCLUDES += \
	hardware/intel/libva
endif

LOCAL_C_FLAGS =+ -fno-pic

LOCAL_SHARED_LIBRARIES := \
	libcamera_client \
	libutils \
	libcutils \
	libbinder \
	libjpeg \
	libandroid \
	libui \
	libia_mkn \
	libmfldadvci \
	libia_nvm \
	libtbd \
	libsqlite \
	libdl

ifeq ($(USE_INTEL_METABUFFER),true)
LOCAL_SHARED_LIBRARIES += \
	libintelmetadatabuffer
endif

ifeq ($(USE_INTEL_JPEG), true)
LOCAL_SHARED_LIBRARIES += \
	libva \
	libva-tpi \
	libva-android
endif

LOCAL_STATIC_LIBRARIES := \
	libcameranvm \
	libia_coordinate

ifeq ($(USE_INTEL_JPEG), true)
LOCAL_CFLAGS += -DUSE_INTEL_JPEG
endif

ifeq ($(REF_DEVICE_NAME),mfld_gi)
LOCAL_CFLAGS += -DMFLD_GI

else ifneq (,$(findstring $(REF_DEVICE_NAME),mfld_dv10 redridge))
LOCAL_CFLAGS += -DMFLD_DV10
else ifneq (,$(findstring $(REF_DEVICE_NAME),victoriabay ctp_pr1 ctp_nomodem))
LOCAL_CFLAGS += -DCLVT
else ifeq ($(REF_DEVICE_NAME),mrfl_vp)
LOCAL_CFLAGS += -DMRFL_VP
else ifeq ($(TARGET_DEVICE),merr_vv)
LOCAL_CFLAGS += -DMERR_VV
else ifeq ($(TARGET_DEVICE),yukkabeach)
LOCAL_CFLAGS += -DYUKKA
else ifeq ($(REF_DEVICE_NAME),salitpa)
LOCAL_CFLAGS += -DSALITPA
else
LOCAL_CFLAGS += -DMFLD_PR2
endif

# enable R&D features only in R&D builds
ifneq ($(filter userdebug eng tests, $(TARGET_BUILD_VARIANT)),)
LOCAL_CFLAGS += -DLIBCAMERA_RD_FEATURES -Wunused-variable
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

