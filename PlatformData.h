/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef PLATFORMDATA_H_
#define PLATFORMDATA_H_

#include "CameraConf.h"
#include <utils/String8.h>
#include <utils/Vector.h>

#define RESOLUTION_14MP_WIDTH   4352
#define RESOLUTION_14MP_HEIGHT  3264
#define RESOLUTION_13MP_WIDTH   4192
#define RESOLUTION_13MP_HEIGHT  3104
#define RESOLUTION_8MP_WIDTH    3264
#define RESOLUTION_8MP_HEIGHT   2448
#define RESOLUTION_5MP_WIDTH    2560
#define RESOLUTION_5MP_HEIGHT   1920
#define RESOLUTION_3MP_WIDTH    2048
#define RESOLUTION_3MP_HEIGHT   1536
#define RESOLUTION_1_3MP_WIDTH  1280
#define RESOLUTION_1_3MP_HEIGHT 960
#define RESOLUTION_1080P_WIDTH  1920
#define RESOLUTION_1080P_HEIGHT 1080
#define RESOLUTION_2MP_WIDTH  1600
#define RESOLUTION_2MP_HEIGHT 1200
#define RESOLUTION_720P_WIDTH   1280
#define RESOLUTION_720P_HEIGHT  720
#define RESOLUTION_480P_WIDTH   768
#define RESOLUTION_480P_HEIGHT  480
#define RESOLUTION_VGA_WIDTH    640
#define RESOLUTION_VGA_HEIGHT   480
#define RESOLUTION_POSTVIEW_WIDTH    320
#define RESOLUTION_POSTVIEW_HEIGHT   240

#include <camera.h>
#include "AtomCommon.h"
#include <IntelParameters.h>

namespace android {

/**
 * \file PlatformData.h
 *
 * HAL internal interface for managing platform specific static
 * data.
 *
 * Design principles for platform data mechanism:
 *
 * 1. Make it easy as possible to add new configurable data.
 *
 * 2. Make it easy as possible to add new platforms.
 *
 * 3. Allow inheriting platforms from one another (as we'll typically
 *    have many derived platforms).
 *
 * 4. Split implementations into separate files, to avoid
 *    version conflicts with parallel work targeting different
 *    platforms.
 *
 * 5. Focus on plain flat data and avoid defining new abstractions
 *    and relations.
 *
 * 6. If any #ifdefs are needed, put them in platform files.
 *
 * 7. Keep the set of parameters to a minimum, and only add
 *    data that really varies from platform to another.
 */

// Forward declarations
class PlatformBase;

/**
 * \class PlatformData
 *
 * This class is a singleton that contains all the static information
 * from the platform. It doesn't store any state. It is a data repository
 * for static data. It provides convenience methods to initialize
 * some parameters based on the HW limitations.
 */

class PlatformData {

 private:
    static PlatformBase* mInstance;

    /**
     * Get access to the platform singleton.
     *
     * Note: this is implemented in PlatformFactory.cpp
     */
    static PlatformBase* getInstance(void);

 public:

    static AiqConf AiqConfig;
    static HalConf HalConfig;

    enum SensorFlip {
        SENSOR_FLIP_NA     = -1,   // Support Not-Available
        SENSOR_FLIP_OFF    = 0x00, // Both flip ctrls set to 0
        SENSOR_FLIP_H      = 0x01, // V4L2_CID_HFLIP 1
        SENSOR_FLIP_V      = 0x02, // V4L2_CID_VFLIP 1
    };

    /**
     * Number of cameras
     *
     * Returns number of cameras that may be opened with
     * android.hardware.Camera.open().
     *
     * \return a non-negative integer
     */
    static int numberOfCameras(void);

    /**
     * Sensor type of camera id
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return SensorType of camera.     */
    static SensorType sensorType(int cameraId);

    /**
     * Facing of camera id
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return int value as defined in Camera.CameraInfo
     */
    static int cameraFacing(int cameraId);

    /**
     * Whether the back camera has flash?
     *
     * \return true if flash is available
     */
    static bool supportsBackFlash(void);

