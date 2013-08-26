ifeq ($(USE_CAMERA_STUB),false)
ifeq ($(USE_CAMERA_HAL2),true)
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

ifeq ($(USE_INTEL_METABUFFER),true)
LOCAL_CFLAGS += -DENABLE_INTEL_METABUFFER
endif

ifeq ($(BOARD_GRAPHIC_IS_GEN),true)
LOCAL_CFLAGS += -DGRAPHIC_IS_GEN
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
	AtomAIQ.cpp \
	AtomSoc3A.cpp \
	AtomDvs.cpp \
	AtomDvs2.cpp \
	AtomHAL.cpp \
	CameraConf.cpp \
	ColorConverter.cpp \
	ImageScaler.cpp \
	EXIFMaker.cpp \
	JpegCompressor.cpp \
	SWJpegEncoder.cpp \
	CallbacksThread.cpp \
	LogHelper.cpp \
	MemoryUtils.cpp \
	PlatformData.cpp \
	CameraProfiles.cpp \
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
	GPUScaler.cpp \
	AtomAcc.cpp \
	AtomIspObserverManager.cpp \
	SensorThread.cpp \
	ScalerService.cpp \
	PostCaptureThread.cpp \
	SensorSyncManager.cpp \
	AccManagerThread.cpp \
	v4l2dev/v4l2devicebase.cpp \
	v4l2dev/v4l2videonode.cpp \
	v4l2dev/v4l2subdevice.cpp 

ifeq ($(USE_INTEL_JPEG), true)
LOCAL_SRC_FILES += \
	JpegHwEncoder.cpp
endif

ifeq ($(USE_INTEL_CAMERA_EXTRAS),true)
LOCAL_SRC_FILES += AtomCP.cpp \
                   UltraLowLight.cpp
endif

LOCAL_C_INCLUDES += \
	$(call include-path-for, frameworks-base) \
	$(call include-path-for, frameworks-base)/binder \
	$(call include-path-for, frameworks-av)/camera \
	$(call include-path-for, frameworks-native)/media/openmax \
	$(call include-path-for, jpeg) \
	$(call include-path-for, libhardware)/hardware \
	$(call include-path-for, skia)/core \
	$(call include-path-for, skia)/images \
	$(call include-path-for, sqlite) \
	$(TARGET_OUT_HEADERS)/libtbd \
	$(TARGET_OUT_HEADERS)/libmix_videoencoder \
	$(TARGET_OUT_HEADERS)/cameralibs \
	$(TARGET_OUT_HEADERS)/libmfldadvci \
	$(TARGET_OUT_HEADERS)/libCameraFaceDetection \
	$(LOCAL_PATH)/v4l2dev/

ifeq ($(BOARD_GRAPHIC_IS_GEN), true)
else
LOCAL_C_INCLUDES += \
	$(TARGET_OUT_HEADERS)/pvr/hal
endif

ifeq (,$(wildcard frameworks/base/core/jni/android_hardware_Camera.h))
LOCAL_C_INCLUDES += \
	vendor/intel/hardware/camera_extension/include/
endif

ifeq ($(USE_INTEL_JPEG), true)
LOCAL_C_INCLUDES += \
	vendor/intel/hardware/libva
endif

LOCAL_C_FLAGS =+ -fno-pic

LOCAL_SHARED_LIBRARIES := \
	libcamera_client \
	libutils \
	libcutils \
	libbinder \
	libjpeg \
	libia_aiq \
	libia_isp_1_5 \
	libia_isp_2_2 \
	libia_cmc_parser \
	libui \
	libia_mkn \
	libmfldadvci \
	libia_dvs_2 \
	libia_nvm \
	libtbd \
	libsqlite \
	libdl \
	libEGL \
	libGLESv2 \
	libgui \
	libexpat

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
	gdctool \
	libia_coordinate \
        libmorpho_image_stabilizer3

ifeq ($(USE_INTEL_JPEG), true)
LOCAL_CFLAGS += -DUSE_INTEL_JPEG
endif

ifeq ($(USE_CSS_2_0), true)
LOCAL_CFLAGS += -DATOMISP_CSS2
endif

# enable R&D features only in R&D builds
ifneq ($(filter userdebug eng tests, $(TARGET_BUILD_VARIANT)),)
LOCAL_CFLAGS += -DLIBCAMERA_RD_FEATURES -Wunused-variable -Werror
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

