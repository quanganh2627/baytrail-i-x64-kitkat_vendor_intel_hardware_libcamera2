# The camera HAL depends on the XML configuration file camera_profiles
# This section ensures that the files is present on the build image
#

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

#Please add the product name when a new config file was added

#default platform
PLATFORM_NAME := moorefield

ifeq ($(TARGET_DEVICE), mofd_v1)
PLATFORM_NAME := moorefield
endif

LOCAL_MODULE := camera_profiles.xml
LOCAL_MODULE_OWNER := intel
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(PLATFORM_NAME)/$(TARGET_DEVICE)/camera_profiles.xml
include $(BUILD_PREBUILT)