    /**
     * Whether image data injection from file is supported?
     *
     * \return true if supported
     */
    static bool supportsFileInject(void);

    /**
     * Whether platform can support continuous capture mode in terms of
     * SoC and ISP performance.
     *
     * \return true if supported
     */
    static bool supportsContinuousCapture(void);

    /**
     * What's the maximum supported size of the RAW ringbuffer
     * for continuous capture maintained by the ISP.
     *
     * This depends both on kernel and CSS firmware, but also total
     * available system memory that should be used for imaging use-cases.
     *
     * \return int number 0...N, if supportsContinuousCapture() is
     *         false, this function will always return 0
     */
    static int maxContinuousRawRingBufferSize(void);

    /**
     * Returns the average lag between user pressing shutter UI button or
     * key, to camera HAL receiving take_picture method call.
     *
     * This value is used to fine-tune frame selection for Zero
     * Shutter Lag.
     *
     * \return int lag time in milliseconds
     */
    static int shutterLagCompensationMs(void);

    /**
     * Orientation of camera id
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return int value as defined in Camera.CameraInfo
     */
    static int cameraOrientation(int cameraId);

    /**
     * Returns string described preferred preview size for video
     *
     * \return string following getParameter value notation
     */
    static const char* preferredPreviewSizeForVideo(void);

    /**
     * Whether the camera supports Digital Video Stabilization or not
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return true if it is supported
     */
    static bool supportsDVS(int cameraId);

    /**
     * Returns the supported burst capture's fps list for the platform
     * \return Supported burst capture's fps list string.
     */
    static const char* supportedBurstFPS(int CameraId);

    /**
     * Returns the supported burst capture's length list for the platform
     * \return Supported burst capture's length list string.
     */
    static const char* supportedBurstLength(int CameraId);

    /**
     * Returns the max burst FPS
     * \return Supported the max burst FPS.
     */
    static int getMaxBurstFPS(int CameraId);

    /**
     * Flipping controls to set for camera id
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return int value as defined in PlatformData::SENSOR_FLIP_FLAGS
     */
    static int sensorFlipping(int cameraId);

    /**
     * Exposure compensation default value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the default value as a string.
     */
    static const char* supportedDefaultEV(int cameraId);

    /**
     * Exposure compensation max value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the max as a string.
     */
    static const char* supportedMaxEV(int cameraId);

    /**
     * Exposure compensation min value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the min as a string.
     */
    static const char* supportedMinEV(int cameraId);

    /**
     * Exposure compensation step value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the step as a string.
     */
    static const char* supportedStepEV(int cameraId);

    /**
     * Ae Metering mode supported value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value supported for ae metering as a string.
     */
    static const char* supportedAeMetering(int cameraId);

    /**
     * Default Ae Metering value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the default ae metering as a string.
     */
    static const char* defaultAeMetering(int cameraId);

    /**
     * Saturation default value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the iso default value as a string.
     */
    static const char* defaultSaturation(int cameraId);

    /**
     * Saturation default value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the default saturation as a string.
     */
    static const char* supportedSaturation(int cameraId);

    /**
     * Saturation max value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the max saturation as a string.
     */
    static const char* supportedMaxSaturation(int cameraId);

    /**
     * Saturation min value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the min saturation as a string.
     */
    static const char* supportedMinSaturation(int cameraId);

    /**
     * Saturation step value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the saturation step as a string.
     */
    static const char* supportedStepSaturation(int cameraId);

    /**
     * Contrast default value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the iso default value as a string.
     */
    static const char* defaultContrast(int cameraId);

    /**
     * Contrast default value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the default contrast as a string.
     */
    static const char* supportedContrast(int cameraId);

    /**
     * Contrast max value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the max contrast as a string.
     */
    static const char* supportedMaxContrast(int cameraId);

    /**
     * Contrast min value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the min contrast as a string.
     */
    static const char* supportedMinContrast(int cameraId);

    /**
     * Contrast step value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the contrast step as a string.
     */
    static const char* supportedStepContrast(int cameraId);

