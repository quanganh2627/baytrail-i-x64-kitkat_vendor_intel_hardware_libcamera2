ifeq ($(USE_CAMERA_STUB),false)
ifeq ($(USE_CAMERA_HAL2),true)
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

USE_INTEL_JPEG :=false
# USE_INTEL_FACE_DETECTION_IN_CAMERAHAL flag is controlled from
# InteFace.inc. If this file missing means, intel face
# detection is not supported.
# So -include will ignore if this file is not found.
-include $(TARGET_OUT_HEADERS)/cameralibs/IntelFace.inc

# if Intel face detection is included, select FD method to be used.
# face detection alternatives: ola, none
ifeq ($(USE_INTEL_FACE_DETECTION_IN_CAMERAHAL),true)
FACE_DETECTION_TYPE :=ola
else
FACE_DETECTION_TYPE :=none
endif

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
	CallbacksThread.cpp \
	LogHelper.cpp \
	PlatformData.cpp \
	IntelParameters.cpp \
	exif/ExifCreater.cpp

ifeq ($(FACE_DETECTION_TYPE),ola)
LOCAL_SRC_FILES += OlaFaceDetect.cpp 
LOCAL_CFLAGS 	+= -DOLA_FACEDETECTION
endif

LOCAL_C_INCLUDES += \
	frameworks/base/include \
	frameworks/base/include/binder \
	frameworks/av/include/camera \
	external/jpeg \
	hardware/libhardware/include/hardware \
	external/skia/include/core \
	external/skia/include/images \
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
	libsharedbuffer \
	libmfldadvci \
	
ifeq ($(FACE_DETECTION_TYPE),ola)
LOCAL_SHARED_LIBRARIES +=libCameraFaceDetection
endif

ifeq ($(USE_INTEL_JPEG), true)
LOCAL_SHARED_LIBRARIES += \
	libjpeg \

LOCAL_CFLAGS += -DUSE_INTEL_JPEG

endif

ifeq ($(TARGET_PRODUCT), mfld_cdk)
LOCAL_CFLAGS += -DMFLD_CDK
else ifeq ($(TARGET_PRODUCT),mfld_gi)
LOCAL_CFLAGS += -DMFLD_GI
else ifeq ($(TARGET_PRODUCT), mfld_dv10)
LOCAL_CFLAGS += -DMFLD_DV10
else ifeq ($(TARGET_PRODUCT), ctp_pr0)
LOCAL_CFLAGS += -DCTP_PR0
else ifeq ($(TARGET_PRODUCT), ctp_pr1)
LOCAL_CFLAGS += -DCTP_PR1
else ifeq ($(TARGET_PRODUCT), mrfl_vp)
LOCAL_CFLAGS += -DMRFL_VP
else
LOCAL_CFLAGS += -DMFLD_PR2
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
else ifeq ($(TARGET_PRODUCT), ctp_pr1)
LOCAL_MODULE := camera.ctp_pr1
else
LOCAL_MODULE := camera.mfld_pr2
endif

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

endif  #ifeq ($(USE_CAMERA_HAL2),true)
endif #ifeq ($(USE_CAMERA_STUB),false)