    /**
     * Sharpness default value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the iso default value as a string.
     */
    static const char* defaultSharpness(int cameraId);

    /**
     * Sharpness default value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the default sharpness as a string.
     */
    static const char* supportedSharpness(int cameraId);

    /**
     * Sharpness max value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the max sharpness as a string.
     */
    static const char* supportedMaxSharpness(int cameraId);

    /**
     * Sharpness min value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the min sharpness as a string.
     */
    static const char* supportedMinSharpness(int cameraId);

    /**
     * Sharpness step value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the sharpness step as a string.
     */
    static const char* supportedStepSharpness(int cameraId);

    /**
     * Flash mode supported value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the flash supported as a string.
     */
    static const char* supportedFlashModes(int cameraId);

    /**
     * Flash mode default value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the flash default value as a string.
     */
    static const char* defaultFlashMode(int cameraId);

    /**
     * Iso mode supported value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the flash supported as a string.
     */
    static const char* supportedIso(int cameraId);

    /**
     * Iso default value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the iso default value as a string.
     */
    static const char* defaultIso(int cameraId);

    /**
     * Returns the supported scene modes for the platform
     * \return Supported scene mode string, or empty string
     *  upon error.
     */
    static const char* supportedSceneModes(int cameraId);

    /**
     * scene mode default value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the scene mode default value as a string.
     */
    static const char* defaultSceneMode(int cameraId);

    /**
     * Returns the supported effect modes for the platform
     * \return Supported effect mode string, or empty string
     *  upon error.
     */
    static const char* supportedEffectModes(int cameraId);

    /**
     * Returns the supported Intel specific effect modes for the platform
     * \return Supported effect mode string, or empty string
     *  upon error.
     */
    static const char* supportedIntelEffectModes(int cameraId);

    /**
     * effect mode default value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the effect mode default value as a string.
     */
    static const char* defaultEffectMode(int cameraId);

    /**
     * Returns the supported awb modes for the platform
     * \return Supported awb mode string, or empty string
     *  upon error.
     */
    static const char* supportedAwbModes(int cameraId);

    /**
     * awb mode default value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the awb mode default value as a string.
     */
    static const char* defaultAwbMode(int cameraId);

    /**
     * preview frame rate supported value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value supported for preview frame rate.
     */
    static const char* supportedPreviewFrameRate(int cameraId);

    /**
     * preview fps range supported value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value supported for preview fps range.
     */
    static const char* supportedPreviewFPSRange(int cameraId);

    /**
     * Default preview FPS range
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the default preview fps range as a string.
     */
    static const char* defaultPreviewFPSRange(int cameraId);

    /**
     * supported preview sizes
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the supported preview sizes as a string.
     */
    static const char* supportedPreviewSizes(int cameraId);

    /**
     * supported preview update modes
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the supported preview update mode as a string.
     */
    static const char* supportedPreviewUpdateModes(int cameraId);

    /**
     * default preview update mode
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the default preview update mode as a string.
     */
    static const char* defaultPreviewUpdateMode(int cameraId);

    /**
     * Whether the slow motion playback in high speed recording mode is supported?
     * \return true if the slow motion playback is supported
     */
    static bool supportsSlowMotion(int cameraId);

    /**
     * Focus mode supported value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the focus supported as a string.
     */
    static const char* supportedFocusModes(int cameraId);

    /**
     * Focus mode default value
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the focus default value as a string.
     */
    static const char* defaultFocusMode(int cameraId);


    /**
     * supported video sizes
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the supported video sizes as a string.
     */
    static const char* supportedVideoSizes(int cameraId);

    /**
     * supported snapshot sizes
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return the value of the supported snapshot sizes as a string.
     */
    static const char* supportedSnapshotSizes(int cameraId);

    /**
     * Returns the name of the product
     * This is meant to be used in the EXIF metadata
     *
     * \return string with product name
     */
    static const char* productName(void);

    /**
     * Returns the name of the manufacturer
     * This is meant to be used in the EXIF metadata
     *
     * \return string with product manufacturer
     */
    static const char* manufacturerName(void);

    /**
     * Returns sensor parameter files for input sensor
     *
     * \param sensor_id identifier to sensor
     * \return pointer to sensor parameter file
    */
    static const SensorParams *getSensorParamsFile(char *sensorId);

    /**
     * Returns the ISP sub device name
     *
     * \return the ISP sub device name, it'll return NULL when it fails
    */
    static const char* getISPSubDeviceName(void);

    /**
     * Returns the max panorama snapshot count
     * \return the max panorama snapshot count
     */
    static int getMaxPanoramaSnapshotCount();

    /**
     * Whether preview is rendered via HW overlay or GFx plane
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return true if rendered via HW overlay
     * \return false if rendered via Gfx
     */
    static bool renderPreviewViaOverlay(int cameraId);

    /**
     * Returns whether the resolution is supported by VFPP
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \param width of resolution
     * \param height of resolution
     * \return true if resolution is supported by VFPP, false if not
     */
    static bool resolutionSupportedByVFPP(int cameraId, int width, int height);

    /**
     * Returns the relative rotation between the camera normal scan order
     * and the display attached to the HW overlay.
     * A rotation of this magnitud is required to render correctly the preview
     *
     * \param cameraId identifier passed to android.hardware.Camera.open()
     * \return degrees required to rotate: 0,90,180,270
     */
    static int overlayRotation(int cameraId);

    /**
     * Returns the max zoom factor
     * \return the max zoom factor
     */
    static int getMaxZoomFactor(void);

    /**
     * Whether snapshot in video is supported?
     *
     * \return true if supported
     */
    static bool supportVideoSnapshot(void);

    /**
     * Returns the Recording buffers number
     *
     * \return the recording buffers number
    */
    static int getRecordingBufNum(void);

    /**
     * Whether Intel3A ia_aiq is supported?
     *
     * \return true if supported
     */
    // TODO: remove this until official ia_aiq is adopted
    static bool supportAIQ(void);

    /**
     * Returns the preview format with V4l2 definition
     *
     * \return the preview format, V4L2_PIX_FMT_NV12 or V4L2_PIX_FMT_YVU420
    */
    static int getPreviewFormat(void);

    /**
     * Returns the board name
     *
     * \return the board name, it'll return NULL when it fails
    */
    static const char* getBoardName(void);


};

/**
 * \class PlatformBase
 *
 * Base class for defining static platform features and
 * related configuration data that is needed by rest of the HAL.
 *
 * Each platform will extend this class.
 */
class PlatformBase {

    friend class PlatformData;

public:
    PlatformBase() {    //default
        mPanoramaMaxSnapshotCount = 10;
        mSupportVideoSnapshot = true;
        mNumRecordingBuffers = 9;
        mContinuousCapture = false;
        mMaxContinuousRawRingBuffer = 0;
        mShutterLagCompensationMs = 40;
        mSupportAIQ = false;
        mPreviewFormat = V4L2_PIX_FMT_NV12;
   };

 protected:

    /**
     * Camera feature info that is specific to camera id
     */

    class CameraInfo {
    public:
        CameraInfo() {
            sensorType = SENSOR_TYPE_RAW;
            facing = CAMERA_FACING_BACK;
            orientation = 90;
            flipping = PlatformData::SENSOR_FLIP_NA;
            dvs = true;
            supportedSnapshotSizes = "320x240,640x480,1024x768,1280x720,1920x1080,2048x1536,2560x1920,3264x1836,3264x2448";
            mPreviewViaOverlay = false;
            overlayRelativeRotation = 90;
            //burst
            maxBurstFPS = 15;
            supportedBurstFPS = "1,3,5,7,15";
            supportedBurstLength = "1,3,5,10";
            defaultBurstLength = "10";
            //EV
            maxEV = "2";
            minEV = "-2";
            stepEV = "0.33333333";
            defaultEV = "0";
            //Saturation
            maxSaturation = "";
            minSaturation = "";
            stepSaturation = "";
            defaultSaturation = "";
            supportedSaturation = "";
            //Contrast
            maxContrast = "";
            minContrast = "";
            stepContrast = "";
            defaultContrast = "";
            supportedContrast = "";
            //Sharpness
            maxSharpness = "";
            minSharpness = "";
            stepSharpness = "";
            defaultSharpness = "";
            supportedSharpness = "";
            //FlashMode
            supportedFlashModes.appendFormat("%s,%s,%s,%s"
                ,CameraParameters::FLASH_MODE_AUTO
                ,CameraParameters::FLASH_MODE_OFF
                ,CameraParameters::FLASH_MODE_ON
                ,CameraParameters::FLASH_MODE_TORCH);
            defaultFlashMode.appendFormat("%s", CameraParameters::FLASH_MODE_OFF);
            //Iso
            supportedIso = "iso-auto,iso-100,iso-200,iso-400,iso-800";
            defaultIso = "iso-auto";
            //sceneMode
            supportedSceneModes.appendFormat("%s,%s,%s,%s,%s,%s,%s"
                ,CameraParameters::SCENE_MODE_AUTO
                ,CameraParameters::SCENE_MODE_PORTRAIT
                ,CameraParameters::SCENE_MODE_SPORTS
                ,CameraParameters::SCENE_MODE_LANDSCAPE
                ,CameraParameters::SCENE_MODE_NIGHT
                ,CameraParameters::SCENE_MODE_FIREWORKS
                ,CameraParameters::SCENE_MODE_BARCODE);

            defaultSceneMode.appendFormat("%s", CameraParameters::SCENE_MODE_AUTO);
            //effectMode
            supportedEffectModes.appendFormat("%s,%s,%s,%s"
                ,CameraParameters::EFFECT_NONE
                ,CameraParameters::EFFECT_MONO
                ,CameraParameters::EFFECT_NEGATIVE
                ,CameraParameters::EFFECT_SEPIA);

            supportedIntelEffectModes.appendFormat("%s,%s,%s,%s,%s,%s,%s,%s,%s,%s"
                ,CameraParameters::EFFECT_NONE
                ,CameraParameters::EFFECT_MONO
                ,CameraParameters::EFFECT_NEGATIVE
                ,CameraParameters::EFFECT_SEPIA
                ,IntelCameraParameters::EFFECT_VIVID
                ,IntelCameraParameters::EFFECT_STILL_SKY_BLUE
                ,IntelCameraParameters::EFFECT_STILL_GRASS_GREEN
                ,IntelCameraParameters::EFFECT_STILL_SKIN_WHITEN_LOW
                ,IntelCameraParameters::EFFECT_STILL_SKIN_WHITEN_MEDIUM
                ,IntelCameraParameters::EFFECT_STILL_SKIN_WHITEN_HIGH);
            defaultEffectMode.appendFormat("%s", CameraParameters::EFFECT_NONE);
            //awbmode
            supportedAwbModes.appendFormat("%s,%s,%s,%s,%s"
                ,CameraParameters::WHITE_BALANCE_AUTO
                ,CameraParameters::WHITE_BALANCE_INCANDESCENT
                ,CameraParameters::WHITE_BALANCE_FLUORESCENT
                ,CameraParameters::WHITE_BALANCE_DAYLIGHT
                ,CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT);
            defaultAwbMode.appendFormat("%s", CameraParameters::WHITE_BALANCE_AUTO);
            //ae metering
            supportedAeMetering = "auto,center,spot";
            defaultAeMetering = "auto";
            //preview
            supportedPreviewFrameRate = "30,15,10";
            supportedPreviewFPSRange = "(10500,30304),(11000,30304),(11500,30304)";
            defaultPreviewFPSRange = "10500,30304";
            supportedVideoSizes = "176x144,320x240,352x288,640x480,720x480,720x576,1280x720,1920x1080";
            // Leaving this empty. NOTE: values need to be given in derived classes.
            supportedPreviewSizes = "";
            supportedPreviewUpdateModes = "standard,continuous,during-capture,windowless";
            defaultPreviewUpdateMode = "standard";
            //For high speed recording, slow motion playback
            hasSlowMotion = false;
            // focus modes
            supportedFocusModes.appendFormat("%s,%s,%s,%s,%s,%s"
                ,CameraParameters::FOCUS_MODE_AUTO
                ,CameraParameters::FOCUS_MODE_INFINITY
                ,CameraParameters::FOCUS_MODE_FIXED
                ,CameraParameters::FOCUS_MODE_MACRO
                ,CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO
                ,CameraParameters::FOCUS_MODE_CONTINUOUS_PICTURE);
            defaultFocusMode.appendFormat("%s", CameraParameters::FOCUS_MODE_AUTO);
        };

        SensorType sensorType;
        int facing;
        int orientation;
        int flipping;
        bool dvs;
        String8 supportedSnapshotSizes;
        bool mPreviewViaOverlay;
        int overlayRelativeRotation;  /*<! Relative rotation between the native scan order of the
                                           camera and the display attached to the overlay */
        // VFPP limited resolutions (sensor blanking time dependent
        Vector<Size> mVFPPLimitedResolutions; // preview resolutions with VFPP limitations
        // burst
        int maxBurstFPS;
        String8 supportedBurstFPS;
        String8 supportedBurstLength;
        String8 defaultBurstLength;
        // exposure
        String8 maxEV;
        String8 minEV;
        String8 stepEV;
        String8 defaultEV;
        // AE metering
        String8 supportedAeMetering;
        String8 defaultAeMetering;
        // saturation
        String8 maxSaturation;
        String8 minSaturation;
        String8 stepSaturation;
        String8 defaultSaturation;
        String8 supportedSaturation;
        // contrast
        String8 maxContrast;
        String8 minContrast;
        String8 stepContrast;
        String8 defaultContrast;
        String8 supportedContrast;
        // sharpness
        String8 maxSharpness;
        String8 minSharpness;
        String8 stepSharpness;
        String8 defaultSharpness;
        String8 supportedSharpness;
        // flash
        String8 supportedFlashModes;
        String8 defaultFlashMode;
        // iso
        String8 supportedIso;
        String8 defaultIso;
        // scene modes
        String8 supportedSceneModes;
        String8 defaultSceneMode;
        // effect
        String8 supportedEffectModes;
        String8 supportedIntelEffectModes;
        String8 defaultEffectMode;
        // awb
        String8 supportedAwbModes;
        String8 defaultAwbMode;
        // preview
        String8 supportedPreviewFrameRate;
        String8 supportedPreviewFPSRange;
        String8 defaultPreviewFPSRange;
        String8 supportedPreviewSizes;
        String8 supportedPreviewUpdateModes;
        String8 defaultPreviewUpdateMode;
        String8 supportedVideoSizes;
        // For high speed recording, slow motion playback
        bool hasSlowMotion;
        // focus modes
        String8 supportedFocusModes;
        String8 defaultFocusMode;

    };

    // note: Android NDK does not yet support C++11 and
    //       initializer lists, so avoiding structs for now
    //       in these definitions (2012/May)

    Vector<CameraInfo> mCameras;

    bool mBackFlash;
    bool mFileInject;
    bool mSupportVideoSnapshot;

    bool mContinuousCapture;
    int mMaxContinuousRawRingBuffer;
    int mShutterLagCompensationMs;

    int mPanoramaMaxSnapshotCount;

    String8 mVideoPreviewSizePref;

    /* For EXIF Metadata */
    String8 mProductName;
    String8 mManufacturerName;

    /* For Device name */
    String8 mSubDevName;

    /* For Zoom factor */
    int mMaxZoomFactor;

    /*
     * For Recording Buffers number
     * Because we have 512MB RAM devices, like the Lex,
     * we have less memory for the recording.
     * So we need to make the recording buffers can be configured.
    */
    int mNumRecordingBuffers;

    /* For Intel3A ia_aiq */
    bool mSupportAIQ;

    int mPreviewFormat;

    /* blackbay, or merr_vv, or redhookbay, or victoriabay... */
    String8 mBoardName;
};

} /* namespace android */
#endif /* PLATFORMDATA_H_ */
